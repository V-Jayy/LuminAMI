#include "core.hpp"
#include <map>
#include <optional>

namespace luminami {
namespace {
std::string ascii_z(const Bytes& bytes, size_t& position, size_t end) {
    std::string value;
    while (position < end) {
        auto c = bytes[position++];
        if (!c)
            return value;
        if (c >= 0x80)
            throw Error("Compressed SCSU string requires a decoder");
        value += static_cast<char>(c);
    }
    throw Error("Unterminated HII ASCII string");
}
std::string ucs2_z(const Bytes& bytes, size_t& position, size_t end) {
    std::wstring value;
    while (position + 2 <= end) {
        auto c = read_le(bytes, position, 2);
        position += 2;
        if (!c)
            return utf8(value);
        value += static_cast<wchar_t>(c);
    }
    throw Error("Unterminated HII UCS2 string");
}
struct Strings {
    std::string language;
    std::map<uint32_t, std::string> values;
};
Strings parse_strings(const Bytes& bytes, size_t begin, size_t end) {
    if (end - begin < 47)
        throw Error("Truncated HII string header");
    auto header = read_le(bytes, begin + 4, 4), info = read_le(bytes, begin + 8, 4);
    if (header < 47 || header > end - begin || info < header || info >= end - begin)
        throw Error("Invalid HII string offsets");
    size_t language_position = begin + 46;
    Strings result;
    result.language = ascii_z(bytes, language_position, begin + static_cast<size_t>(header));
    size_t position = begin + static_cast<size_t>(info);
    uint32_t id = 1;
    while (position < end) {
        auto op = bytes[position++];
        if (!op)
            return result;
        if (op >= 0x10 && op <= 0x17) {
            bool font = op & 1;
            if (font) {
                if (position >= end)
                    throw Error("Truncated HII string font");
                ++position;
            }
            uint32_t count = 1;
            if (op == 0x12 || op == 0x13 || op == 0x16 || op == 0x17) {
                if (position + 2 > end)
                    throw Error("Truncated HII string count");
                count = static_cast<uint32_t>(read_le(bytes, position, 2));
                position += 2;
            }
            for (uint32_t i = 0; i < count; ++i) {
                if (id > 0xffff)
                    throw Error("HII string IDs overflow");
                result.values[id++] =
                    op < 0x14 ? ascii_z(bytes, position, end) : ucs2_z(bytes, position, end);
            }
        } else if (op == 0x20) {
            if (position + 2 > end)
                throw Error("Truncated HII duplicate");
            auto duplicate = static_cast<uint32_t>(read_le(bytes, position, 2));
            position += 2;
            auto it = result.values.find(duplicate);
            if (it == result.values.end())
                throw Error("Unknown HII duplicate ID");
            result.values[id++] = it->second;
        } else if (op == 0x21 || op == 0x22) {
            size_t width = op == 0x21 ? 2 : 1;
            if (position + width > end)
                throw Error("Truncated HII skip block");
            auto count = read_le(bytes, position, width);
            position += width;
            if (count > 0x10000 - id)
                throw Error("HII string skip exceeds ID range");
            id += static_cast<uint32_t>(count);
        } else if (op >= 0x30 && op <= 0x32) {
            size_t width = size_t{1} << (op - 0x30);
            if (position + 1 + width > end)
                throw Error("Truncated HII extension");
            auto length = read_le(bytes, position + 1, width);
            if (length < 2 + width || length > end - (position - 1))
                throw Error("Invalid HII extension length");
            position = position - 1 + static_cast<size_t>(length);
        } else
            throw Error("Unsupported HII string block " + std::to_string(op));
    }
    throw Error("HII strings have no END block");
}
std::string lookup(const Strings& strings, uint64_t id) {
    auto it = strings.values.find(static_cast<uint32_t>(id));
    return it == strings.values.end() ? "#string-" + std::to_string(id) : it->second;
}
void parse_forms(const Bytes& bytes, size_t begin, size_t end, const Strings& strings,
                 const std::string& package, Json& result) {
    std::map<std::string, Json> stores;
    struct Frame {
        std::optional<size_t> previous_question;
        std::string previous_formset;
        bool previous_conditional;
        bool previous_layout;
        uint8_t opcode;
        size_t questions_begin;
        std::optional<size_t> owned_question;
    };
    std::vector<Frame> stack;
    std::optional<size_t> question;
    std::string formset = package;
    bool conditional = false, layout = false;
    for (size_t position = begin; position < end;) {
        if (end - position < 2)
            throw Error("Truncated IFR header");
        uint8_t op = bytes[position], length = bytes[position + 1] & 0x7f;
        bool scoped = (bytes[position + 1] & 0x80) != 0;
        if (length < 2 || length > end - position)
            throw Error("Invalid IFR opcode length");
        auto require = [&](size_t n) {
            if (length < n)
                throw Error("Truncated IFR opcode " + std::to_string(op));
        };
        auto field = [&](size_t at, size_t width) {
            require(at + width);
            return read_le(bytes, position + at, width);
        };
        if (op == 0x29) {
            if (scoped || length != 2 || stack.empty())
                throw Error("Unbalanced IFR END");
            auto saved = stack.back();
            stack.pop_back();
            question = saved.previous_question;
            formset = saved.previous_formset;
            conditional = saved.previous_conditional;
            layout = saved.previous_layout;
        } else {
            if (scoped)
                stack.push_back(
                    {question, formset, conditional, layout, op, result["questions"].size(), std::nullopt});
            auto restrict_owner = [&] {
                // The last parsed question can be a completed sibling. Only a
                // question whose scope is still open owns child constraints.
                for (auto it = stack.rbegin(); it != stack.rend(); ++it)
                    if (it->owned_question) {
                        auto& owner = result["questions"][*it->owned_question];
                        owner["conditional_or_extended"] = true;
                        owner["writable"] = false;
                        return true;
                    }
                return false;
            };
            if (op == 0x0e) {
                require(23);
                formset = guid_string(bytes, position + 2);
                question.reset();
            }
            if (op == 0x0a || op == 0x19 || op == 0x1e) {
                // This is a direct-storage configuration tool. Browser layout
                // conditions do not change the question's varstore mapping.
                // SCEWIN's reference export includes these questions as active.
                // Keep submit/security/computed constraints distinct below.
                layout = true;
                for (auto it = stack.rbegin(); it != stack.rend(); ++it)
                    if (it->owned_question) {
                        result["questions"][*it->owned_question]["layout_conditional"] = true;
                        result["questions"][*it->owned_question]["conditional_or_extended"] = true;
                        break;
                    }
            }
            if (op == 0x10 || op == 0x11 || op == 0x63 || op == 0x60) {
                restrict_owner();
                conditional = true;
            }
            if (op == 0x0b) {
                if (!restrict_owner()) {
                    // LOCKED can also apply to a form, including questions
                    // preceding the tag. Keep that entire form read-only.
                    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
                        if (it->opcode == 0x01 || it->opcode == 0x5d || it->opcode == 0x0e) {
                            for (size_t index = it->questions_begin; index < result["questions"].size();
                                 ++index) {
                                result["questions"][index]["conditional_or_extended"] = true;
                                result["questions"][index]["writable"] = false;
                            }
                            break;
                        }
                }
                conditional = true;
            }
            // READ, WRITE and VALUE expressions require browser semantics;
            // raw NVRAM patches cannot reproduce computed/callback behavior.
            if (op == 0x2d || op == 0x2e || op == 0x5a)
                restrict_owner();
            if (op == 0x5f) {
                require(18);
                // Tiano labels and class metadata do not alter storage or visibility.
                // Unknown extensions remain excluded from writes conservatively.
                bool metadata = !scoped && length == 21 &&
                                guid_string(bytes, position + 2) == "0f0b1735-87a0-4193-b266-538c38af48ce" &&
                                (field(18, 1) == 0 || field(18, 1) == 3 || field(18, 1) == 4);
                if (!metadata) {
                    restrict_owner();
                    conditional = true;
                }
            }
            if (op == 0x24 || op == 0x26 || op == 0x25) {
                size_t guid_at = op == 0x24 ? 2 : 4;
                size_t id_at = op == 0x24 ? 18 : 2;
                require(guid_at + 16);
                auto id = field(id_at, 2);
                Json store = {{"id", id},
                              {"formset", formset},
                              {"guid", guid_string(bytes, position + guid_at)},
                              {"kind", op == 0x25   ? "name-value"
                                       : op == 0x24 ? "buffer"
                                                    : "efi"}};
                if (op != 0x25) {
                    auto size_at = op == 0x24 ? 20 : 24, name_at = op == 0x24 ? 22 : 26;
                    store["size"] = field(static_cast<size_t>(size_at), 2);
                    require(static_cast<size_t>(name_at) + 1);
                    size_t cursor = position + static_cast<size_t>(name_at);
                    store["name"] = ascii_z(bytes, cursor, position + length);
                    if (op == 0x26)
                        store["attributes"] = field(20, 4);
                }
                auto key = formset + ":" + std::to_string(id);
                if (stores.count(key))
                    throw Error("Duplicate IFR varstore identity");
                stores[key] = store;
                result["varstores"].push_back(store);
            }
            if (op == 0x05 || op == 0x06 || op == 0x07 || op == 0x1c || op == 0x23 || op == 0x08) {
                require(14);
                auto id = field(6, 2), store = field(8, 2), offset = field(10, 2), flags = field(12, 1);
                bool scalar = op == 0x05 || op == 0x06 || op == 0x07;
                uint64_t width = op == 0x06 ? 1 : scalar ? uint64_t{1} << (field(13, 1) & 3) : 0;
                if (op == 0x1c) {
                    require(16);
                    width = field(14, 1) * 2;
                }
                Json q = {{"package", package},
                          {"formset", formset},
                          {"question_id", id},
                          {"varstore_id", store},
                          {"offset", offset},
                          {"width", width},
                          {"flags", flags},
                          {"opcode", op},
                          {"name", lookup(strings, field(2, 2))},
                          {"help", lookup(strings, field(4, 2))},
                          {"conditional_or_extended", conditional || layout},
                          {"layout_conditional", layout},
                          {"scalar", scalar},
                          {"writable", (scalar || op == 0x1c) && !(flags & 1) && !conditional && store != 0},
                          {"browser_callback", (flags & 4) != 0},
                          {"options", Json::array()}};
                if (op == 0x1c) {
                    q["minimum_characters"] = field(13, 1);
                    q["maximum_characters"] = field(14, 1);
                    q["multiline"] = (field(15, 1) & 1) != 0;
                }
                if (op == 0x05 || op == 0x07) {
                    q["minimum"] = field(14, static_cast<size_t>(width));
                    q["maximum"] = field(14 + static_cast<size_t>(width), static_cast<size_t>(width));
                    q["step"] = field(14 + static_cast<size_t>(width) * 2, static_cast<size_t>(width));
                    q["signed"] = op == 0x07 && (field(13, 1) & 0x30) == 0;
                }
                question = result["questions"].size();
                result["questions"].push_back(q);
                if (scoped)
                    stack.back().owned_question = question;
            }
            if (op == 0x09 && question) {
                auto type = field(5, 1);
                size_t width = type <= 3 ? size_t{1} << type : type == 4 ? 1 : 0;
                if (width)
                    result["questions"][*question]["options"].push_back(
                        {{"name", lookup(strings, field(2, 2))},
                         {"flags", field(4, 1)},
                         {"value", field(6, width)}});
            }
        }
        position += length;
    }
    if (!stack.empty())
        throw Error("Unterminated IFR scope");
    for (auto& q : result["questions"])
        if (q["package"] == package && !q.contains("variable")) {
            auto key =
                q["formset"].get<std::string>() + ":" + std::to_string(q["varstore_id"].get<uint64_t>());
            auto it = stores.find(key);
            if (it == stores.end() || !it->second.contains("name")) {
                q["writable"] = false;
                continue;
            }
            q["variable"] = it->second;
            // A browser callback flag alone does not remove a concrete buffer
            // field from AMI's direct SetVariable interface (e.g. XMP in Setup).
            // Dynamic/name-value callbacks without such storage remain unsupported.
            if (q.value("browser_callback", false) && it->second["kind"] != "buffer")
                q["writable"] = false;
            auto offset = q["offset"].get<uint64_t>(), width = q["width"].get<uint64_t>(),
                 size = it->second["size"].get<uint64_t>();
            if (offset > size || width > size - offset)
                q["writable"] = false;
        }
}
} // namespace
Json inspect_hii(const Bytes& bytes) {
    Json result = {{"format", "uefi-hii-package-lists"}, {"sha256", sha256(bytes)},
                   {"package_lists", Json::array()},     {"varstores", Json::array()},
                   {"questions", Json::array()},         {"warnings", Json::array()}};
    size_t position = 0;
    while (position < bytes.size()) {
        if (bytes.size() - position < 20)
            throw Error("Truncated HII package-list header");
        auto length = read_le(bytes, position + 16, 4);
        if (length < 24 || length > bytes.size() - position)
            throw Error("Invalid HII package-list size at byte " + std::to_string(position));
        auto guid = guid_string(bytes, position);
        size_t end = position + static_cast<size_t>(length), cursor = position + 20;
        Strings strings;
        bool has_strings = false;
        std::vector<std::pair<size_t, size_t>> forms;
        bool terminated = false;
        while (cursor < end) {
            auto header = read_le(bytes, cursor, 4);
            auto size = header & 0xffffff, type = header >> 24;
            if (size < 4 || size > end - cursor)
                throw Error("Invalid HII package size");
            if (type == 4) {
                try {
                    auto candidate = parse_strings(bytes, cursor, cursor + static_cast<size_t>(size));
                    if (!has_strings || candidate.language == "en-US" || candidate.language == "en") {
                        strings = std::move(candidate);
                        has_strings = true;
                    }
                } catch (const Error& e) {
                    result["warnings"].push_back({{"package", guid}, {"strings", e.what()}});
                }
            } else if (type == 2)
                forms.emplace_back(cursor + 4, cursor + static_cast<size_t>(size));
            else if (type == 0xdf) {
                if (size != 4 || cursor + 4 != end)
                    throw Error("Invalid HII package END");
                terminated = true;
            }
            cursor += static_cast<size_t>(size);
        }
        if (!terminated)
            throw Error("HII package list has no END package");
        result["package_lists"].push_back(
            {{"guid", guid}, {"length", length}, {"language", strings.language}});
        for (auto [begin, finish] : forms)
            parse_forms(bytes, begin, finish, strings, guid, result);
        position = end;
    }
    if (result["package_lists"].empty())
        throw Error("Empty HII database");
    result["summary"] = {{"package_lists", result["package_lists"].size()},
                         {"varstores", result["varstores"].size()},
                         {"questions", result["questions"].size()}};
    return result;
}
} // namespace luminami
