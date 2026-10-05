#include "core.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <sstream>

namespace luminami {
namespace {
std::string trim(std::string text) {
    auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}
std::string normalize(std::string text) {
    text = trim(text);
    if (text.rfind("//", 0) == 0)
        text = trim(text.substr(2));
    return text;
}
uint64_t number(const std::string& text, int base) {
    auto value = trim(text);
    size_t used = 0;
    if (value.empty() || value[0] == '-' || value[0] == '+')
        throw Error("Invalid unsigned value");
    uint64_t result;
    try {
        result = std::stoull(value, &used, base);
    } catch (...) {
        throw Error("Invalid numeric value: " + value);
    }
    if (used != value.size())
        throw Error("Invalid numeric suffix: " + value);
    return result;
}
std::string encode_text(const std::string& raw) {
    // Preserve legacy file bytes for edits; JSON presents invalid UTF-8 bytes
    // as Latin-1 rather than silently deleting them.
    try {
        (void)wide(raw);
        return raw;
    } catch (const Error&) {
    }
    std::wstring text;
    for (unsigned char c : raw)
        text += static_cast<wchar_t>(c);
    return utf8(text);
}
struct Line {
    std::string text, ending;
};
std::vector<Line> lines(const Bytes& bytes) {
    if (std::find(bytes.begin(), bytes.end(), 0) != bytes.end())
        throw Error("UTF-16 or binary scripts are unsupported; use AMI ANSI or UTF-8 text");
    std::vector<Line> out;
    std::string source(bytes.begin(), bytes.end());
    if (source.rfind("\xef\xbb\xbf", 0) == 0)
        source.erase(0, 3);
    size_t position = 0;
    while (position < source.size()) {
        auto end = source.find('\n', position);
        auto text = source.substr(position, end == std::string::npos ? end : end - position);
        std::string ending = end == std::string::npos ? "" : "\n";
        if (!text.empty() && text.back() == '\r') {
            text.pop_back();
            ending = "\r" + ending;
        }
        out.push_back({text, ending});
        if (end == std::string::npos)
            break;
        position = end + 1;
    }
    return out;
}
std::string identity(const Json& q) {
    return q.at("token").dump() + ":" + q.at("name").get<std::string>() + ":" + q.at("offset").dump() + ":" +
           q.at("width").dump();
}
Json parse(const std::vector<Line>& source) {
    Json out = {{"format", "ami-setup-script"},
                {"hii_crc32", nullptr},
                {"questions", Json::array()},
                {"issues", Json::array()}};
    Json current;
    bool in_options = false;
    bool lumin_format = false;
    for (const auto& line : source)
        if (line.text == "// LuminAMI settings export v1")
            lumin_format = true;
    const std::regex field(R"(^([^=]+)=\s*(.*)$)");
    const std::regex option(R"(^\s*(\*)?\[([0-9a-fA-F]+)\](.*)$)");
    auto finish = [&]() {
        if (current.is_null())
            return;
        for (const char* key : {"token", "offset", "width"})
            if (current[key].is_null())
                out["issues"].push_back(
                    {{"line", current["line"]}, {"error", std::string("Missing ") + key}});
        if (!current["options"].empty()) {
            size_t selected = 0;
            for (auto& op : current["options"])
                if (op["selected"] == true)
                    ++selected;
            if (selected != 1)
                out["issues"].push_back(
                    {{"line", current["line"]}, {"error", "Options must have exactly one selection"}});
        }
        out["questions"].push_back(current);
        current = nullptr;
    };
    for (size_t i = 0; i < source.size(); ++i) {
        auto text = normalize(source[i].text);
        std::smatch match;
        // Option labels may contain '=' (for example UCLK=MEMCLK). Recognize
        // continuation rows before treating text as a metadata assignment.
        if (!(in_options && std::regex_match(text, match, option)) && std::regex_match(text, match, field)) {
            auto key = trim(match[1].str()), value = trim(match[2].str());
            bool quoted = key == "Value" && !value.empty() && value.front() == '"';
            auto comment = value.find("//");
            if (!quoted && comment != std::string::npos)
                value = trim(value.substr(0, comment));
            if (key == "HIICrc32") {
                out["hii_crc32"] = value;
                continue;
            }
            if (key == "Setup Question") {
                finish();
                in_options = false;
                current = {{"name", encode_text(value)},
                           {"line", i + 1},
                           {"commented", trim(source[i].text).rfind("//", 0) == 0},
                           {"token", nullptr},
                           {"offset", nullptr},
                           {"width", nullptr},
                           {"options", Json::array()},
                           {"value", nullptr}};
            } else if (!current.is_null()) {
                in_options = key == "Options";
                if (key == "Token" || key == "Offset" || key == "Width") {
                    try {
                        auto n = number(value, 16);
                        std::string dest = key == "Token" ? "token" : key == "Offset" ? "offset" : "width";
                        if (!current[dest].is_null())
                            throw Error("Duplicate field " + key);
                        current[dest] = n;
                    } catch (const Error& e) {
                        out["issues"].push_back({{"line", i + 1}, {"error", e.what()}});
                    }
                } else if (key == "Value") {
                    current["value_line"] = i + 1;
                    if (quoted) {
                        try {
                            if (lumin_format) {
                                current["value"] = Json::parse(value);
                                current["value_format"] = "utf8-string";
                            } else {
                                auto end = value.find_last_of('"');
                                if (end == 0)
                                    throw Error("Unterminated quoted string");
                                current["value"] = encode_text(value.substr(1, end - 1));
                                current["value_format"] = "legacy-string";
                            }
                            if (!current["value"].is_string())
                                throw Error("Quoted value must be a string");
                        } catch (const std::exception& e) {
                            current["value"] = nullptr;
                            out["issues"].push_back({{"line", i + 1}, {"error", e.what()}});
                        }
                        continue;
                    }
                    current["value_format"] =
                        value.size() > 1 && value.front() == '<' ? "decimal-angle" : "hex";
                    if (current["value_format"] == "hex") {
                        try {
                            current["value"] = number(value, 16);
                        } catch (const Error& e) {
                            current["value"] = encode_text(value);
                            out["issues"].push_back({{"line", i + 1}, {"error", e.what()}});
                        }
                    } else
                        current["value"] = encode_text(value);
                } else if (key == "Signed") {
                    if (value != "0" && value != "1")
                        out["issues"].push_back({{"line", i + 1}, {"error", "Invalid signed flag"}});
                    else
                        current["signed"] = value == "1";
                } else if (key == "Help String")
                    current["help"] = encode_text(value);
                if (in_options)
                    text = value;
            }
        }
        if (in_options && !current.is_null() && std::regex_match(text, match, option)) {
            auto label = match[3].str();
            auto c = label.find("//");
            if (c != std::string::npos)
                label.resize(c);
            auto n = number(match[2].str(), 16);
            bool selected = match[1].matched;
            current["options"].push_back({{"code", match[2].str()},
                                          {"value", n},
                                          {"label", encode_text(trim(label))},
                                          {"selected", selected},
                                          {"line", i + 1}});
            if (selected)
                current["value"] = n;
        }
    }
    finish();
    size_t commented = 0;
    for (const auto& q : out["questions"])
        if (q["commented"] == true)
            ++commented;
    out["summary"] = {{"questions", out["questions"].size()},
                      {"active", out["questions"].size() - commented},
                      {"commented", commented},
                      {"issues", out["issues"].size()}};
    return out;
}
} // namespace
Json inspect_script(const std::filesystem::path& path) {
    auto bytes = read_file(path);
    auto out = parse(lines(bytes));
    out["sha256"] = sha256(bytes);
    return out;
}
Json script_diff(const std::filesystem::path& before, const std::filesystem::path& after) {
    auto left = inspect_script(before), right = inspect_script(after);
    if (left["hii_crc32"].is_null() || left["hii_crc32"] != right["hii_crc32"])
        throw Error("Missing or different HII checksum; scripts are not comparable");
    std::map<std::string, Json> known;
    for (auto& q : left["questions"]) {
        auto key = identity(q);
        if (!known.emplace(key, q).second)
            throw Error("Ambiguous repeated question identity");
    }
    Json changes = Json::array();
    for (auto& q : right["questions"]) {
        auto it = known.find(identity(q));
        if (it == known.end())
            throw Error("Question metadata was added or altered");
        auto old = it->second;
        if (old["commented"] != q["commented"])
            throw Error("Question activation changed");
        if (old["value"] != q["value"])
            changes.push_back({{"token", q["token"]},
                               {"name", q["name"]},
                               {"before", old["value"]},
                               {"after", q["value"]}});
        known.erase(it);
    }
    if (!known.empty())
        throw Error("Questions were removed");
    return {{"hii_crc32", left["hii_crc32"]}, {"changes", changes}, {"writes_firmware", false}};
}
void edit_script(const std::filesystem::path& input, const std::filesystem::path& output, uint64_t token,
                 const std::string& value) {
    std::error_code ec;
    if (std::filesystem::equivalent(input, output, ec))
        throw Error("Output must differ from input");
    auto bytes = read_file(input);
    auto source = lines(bytes);
    auto doc = parse(source);
    Json found;
    for (auto& q : doc["questions"])
        if (q["token"] == token && q["commented"] == false) {
            if (!found.is_null())
                throw Error("Token matches multiple questions");
            found = q;
        }
    if (found.is_null())
        throw Error("Active token not found");
    auto width = found["width"].get<uint64_t>();
    auto format = found.value("value_format", std::string{});
    if (format == "utf8-string" || format == "legacy-string") {
        if (!width || width % 2 || width > 510)
            throw Error("Invalid UTF16 string width");
        (void)encode_utf16(value, static_cast<size_t>(width / 2));
        if (format == "legacy-string" && value.find_first_of("\"\r\n") != std::string::npos)
            throw Error("Legacy AMI strings cannot represent quotes or newlines safely");
        auto i = found["value_line"].get<size_t>() - 1;
        auto equals = source[i].text.find('=');
        source[i].text = source[i].text.substr(0, equals + 1) + "\t" +
                         (format == "utf8-string" ? Json(value).dump() : "\"" + value + "\"");
    } else {
        if (!width || width > 8)
            throw Error("Unsupported setting width");
        uint64_t desired = 0;
        int64_t signed_desired = 0;
        if (found.value("signed", false)) {
            size_t used = 0;
            if (value.empty() || value[0] == '+')
                throw Error("Invalid signed value");
            try {
                signed_desired = std::stoll(value, &used, 0);
            } catch (...) {
                throw Error("Signed value overflows");
            }
            if (used != value.size())
                throw Error("Invalid signed value suffix");
            if (width < 8 && (signed_desired < -(int64_t{1} << (width * 8 - 1)) ||
                              signed_desired > ((int64_t{1} << (width * 8 - 1)) - 1)))
                throw Error("Signed value exceeds setting width");
            desired = static_cast<uint64_t>(signed_desired);
            if (width < 8)
                desired &= (uint64_t{1} << (width * 8)) - 1;
        } else
            desired = number(value, 0);
        if (width < 8 && desired >= (uint64_t{1} << (width * 8)))
            throw Error("Value exceeds setting width");
        if (!found["options"].empty()) {
            bool valid = false;
            for (auto& op : found["options"])
                if (op["value"] == desired)
                    valid = true;
            if (!valid)
                throw Error("Requested value is absent from options");
            const std::regex marker(R"((\*)?(\[[0-9a-fA-F]+\]))");
            for (auto& op : found["options"]) {
                auto i = op["line"].get<size_t>() - 1;
                std::smatch match;
                if (!std::regex_search(source[i].text, match, marker))
                    throw Error("Cannot locate option marker");
                source[i].text.replace(static_cast<size_t>(match.position()),
                                       static_cast<size_t>(match.length()),
                                       (op["value"] == desired ? "*" : "") + match[2].str());
            }
        } else {
            if (!found.contains("value_line"))
                throw Error("Question has no editable scalar value");
            auto i = found["value_line"].get<size_t>() - 1;
            auto& text = source[i].text;
            auto equals = text.find('=');
            auto comment = text.find("//", equals + 1);
            bool angle = found["value"].is_string() &&
                         found["value"].get<std::string>().find('<') != std::string::npos;
            std::ostringstream scalar;
            if (angle && found.value("signed", false))
                scalar << signed_desired;
            else if (angle)
                scalar << desired;
            else
                scalar << std::uppercase << std::hex << desired;
            text = text.substr(0, equals + 1) + "\t" + (angle ? "<" : "") + scalar.str() +
                   (angle ? ">" : "") + (comment == std::string::npos ? "" : "\t" + text.substr(comment));
        }
    }
    Bytes result;
    if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        result = {0xef, 0xbb, 0xbf};
    for (const auto& line : source) {
        result.insert(result.end(), line.text.begin(), line.text.end());
        result.insert(result.end(), line.ending.begin(), line.ending.end());
    }
    write_file(output, result);
}
} // namespace luminami
