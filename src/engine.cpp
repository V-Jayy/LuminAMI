#include "core.hpp"
#include <map>
#include <sstream>
#include <iomanip>
#include <bit>
#include <cwctype>

namespace luminami {
namespace {
Json load_capture(const std::filesystem::path& root) {
    auto bytes = read_file(root / L"capture.json");
    auto manifest = Json::parse(bytes);
    if (manifest.value("format", "") != "luminami-capture-v1")
        throw Error("Unsupported capture manifest");
    if (!manifest.value("capture_complete", true) ||
        (manifest.contains("errors") && !manifest["errors"].empty()))
        throw Error("Capture contains unresolved read failures");
    auto hii = read_file(root / L"hii.bin");
    if (manifest.at("hii_sha256") != sha256(hii))
        throw Error("HII capture checksum mismatch");
    manifest["catalog"] = inspect_hii(hii); // Never trust an editable catalog sidecar for addressing.
    for (auto& variable : manifest.at("variables")) {
        auto raw = unhex(variable.at("data").get<std::string>());
        if (variable.at("sha256") != sha256(raw) || variable.at("size") != raw.size())
            throw Error("Variable capture checksum or size mismatch");
        variable["guid"] = guid_string(guid_bytes(variable.at("guid").get<std::string>()), 0);
    }
    return manifest;
}
std::string h(uint64_t n, size_t width = 2) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setw(static_cast<int>(width)) << std::setfill('0') << n;
    return stream.str();
}
std::string one_line(std::string value) {
    for (auto& c : value)
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
    auto comment = value.find("//");
    if (comment != std::string::npos)
        value.resize(comment);
    auto start = value.find_first_not_of(' ');
    return start == std::string::npos ? "" : value.substr(start, value.find_last_not_of(' ') - start + 1);
}
Json catalog_values(const Json& manifest) {
    std::map<std::string, Bytes> values;
    for (const auto& v : manifest["variables"]) {
        auto key = v["guid"].get<std::string>() + ":" + v["name"].get<std::string>();
        if (!values.emplace(key, unhex(v["data"].get<std::string>())).second)
            throw Error("Repeated capture variable identity");
    }
    Json result = Json::array();
    uint64_t token = 0;
    for (auto q : manifest["catalog"]["questions"]) {
        ++token;
        if ((q["scalar"] != true && q["opcode"] != 0x1c) || !q.contains("variable"))
            continue;
        auto store = q["variable"];
        auto key = store["guid"].get<std::string>() + ":" + store["name"].get<std::string>();
        auto it = values.find(key);
        if (it == values.end()) {
            bool explicitly_missing = false;
            if (manifest.contains("unavailable_variables"))
                for (const auto& missing : manifest["unavailable_variables"])
                    if (missing["guid"].get<std::string>() + ":" + missing["name"].get<std::string>() ==
                            key &&
                        missing["status"] == 0x8e)
                        explicitly_missing = true;
            if (explicitly_missing)
                continue;
            throw Error("Missing captured varstore: " + key);
        }
        auto offset = q["offset"].get<size_t>(), width = q["width"].get<size_t>();
        for (const auto& v : manifest["variables"])
            if (v["guid"].get<std::string>() + ":" + v["name"].get<std::string>() == key &&
                v["attributes"] != 7)
                q["writable"] = false;
        if (offset > it->second.size() || width > it->second.size() - offset)
            throw Error("Question exceeds its captured variable");
        if (q["opcode"] == 0x1c) {
            Bytes field(it->second.begin() + static_cast<ptrdiff_t>(offset),
                        it->second.begin() + static_cast<ptrdiff_t>(offset + width));
            q["value"] = decode_utf16(field);
            q["value_bytes"] = hex(field);
            auto characters = wide(q["value"].get<std::string>()).size();
            if (characters < q["minimum_characters"].get<size_t>())
                q["writable"] = false;
        } else {
            q["raw_value"] = read_le(it->second, offset, width);
            q["value"] = q["raw_value"];
            if (q.value("signed", false))
                q["value"] = signed_value(q["raw_value"].get<uint64_t>(), width);
        }
        q["token"] = token;
        q["name"] = one_line(q["name"].get<std::string>());
        q["help"] = one_line(q["help"].get<std::string>());
        for (auto& option : q["options"])
            option["name"] = one_line(option["name"].get<std::string>());
        if (store["guid"] == "8be4df61-93ca-11d2-aa0d-00e098032b8c" && store["name"] == "PlatformLang" &&
            q["opcode"] == 5) {
            // The browser stores a language tag, whereas this IFR question and
            // AMISCE present a language index. It is not a scalar byte field.
            const auto& data = it->second;
            auto end = std::find(data.begin(), data.end(), 0);
            std::string language(data.begin(), end);
            q["storage_representation"] = "language-tag";
            q["writable"] = false;
            auto codes = values.find("8be4df61-93ca-11d2-aa0d-00e098032b8c:PlatformLangCodes");
            if (codes != values.end()) {
                auto stop = std::find(codes->second.begin(), codes->second.end(), 0);
                std::string tags(codes->second.begin(), stop);
                q["options"] = Json::array();
                size_t position = 0;
                uint64_t index = 0;
                while (position < tags.size()) {
                    auto next = tags.find(';', position);
                    auto tag = tags.substr(position, next == std::string::npos ? next : next - position);
                    if (tag.empty())
                        throw Error("Empty PlatformLangCodes entry");
                    (void)wide(tag);
                    q["options"].push_back({{"name", tag}, {"flags", 0}, {"value", index++}});
                    if (next == std::string::npos)
                        break;
                    position = next + 1;
                }
            }
            for (const auto& option : q["options"])
                if (option["name"] == language)
                    q["value"] = option["value"];
        }
        if (q["name"].get<std::string>().rfind("#string-", 0) == 0)
            q["writable"] = false;
        if (q["opcode"] == 5) {
            bool found = false;
            for (auto& option : q["options"])
                if (option["value"] == q["value"])
                    found = true;
            if (!found)
                q["writable"] = false;
        }
        if (q["opcode"] == 7) {
            auto step = q["step"].get<uint64_t>();
            if (q.value("signed", false)) {
                auto value = q["value"].get<int64_t>(),
                     minimum = signed_value(q["minimum"].get<uint64_t>(), width),
                     maximum = signed_value(q["maximum"].get<uint64_t>(), width);
                if (value < minimum || value > maximum ||
                    (step && (static_cast<uint64_t>(value) - static_cast<uint64_t>(minimum)) % step))
                    q["writable"] = false;
            } else {
                auto value = q["value"].get<uint64_t>(), minimum = q["minimum"].get<uint64_t>(),
                     maximum = q["maximum"].get<uint64_t>();
                if (value < minimum || value > maximum || (step && (value - minimum) % step))
                    q["writable"] = false;
            }
        }
        result.push_back(q);
    }
    return result;
}
std::string render(const Json& manifest, const Json& questions, bool duplicates) {
    std::string out = "// LuminAMI settings export v1\r\n// Token identities are LuminAMI-specific. Import "
                      "through LuminAMI with the matching capture.\r\n";
    out += "// HII SHA256 = " + manifest["hii_sha256"].get<std::string>() + "\r\n";
    // Confirmed against a fresh AMISCE export on the validation board.
    out += "HIICrc32= " + h(crc32(unhex(manifest["hii_bytes"].get<std::string>())), 8) + "\r\n\r\n";
    std::map<std::string, size_t> counts;
    for (const auto& q : questions)
        ++counts[q["name"].get<std::string>()];
    for (const auto& q : questions) {
        if (duplicates && counts[q["name"].get<std::string>()] < 2)
            continue;
        std::string prefix = (duplicates || q["writable"] == false) ? "// " : "";
        auto line = [&](const std::string& text) { out += prefix + text + "\r\n"; };
        line("Setup Question\t= " + one_line(q["name"].get<std::string>()));
        line("Help String\t= " + one_line(q["help"].get<std::string>()));
        line("Token\t=" + h(q["token"].get<uint64_t>()) + "\t// Do NOT change this line");
        line("Offset\t=" + h(q["offset"].get<uint64_t>()));
        line("Width\t=" + h(q["width"].get<uint64_t>()));
        if (q.value("signed", false))
            line("Signed\t=1");
        if (q["opcode"] == 5 && !q["options"].empty()) {
            bool first = true, selected = false;
            for (const auto& option : q["options"]) {
                bool mark = !selected && option["value"] == q["value"];
                line((first ? "Options\t=" : "         ") + std::string(mark ? "*" : "") + "[" +
                     h(option["value"].get<uint64_t>(), q["width"].get<size_t>() * 2) + "]" +
                     one_line(option["name"].get<std::string>()));
                selected = selected || mark;
                first = false;
            }
        } else if (q["opcode"] == 0x1c)
            line("Value\t=" + q["value"].dump());
        else
            line("Value\t=<" + q["value"].dump() + ">");
        out += "\r\n";
    }
    return out;
}
} // namespace
Json export_live_workflow(const std::filesystem::path& capture, const std::filesystem::path& script,
                          const std::filesystem::path& duplicates, const std::function<Json()>& capture_now) {
    if (!capture_now)
        throw Error("Live export requires a capture transport");
    if (capture.empty() || script.empty() || std::filesystem::exists(capture))
        throw Error("Live export requires a new capture directory and settings path");
    auto canonical = [](const std::filesystem::path& path) {
        auto text = std::filesystem::weakly_canonical(std::filesystem::absolute(path)).wstring();
        // Windows path identity is case-insensitive in the supported workspace.
        std::transform(text.begin(), text.end(), text.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return text;
    };
    const auto root = canonical(capture), main = canonical(script);
    if (!duplicates.empty() && canonical(duplicates) == main)
        throw Error("Settings and duplicate output must differ");
    for (const auto& path : {script, duplicates})
        if (!path.empty()) {
            if (std::filesystem::exists(path))
                throw Error("Live export output already exists");
            const auto destination = canonical(path);
            if (destination == root || destination == canonical(capture / L"capture.json") ||
                destination == canonical(capture / L"catalog.json") ||
                destination == canonical(capture / L"hii.bin"))
                throw Error("Export output collides with retained capture evidence");
            auto parent = std::filesystem::absolute(path).parent_path();
            if (canonical(parent) != root && !std::filesystem::is_directory(parent))
                throw Error("Export output parent directory does not exist");
        }
    // Check output collisions before loading a kernel driver or reading firmware.
    // Retain the capture if decoding/export fails; it is the diagnostic evidence.
    auto captured = capture_now();
    if (!captured.value("ok", false))
        throw Error("Live capture did not succeed");
    auto result = export_capture(capture, script, duplicates);
    result["source"] = "live AMI HII and varstores";
    result["capture"] = utf8(capture.wstring());
    return result;
}
Json export_capture(const std::filesystem::path& capture, const std::filesystem::path& script,
                    const std::filesystem::path& duplicates) {
    if (std::filesystem::exists(script) || (!duplicates.empty() && std::filesystem::exists(duplicates)))
        throw Error("Export output already exists");
    if (!duplicates.empty() && std::filesystem::absolute(script).lexically_normal() ==
                                   std::filesystem::absolute(duplicates).lexically_normal())
        throw Error("Settings and duplicate output must differ");
    auto manifest = load_capture(capture);
    manifest["hii_bytes"] = hex(read_file(capture / L"hii.bin"));
    auto questions = catalog_values(manifest);
    if (questions.empty())
        throw Error("HII capture contains no supported scalar setup questions");
    auto main = render(manifest, questions, false);
    write_file(script, Bytes(main.begin(), main.end()));
    if (!duplicates.empty()) {
        auto dupes = render(manifest, questions, true);
        write_file(duplicates, Bytes(dupes.begin(), dupes.end()));
    }
    return {{"ok", true},
            {"questions", questions.size()},
            {"script", utf8(script.wstring())},
            {"source", "captured HII and varstores"},
            {"writes_firmware", false}};
}
Json plan_import(const std::filesystem::path& capture, const std::filesystem::path& script) {
    auto manifest = load_capture(capture);
    manifest["hii_bytes"] = hex(read_file(capture / L"hii.bin"));
    auto catalog = catalog_values(manifest), doc = inspect_script(script);
    if (doc["hii_crc32"] != h(crc32(read_file(capture / L"hii.bin")), 8))
        throw Error("Script HII checksum does not match capture");
    auto source = read_file(script);
    const std::string text(source.begin(), source.end());
    if (text.find("// HII SHA256 = " + manifest["hii_sha256"].get<std::string>()) == std::string::npos)
        throw Error("Script lacks the matching LuminAMI HII identity");
    std::map<uint64_t, Json> known;
    for (auto& q : catalog)
        known.emplace(q["token"].get<uint64_t>(), q);
    Json patches = Json::array();
    std::map<std::string, std::map<size_t, uint8_t>> modified;
    for (const auto& q : doc["questions"]) {
        if (q["token"].is_null() || q["offset"].is_null() || q["width"].is_null())
            throw Error("Question metadata is incomplete");
        auto it = known.find(q["token"].get<uint64_t>());
        if (it == known.end())
            throw Error("Unknown or repeated question token");
        const auto original = it->second;
        if (q["name"] != original["name"] || q["offset"] != original["offset"] ||
            q["width"] != original["width"] || q.value("signed", false) != original.value("signed", false) ||
            q["commented"].get<bool>() == original["writable"].get<bool>())
            throw Error("Question identity or activation was altered for token " +
                        std::to_string(q["token"].get<uint64_t>()) + " (" +
                        original["name"].get<std::string>() + ")");
        known.erase(it);
        if (q["commented"] == true)
            continue;
        if (original["opcode"] == 0x1c) {
            if (q.value("value_format", std::string{}) != "utf8-string" || !q["value"].is_string() ||
                !q["options"].empty())
                throw Error("String value requires the exported quoted UTF8 syntax");
            auto desired = q["value"].get<std::string>();
            if (!original.value("multiline", false) && desired.find_first_of("\r\n") != std::string::npos)
                throw Error("String question does not allow multiple lines");
            auto count = wide(desired).size();
            if (count < original["minimum_characters"].get<size_t>())
                throw Error("String is shorter than its IFR minimum character count");
            auto after = encode_utf16(desired, original["maximum_characters"].get<size_t>());
            if (original["value"] == desired)
                continue;
            auto store = original["variable"];
            auto offset = original["offset"].get<size_t>();
            auto key = store["guid"].get<std::string>() + ":" + store["name"].get<std::string>();
            for (size_t i = 0; i < after.size(); ++i) {
                auto existing = modified[key].find(offset + i);
                if (existing != modified[key].end() && existing->second != after[i])
                    throw Error("String and scalar patches request conflicting varstore bytes");
                modified[key][offset + i] = after[i];
            }
            patches.push_back({{"token", original["token"]},
                               {"name", original["name"]},
                               {"variable", store["name"]},
                               {"guid", store["guid"]},
                               {"offset", offset},
                               {"width", after.size()},
                               {"encoding", "utf16"},
                               {"before", original["value"]},
                               {"after", desired},
                               {"before_data", original["value_bytes"]},
                               {"after_data", hex(after)}});
            continue;
        }
        uint64_t value = 0;
        if (q["value"].is_number_unsigned() || q["value"].is_number_integer())
            value = q["value"].get<uint64_t>();
        else if (q["value"].is_string()) {
            auto scalar = q["value"].get<std::string>();
            if (scalar.size() < 3 || scalar.front() != '<' || scalar.back() != '>')
                throw Error("Unsupported scalar value syntax");
            scalar = scalar.substr(1, scalar.size() - 2);
            size_t used = 0;
            auto digits = scalar;
            if (original.value("signed", false) && !digits.empty() && digits.front() == '-')
                digits.erase(0, 1);
            if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos)
                throw Error("Invalid decimal scalar value");
            try {
                if (original.value("signed", false))
                    value = static_cast<uint64_t>(std::stoll(scalar, &used, 10));
                else
                    value = std::stoull(scalar, &used, 10);
            } catch (...) {
                throw Error("Scalar value overflows");
            }
            if (used != scalar.size())
                throw Error("Invalid scalar value");
        } else
            throw Error("Question has no selected value");
        size_t selected = 0;
        for (const auto& option : q["options"])
            if (option["selected"] == true)
                ++selected;
        if (original["opcode"] == 5) {
            if (selected != 1 || q["options"].size() != original["options"].size())
                throw Error("One-of option metadata changed");
            bool valid = false;
            for (size_t i = 0; i < q["options"].size(); ++i) {
                if (q["options"][i]["value"] != original["options"][i]["value"] ||
                    q["options"][i]["label"] != original["options"][i]["name"])
                    throw Error("One-of option identity changed");
                if (original["options"][i]["value"] == value)
                    valid = true;
            }
            if (!valid)
                throw Error("Value is absent from live options");
        }
        auto width = original["width"].get<size_t>();
        if (original.value("signed", false)) {
            if (q.value("value_format", std::string{}) == "hex")
                value = std::bit_cast<uint64_t>(signed_value(value, width));
            auto signed_number = std::bit_cast<int64_t>(value);
            if (width < 8 && (signed_number < -(int64_t{1} << (width * 8 - 1)) ||
                              signed_number > ((int64_t{1} << (width * 8 - 1)) - 1)))
                throw Error("Signed value exceeds varstore field width");
            if (width < 8)
                value &= (uint64_t{1} << (width * 8)) - 1;
        }
        if (width < 8 && value >= (uint64_t{1} << (width * 8)))
            throw Error("Value exceeds varstore field width");
        if (original["opcode"] == 6 && value > 1)
            throw Error("Checkbox requires 0 or 1");
        if (original["opcode"] == 7) {
            auto step = original["step"].get<uint64_t>();
            if (original.value("signed", false)) {
                auto n = signed_value(value, width),
                     minimum = signed_value(original["minimum"].get<uint64_t>(), width),
                     maximum = signed_value(original["maximum"].get<uint64_t>(), width);
                if (n < minimum || n > maximum ||
                    (step && (static_cast<uint64_t>(n) - static_cast<uint64_t>(minimum)) % step))
                    throw Error("Value violates signed IFR numeric constraints");
            } else {
                auto minimum = original["minimum"].get<uint64_t>(),
                     maximum = original["maximum"].get<uint64_t>();
                if (value < minimum || value > maximum || (step && (value - minimum) % step))
                    throw Error("Value violates IFR numeric constraints");
            }
        }
        if (original["raw_value"] == value)
            continue;
        const auto store = original["variable"];
        auto key = store["guid"].get<std::string>() + ":" + store["name"].get<std::string>();
        auto offset = original["offset"].get<size_t>();
        for (size_t i = 0; i < width; ++i) {
            auto byte = static_cast<uint8_t>(value >> (i * 8));
            auto existing = modified[key].find(offset + i);
            if (existing != modified[key].end() && existing->second != byte)
                throw Error("Questions request conflicting writes to the same varstore bytes");
            modified[key][offset + i] = byte;
        }
        patches.push_back({{"token", original["token"]},
                           {"name", original["name"]},
                           {"variable", store["name"]},
                           {"guid", store["guid"]},
                           {"offset", offset},
                           {"width", width},
                           {"before", original["raw_value"]},
                           {"after", value}});
    }
    if (!known.empty())
        throw Error("Questions were removed from the script");
    Json variables = Json::array();
    for (const auto& v : manifest["variables"]) {
        auto key = v["guid"].get<std::string>() + ":" + v["name"].get<std::string>();
        auto changes = modified.find(key);
        if (changes == modified.end())
            continue;
        if (v["attributes"] != 7)
            throw Error("Import requires ordinary NV/BS/RT attributes for " + v["name"].get<std::string>());
        auto before = unhex(v["data"].get<std::string>()), after = before;
        for (const auto& [offset, byte] : changes->second) {
            if (offset >= after.size())
                throw Error("Import patch exceeds its captured variable");
            after[offset] = byte;
        }
        variables.push_back({{"name", v["name"]},
                             {"guid", v["guid"]},
                             {"attributes", v["attributes"]},
                             {"size", before.size()},
                             {"before_data", hex(before)},
                             {"after_data", hex(after)},
                             {"before_sha256", sha256(before)},
                             {"after_sha256", sha256(after)}});
    }
    return {{"format", "luminami-import-plan-v1"},
            {"hii_sha256", manifest["hii_sha256"]},
            {"script_sha256", sha256(source)},
            {"platform", manifest["platform"]},
            {"patches", patches},
            {"variables", variables},
            {"writes_firmware", false},
            {"hardware_apply_ready", false},
            {"reason", "Plan only; explicit live import checks the hardware, HII and every touched baseline "
                       "before writing"}};
}
Json plan_restore(const std::filesystem::path& target_capture, const std::filesystem::path& live_capture,
                  const std::filesystem::path& rebased_script) {
    if (std::filesystem::exists(rebased_script))
        throw Error("Rebased restore script already exists");
    auto target = load_capture(target_capture), live = load_capture(live_capture);
    if (target.at("hii_sha256") != live.at("hii_sha256") || target.at("platform") != live.at("platform")) {
        // Capture platform status also contains transient service/elevation
        // fields; compare only hardware identity, not incidental probe state.
        if (target.at("hii_sha256") != live.at("hii_sha256") ||
            target.at("platform").at("smbios_sha256") != live.at("platform").at("smbios_sha256"))
            throw Error("Backup hardware or HII identity differs from the live board");
    }
    auto desired = catalog_values(target), current = catalog_values(live);
    std::map<uint64_t, Json> by_token;
    for (const auto& q : desired)
        by_token.emplace(q.at("token").get<uint64_t>(), q);
    for (auto& q : current) {
        auto it = by_token.find(q.at("token").get<uint64_t>());
        if (it == by_token.end())
            continue;
        const auto& old = it->second;
        if (q.at("writable") != true || old.at("writable") != true)
            continue;
        if (q.at("name") != old.at("name") || q.at("variable") != old.at("variable") ||
            q.at("width") != old.at("width") || q.at("offset") != old.at("offset"))
            throw Error("Backup question storage mapping changed");
        q["value"] = old.at("value");
    }
    live["hii_bytes"] = hex(read_file(live_capture / L"hii.bin"));
    auto script = render(live, current, false);
    write_file(rebased_script, Bytes(script.begin(), script.end()));
    return plan_import(live_capture, rebased_script);
}
} // namespace luminami
