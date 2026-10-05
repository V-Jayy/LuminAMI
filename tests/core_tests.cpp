#define NOMINMAX
#include <Windows.h>
#include "core.hpp"
#include "drivers.hpp"
#include <functional>
#include <iostream>
#include <random>
#include <map>

using namespace luminami;
namespace {
int checks = 0;
void check(bool success, const std::string& message) {
    ++checks;
    if (!success)
        throw Error("Test failed: " + message);
}
void rejects(const std::function<void()>& operation, const std::string& message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, message);
}
Bytes text(const std::string& value) {
    return Bytes(value.begin(), value.end());
}
void append(Bytes& target, const Bytes& extra) {
    target.insert(target.end(), extra.begin(), extra.end());
}
void word(Bytes& target, uint16_t value) {
    target.push_back(static_cast<uint8_t>(value));
    target.push_back(static_cast<uint8_t>(value >> 8));
}
Bytes fixture_hii(bool string_question = false, bool signed_question = false, uint8_t child_guard = 0,
                  uint8_t question_flags = 0, bool duplicate_options = false, bool equals_labels = false) {
    auto guid = guid_bytes("12345678-1234-5678-9012-123456789012");
    Bytes strings(52);
    put_le(strings, 4, 4, 52);
    put_le(strings, 8, 4, 52);
    const std::string language = "en-US";
    std::copy(language.begin(), language.end(), strings.begin() + 46);
    for (const std::string value :
         {"Fan Mode", "Fan configuration", equals_labels ? "UCLK=MEMCLK" : "Disabled",
          equals_labels ? "UCLK=MEMCLK/2" : "Enabled", "Fan Limit", "Numeric limit"}) {
        strings.push_back(0x14);
        for (char c : value)
            word(strings, static_cast<uint16_t>(c));
        word(strings, 0);
    }
    strings.push_back(0);
    put_le(strings, 0, 4, strings.size() | 0x04000000);
    Bytes forms(4);
    Bytes formset(23);
    formset[0] = 0x0e;
    formset[1] = 0x80 | 23;
    std::copy(guid.begin(), guid.end(), formset.begin() + 2);
    append(forms, formset);
    Bytes label(21);
    label[0] = 0x5f;
    label[1] = 21;
    auto tiano = guid_bytes("0f0b1735-87a0-4193-b266-538c38af48ce");
    std::copy(tiano.begin(), tiano.end(), label.begin() + 2);
    put_le(label, 19, 2, 0x1234);
    append(forms, label);
    Bytes store(28);
    store[0] = 0x24;
    store[1] = 28;
    std::copy(guid.begin(), guid.end(), store.begin() + 2);
    put_le(store, 18, 2, 1);
    put_le(store, 20, 2, string_question ? 68 : 4);
    const std::string variable = "Setup";
    std::copy(variable.begin(), variable.end(), store.begin() + 22);
    append(forms, store);
    Bytes oneof(17);
    oneof[0] = 5;
    oneof[1] = 0x80 | 17;
    put_le(oneof, 2, 2, 1);
    put_le(oneof, 4, 2, 2);
    put_le(oneof, 6, 2, 7);
    put_le(oneof, 8, 2, 1);
    oneof[12] = question_flags;
    put_le(oneof, 14, 1, 0);
    put_le(oneof, 15, 1, 1);
    append(forms, oneof);
    for (uint8_t i = 0; i < 2; ++i) {
        Bytes option(7);
        option[0] = 9;
        option[1] = 7;
        put_le(option, 2, 2, 3 + i);
        option[6] = duplicate_options ? 0 : i;
        append(forms, option);
    }
    if (child_guard) {
        if (child_guard == 0x0b)
            append(forms, {child_guard, 2});
        else {
            Bytes guard(child_guard == 0x10 || child_guard == 0x11 ? 4
                        : child_guard == 0x63                      ? 5
                        : child_guard == 0x60                      ? 18
                                                                   : 2);
            guard[0] = child_guard;
            guard[1] = static_cast<uint8_t>(guard.size()) | 0x80;
            append(forms, guard);
            append(forms, {0x46, 2});
            append(forms, {0x29, 2});
        }
    }
    append(forms, {0x29, 2});
    Bytes numeric(20);
    numeric[0] = 7;
    numeric[1] = 20;
    put_le(numeric, 2, 2, 5);
    put_le(numeric, 4, 2, 6);
    put_le(numeric, 6, 2, 8);
    put_le(numeric, 8, 2, 1);
    put_le(numeric, 10, 2, 1);
    numeric[13] = 0x11;
    put_le(numeric, 14, 2, 0);
    put_le(numeric, 16, 2, 1000);
    put_le(numeric, 18, 2, 10);
    append(forms, numeric);
    if (signed_question) {
        auto offset = forms.size() - numeric.size();
        forms[offset + 13] = 1;
        put_le(forms, offset + 14, 2, 0xff9c);
        put_le(forms, offset + 16, 2, 100);
    }
    if (string_question) {
        Bytes string(16);
        string[0] = 0x1c;
        string[1] = 16;
        put_le(string, 2, 2, 1);
        put_le(string, 6, 2, 9);
        put_le(string, 8, 2, 1);
        put_le(string, 10, 2, 4);
        string[13] = 1;
        string[14] = 32;
        append(forms, string);
    }
    append(forms, {0x29, 2});
    put_le(forms, 0, 4, forms.size() | 0x02000000);
    Bytes result = guid;
    result.resize(20);
    append(result, strings);
    append(result, forms);
    append(result, {4, 0, 0, 0xdf});
    put_le(result, 16, 4, result.size());
    return result;
}
void save_capture(const std::filesystem::path& root, bool string_question = false,
                  bool signed_question = false, bool equals_labels = false) {
    std::filesystem::create_directories(root);
    auto hii = fixture_hii(string_question, signed_question, 0, 0, false, equals_labels);
    Bytes variable = {1, 100, 0, 0};
    if (signed_question) {
        variable[1] = 0xec;
        variable[2] = 0xff;
    }
    if (string_question)
        append(variable, encode_utf16("AB", 32));
    Json manifest = {{"format", "luminami-capture-v1"},
                     {"hii_sha256", sha256(hii)},
                     {"platform", {{"fixture", true}}},
                     {"variables", Json::array({{{"name", "Setup"},
                                                 {"guid", "12345678-1234-5678-9012-123456789012"},
                                                 {"size", variable.size()},
                                                 {"data", hex(variable)},
                                                 {"sha256", sha256(variable)},
                                                 {"attributes", 7}}})}};
    write_file(root / L"hii.bin", hii);
    write_json(root / L"capture.json", manifest);
}
} // namespace
int main() {
    try {
        check(crc32(text("123456789")) == 0xcbf43926, "CRC32 known vector");
        check(sha256(text("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256 known vector");
        check(signed_value(0x80, 1) == -128 && signed_value(0xff9c, 2) == -100 &&
                  signed_value(0xffffffff, 4) == -1 && signed_value(uint64_t{1} << 63, 8) == INT64_MIN,
              "Signed field interpretation preserves two's-complement limits");
        check(hex(encode_utf16("\xf0\x9f\x98\x80", 2)) == "3dd800de" &&
                  decode_utf16(unhex("3dd800de")) == "\xf0\x9f\x98\x80",
              "UTF16 surrogate pair roundtrip preserves supplementary Unicode");
        rejects([] { encode_utf16("\xf0\x9f\x98\x80", 1); },
                "UTF16 maximum counts code units rather than UTF8 bytes or glyphs");
        rejects([] { decode_utf16(unhex("3dd8")); }, "Invalid trailing surrogate is rejected");
        rejects([] { encode_utf16(std::string("A\0B", 3), 4); },
                "Embedded NUL cannot truncate a planned string");
        auto packet = protocol::get_variable(0x12345000, "HiiDB", protocol::HiiGuid, 512);
        check(read_le(packet, 0, 4) == 0x100, "GetVariable operation");
        check(read_le(packet, 4, 4) == 0x12345030, "Request pointer");
        check(read_le(packet, 0x40, 4) == 0x12345050, "UTF16 name pointer");
        check(read_le(packet, 0x4c, 4) == 0x1234505c, "Data follows NUL-terminated name");
        check(guid_string(packet, 0x30) == protocol::HiiGuid, "HiiDB GUID layout");
        check(packet[0x50] == 'H' && packet[0x51] == 0 && read_le(packet, 0x5a, 2) == 0,
              "UTF16 variable name terminator");
        check(protocol::smi(0xb2, 0xef, 0x12345000).size() == 0x26, "SMI IOCTL byte count");
        check(read_le(protocol::smi(0xb2, 0xef, 0x12345000), 0xe, 4) == 0x12345000, "Unaligned SMI register");
        auto next = protocol::next_variable(0x12345000, "", "00000000-0000-0000-0000-000000000000");
        check(next.size() == 0x248 && read_le(next, 0x44, 4) == 0x200, "Enumeration request units");
        auto chunk = protocol::hii_read(0x12345000, 0xa00000, 100);
        check(read_le(chunk, 0xfc4, 4) == 0x500 && read_le(chunk, 0xff8, 4) == 0x12345000 &&
                  read_le(chunk, 0xffc, 4) == 100,
              "HII chunk layout");
        auto negotiation = protocol::wsmt(0xb2);
        check(negotiation.size() == 0x3e && read_le(negotiation, 2, 4) == 0x11000,
              "WSMT driver negotiation packet");
        // A negotiated response from the supported driver is sufficient even
        // when Windows cannot expose an AMI UEFI table. Addresses are synthetic.
        put_le(negotiation, 6, 8, 0x77a6e000);
        put_le(negotiation, 0x0e, 8, 0x20010000000);
        put_le(negotiation, 0x16, 8, 0x77a6d018);
        put_le(negotiation, 0x1e, 8, 0x2000ffff018);
        put_le(negotiation, 0x26, 8, 1);
        put_le(negotiation, 0x26 + 8, 8, 0x77a6e000);
        put_le(negotiation, 0x26 + 20, 4, 4);
        auto mapping = protocol::decode_wsmt(negotiation, 0xb2);
        check(mapping.physical == 0x77a6e000 && mapping.context_physical == 0x77a6d018,
              "Driver-managed WSMT works without an ACPI context or guessed template version");
        check(protocol::decode_wsmt(negotiation, 0xb2, 0x77a6d018).virtual_address == 0x20010000000,
              "Published AMI context can corroborate the driver mapping");
        rejects([&] { protocol::decode_wsmt(negotiation, 0xb2, 0x77a6d000); },
                "Conflicting published context is rejected before using a mapping");
        rejects([&] { protocol::decode_wsmt(negotiation, 0xb3); },
                "Negotiation cannot change the validated SMI port");
        auto malformed_mapping = negotiation;
        malformed_mapping.pop_back();
        rejects([&] { protocol::decode_wsmt(malformed_mapping, 0xb2); },
                "Truncated negotiation cannot supply firmware pointers");
        for (const auto offset : {size_t{6}, size_t{0x0e}, size_t{0x16}, size_t{0x1e}}) {
            malformed_mapping = negotiation;
            put_le(malformed_mapping, offset, 8, 0);
            rejects([&] { protocol::decode_wsmt(malformed_mapping, 0xb2); },
                    "Missing negotiated address is rejected");
            put_le(malformed_mapping, offset, 8, UINT64_MAX);
            rejects([&] { protocol::decode_wsmt(malformed_mapping, 0xb2); },
                    "Negotiated address wraparound is rejected");
        }
        malformed_mapping = negotiation;
        put_le(malformed_mapping, 0x26 + 8, 8, 0x77a6f000);
        rejects([&] { protocol::decode_wsmt(malformed_mapping, 0xb2); },
                "Context cannot refer to a different communication buffer");
        malformed_mapping = negotiation;
        put_le(malformed_mapping, 2, 4, protocol::MaxBuffer - 1);
        rejects([&] { protocol::decode_wsmt(malformed_mapping, 0xb2); },
                "Negotiation must honor the requested communication capacity");
        Bytes initial(24);
        put_le(initial, 20, 4, 0x40);
        auto context = protocol::wsmt_context(initial, protocol::smi(0xb2, 0xef, 0x12345000));
        check(context.size() == 0x50 && read_le(context, 0x10, 4) == 0xc0000004 &&
                  read_le(context, 0x18, 4) == 0xef,
              "WSMT v2 invocation envelope");
        check(read_le(context, 0x40, 8) == 0x12345000 && read_le(context, 20, 4) == 0x40,
              "WSMT ESI pointer and retained template length");
        // amigendrv's F18/F1C dispatch is an arithmetic-and-mask comparison,
        // not a literal F1C constant.
        check(((uint32_t{0xfa002f1c} + uint32_t{0x05ffd0e8}) & uint32_t{0xfffffffb}) == 0,
              "Generic driver handles F1C through masked dispatch");
        check(((uint32_t{0xfa002f20} + uint32_t{0x05ffd0e8}) & uint32_t{0xfffffffb}) != 0,
              "Masked dispatch does not accidentally accept F20");
        rejects([] { (void)protocol::get_variable(0xfffffff0, "Setup", protocol::HiiGuid, 512); },
                "Reject physical pointer overflow");
        rejects([] { (void)protocol::get_variable(0x1000, "Setup", protocol::HiiGuid, protocol::MaxBuffer); },
                "Reject oversized firmware request");
        rejects([] { (void)protocol::hii_read(0x1000, 0xfffffff0, 100); }, "Reject HII source wraparound");
        auto set = protocol::set_variable(0x12345000, "Setup", protocol::HiiGuid, 7, Bytes{1, 2, 3});
        check(read_le(set, 0, 4) == 0x300 && read_le(set, 0x44, 4) == 7 && read_le(set, 0x48, 4) == 3,
              "SetVariable operation, attributes and length match researched function");
        check(read_le(set, 0x4c, 4) == 0x1234505c && set[0x5c] == 1 && set.back() == 3,
              "SetVariable data occupies the addressed payload");
        rejects([] { (void)protocol::set_variable(0x1000, "Setup", protocol::HiiGuid, 7, {}); },
                "Roundtrip cannot delete a variable");
        rejects([] { (void)protocol::set_variable(0x1000, "Setup", protocol::HiiGuid, 0x27, {1}); },
                "Roundtrip cannot alter authenticated variables");
        rejects([] { (void)guid_bytes(""); }, "Reject empty GUID");
        auto catalog = inspect_hii(fixture_hii());
        check(catalog["questions"].size() == 2, "Decode two IFR questions");
        check(catalog["questions"][0]["name"] == "Fan Mode", "Resolve language string ID");
        check(catalog["questions"][0]["variable"]["name"] == "Setup", "Resolve scoped varstore");
        check(catalog["questions"][0]["writable"] == true,
              "Tiano label does not disable subsequent questions");
        check(catalog["questions"][1]["width"] == 2 && catalog["questions"][1]["step"] == 10,
              "Decode numeric width and constraints");
        check(inspect_hii(fixture_hii(false, false, 0, 4))["questions"][0]["writable"] == true,
              "Browser callback with concrete buffer storage supports direct AMI configuration");
        check(inspect_hii(fixture_hii(false, false, 0, 5))["questions"][0]["writable"] == false,
              "Read-only flag remains enforced even when a callback has buffer storage");
        for (uint8_t guard : std::initializer_list<uint8_t>{0x10, 0x11, 0x63, 0x60, 0x0b, 0x2d, 0x2e, 0x5a}) {
            auto constrained = inspect_hii(fixture_hii(false, false, guard));
            check(constrained["questions"][0]["writable"] == false &&
                      constrained["questions"][0]["conditional_or_extended"] == true,
                  "Child IFR constraint disables owning question: " + std::to_string(guard));
            check(constrained["questions"][1]["writable"] == true,
                  "Child IFR constraint does not leak into sibling question: " + std::to_string(guard));
        }
        for (uint8_t guard : std::initializer_list<uint8_t>{0x0a, 0x19, 0x1e}) {
            auto layout = inspect_hii(fixture_hii(false, false, guard));
            check(layout["questions"][0]["writable"] == true &&
                      layout["questions"][0]["layout_conditional"] == true,
                  "Browser layout condition does not disable direct-storage editing: " +
                      std::to_string(guard));
            check(layout["questions"][1]["layout_conditional"] == false,
                  "Browser layout condition does not leak into siblings: " + std::to_string(guard));
        }
        auto string_catalog = inspect_hii(fixture_hii(true));
        check(string_catalog["questions"][2]["width"] == 64 &&
                  string_catalog["questions"][2]["maximum_characters"] == 32,
              "String question width uses UTF16 maximum character count");
        check(string_catalog["questions"][2]["scalar"] == false &&
                  string_catalog["questions"][2]["writable"] == true,
              "Ordinary string questions are typed separately from scalar writes");
        auto malformed = fixture_hii();
        put_le(malformed, 16, 4, 0xffffffff);
        rejects([&] { (void)inspect_hii(malformed); }, "Reject malformed package length");
        for (size_t i = 0; i < fixture_hii().size(); ++i) {
            auto truncated = fixture_hii();
            truncated.resize(i);
            rejects([&] { (void)inspect_hii(truncated); },
                    "Reject truncated HII at byte " + std::to_string(i));
        }
        auto workspace = std::filesystem::absolute(L"test-work") /
                         (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(workspace);
        auto driver_state = workspace / L"driver-state";
        auto driver_cache = workspace / L"provided drivers";
        check(!ami_driver_status(driver_state)["configured"].get<bool>(),
              "Empty driver state is not configured");
        rejects([&] { resolve_ami_driver({}, driver_state); },
                "Headless driver lookup never installs implicitly");
        rejects([&] { verify_ami_driver(text("not a driver")); }, "Unsupported driver bytes are rejected");
        for (const auto& source : DriverSources) {
            auto bytes = provided_ami_driver(source);
            check(sha256(bytes) == source.sha256, "Embedded driver bytes match pinned hash");
            check(std::string(verify_ami_driver(bytes).name) == source.name,
                  "Driver type is identified by bytes");
        }
        auto installed = install_ami_drivers(driver_cache, driver_state);
        check(installed["ok"] == true && installed["loads_driver"] == false &&
                  installed["writes_firmware"] == false,
              "Installer extracts without loading a driver or accessing firmware");
        check(installed["drivers"].size() == 2 && installed["drivers"][0]["extracted"] == true,
              "Installer supplies both AMI drivers");
        check(resolve_ami_driver({}, driver_state) == driver_cache / L"amigendrv64.sys",
              "Installed default is resolved from persistent state");
        auto reused = install_ami_drivers(driver_cache, driver_state);
        check(reused["drivers"][0]["extracted"] == false && reused["drivers"][1]["extracted"] == false,
              "Repeated installation reuses verified files");
        auto custom = workspace / L"custom driver.sys";
        write_file(custom, provided_ami_driver(DriverSources[1]));
        remember_ami_driver(custom, driver_state);
        check(resolve_ami_driver({}, driver_state) == custom,
              "A custom path is remembered without copying the file");
        auto config_hash = sha256(read_file(driver_state / L"driver.json"));
        auto invalid_driver = workspace / L"invalid.sys";
        write_file(invalid_driver, text("wrong bytes"));
        rejects([&] { remember_ami_driver(invalid_driver, driver_state); },
                "Invalid custom selection fails before saving");
        check(sha256(read_file(driver_state / L"driver.json")) == config_hash,
              "Rejected selection keeps the previous choice");
        write_file(custom, text("replaced after selection"), true);
        check(ami_driver_status(driver_state)["configured"] == false,
              "Saved drivers are verified again on lookup");
        rejects([&] { resolve_ami_driver({}, driver_state); },
                "Changed custom bytes are never silently replaced or loaded");
        auto bad_cache = workspace / L"bad-driver-cache";
        std::filesystem::create_directories(bad_cache);
        write_file(bad_cache / L"amigendrv64.sys", text("existing unverified file"));
        rejects([&] { install_ami_drivers(bad_cache, driver_state); },
                "Installer does not overwrite a mismatched existing driver");
        check(!std::filesystem::exists(bad_cache / L"amifldrv64.sys"),
              "Failed installation publishes no other driver");
        check(sha256(read_file(driver_state / L"driver.json")) == config_hash,
              "Failed installation does not change selection");
        auto capture = workspace / L"capture", original = workspace / L"original.txt",
             edited = workspace / L"edited.txt", numeric = workspace / L"numeric.txt";
        save_capture(capture);
        auto exported = export_capture(capture, original, workspace / L"dupes.txt");
        check(exported["questions"] == 2, "Export settings from captured HII and variable bytes");
        auto parsed = inspect_script(original);
        check(parsed["summary"]["issues"] == 0 && parsed["summary"]["questions"] == 2,
              "Parse generated AMI-style settings");
        check(plan_import(capture, original)["patches"].empty(), "Unedited roundtrip proposes zero writes");
        auto baseline = sha256(read_file(original));
        auto equals_capture = workspace / L"equals-labels", equals_script = workspace / L"equals.txt",
             equals_edit = workspace / L"equals-edited.txt";
        save_capture(equals_capture, false, false, true);
        export_capture(equals_capture, equals_script);
        auto equals_document = inspect_script(equals_script);
        check(equals_document["summary"]["issues"] == 0 &&
                  equals_document["questions"][0]["options"].size() == 2 &&
                  equals_document["questions"][0]["options"][1]["label"] == "UCLK=MEMCLK/2",
              "Equals signs in first and continuation option labels preserve metadata and selection");
        check(plan_import(equals_capture, equals_script)["patches"].empty(),
              "Unedited export with equals-containing option labels proposes zero writes");
        edit_script(equals_script, equals_edit, 1, "0");
        auto equals_plan = plan_import(equals_capture, equals_edit);
        check(equals_plan["patches"].size() == 1 && equals_plan["patches"][0]["after"] == 0,
              "Changing a selection preserves equals-containing option identities");
        auto duplicate_capture = workspace / L"duplicate-options";
        save_capture(duplicate_capture);
        auto duplicate_hii = fixture_hii(false, false, 0, 0, true);
        auto duplicate_manifest = Json::parse(read_file(duplicate_capture / L"capture.json"));
        duplicate_manifest["hii_sha256"] = sha256(duplicate_hii);
        duplicate_manifest["variables"][0]["data"] = "00640000";
        duplicate_manifest["variables"][0]["sha256"] = sha256(Bytes{0, 100, 0, 0});
        write_file(duplicate_capture / L"hii.bin", duplicate_hii, true);
        write_json(duplicate_capture / L"capture.json", duplicate_manifest, true);
        auto duplicate_script = workspace / L"duplicate-options.txt";
        export_capture(duplicate_capture, duplicate_script);
        auto duplicate_document = inspect_script(duplicate_script);
        check(duplicate_document["summary"]["issues"] == 0,
              "Duplicate-valued options export exactly one selection");
        check(duplicate_document["questions"][0]["options"][0]["selected"] == true &&
                  duplicate_document["questions"][0]["options"][1]["selected"] == false,
              "Duplicate option values use the first matching entry consistently");
        check(plan_import(duplicate_capture, duplicate_script)["patches"].empty(),
              "Duplicate-valued option export remains a valid unchanged import baseline");
        size_t captures_started = 0;
        auto live_capture = workspace / L"live-export", live_script = live_capture / L"settings.txt",
             live_dupes = live_capture / L"Dupes.txt";
        auto fake_capture = [&] {
            ++captures_started;
            save_capture(live_capture);
            return Json{{"ok", true}};
        };
        rejects([&] { export_live_workflow(live_capture, original, {}, fake_capture); },
                "Live export rejects existing settings before transport access");
        rejects([&] { export_live_workflow(live_capture, live_capture / L"HII.BIN", {}, fake_capture); },
                "Live export rejects case-insensitive capture evidence collision");
        rejects(
            [&] {
                export_live_workflow(live_capture, live_script, live_capture / L"SETTINGS.TXT", fake_capture);
            },
            "Live export rejects aliased duplicate output before capture");
        rejects(
            [&] {
                export_live_workflow(live_capture, workspace / L"missing-parent" / L"settings.txt", {},
                                     fake_capture);
            },
            "Live export rejects missing output parent before capture");
        check(captures_started == 0, "Rejected live exports never access the capture transport");
        auto live_result = export_live_workflow(live_capture, live_script, live_dupes, fake_capture);
        check(captures_started == 1 && live_result["questions"] == 2 && std::filesystem::exists(live_dupes),
              "Live export captures once and creates settings plus duplicates");
        check(plan_import(live_capture, live_script)["patches"].empty(),
              "Single-command live export creates a valid unchanged import baseline");
        rejects([&] { export_live_workflow(live_capture, workspace / L"new.txt", {}, fake_capture); },
                "Live export cannot reuse and overwrite an existing capture");
        check(captures_started == 1, "Existing capture refusal occurs before transport access");
        auto restore_live = workspace / L"restore-live";
        save_capture(restore_live);
        auto restore_manifest = Json::parse(read_file(restore_live / L"capture.json"));
        auto restore_bytes = Bytes{0, 120, 0, 7};
        restore_manifest["variables"][0]["data"] = hex(restore_bytes);
        restore_manifest["variables"][0]["sha256"] = sha256(restore_bytes);
        write_json(restore_live / L"capture.json", restore_manifest, true);
        auto planned_restore = plan_restore(capture, restore_live, workspace / L"restore-rebased.txt");
        check(planned_restore["patches"].size() == 2 &&
                  planned_restore["variables"][0]["before_data"] == "00780007" &&
                  planned_restore["variables"][0]["after_data"] == "01640007",
              "Backup restore rebases supported fields onto live bytes and preserves unrelated data");
        auto restore_hii = read_file(restore_live / L"hii.bin");
        restore_hii[20 + 46] = 'f';
        write_file(restore_live / L"hii.bin", restore_hii, true);
        restore_manifest["hii_sha256"] = sha256(restore_hii);
        write_json(restore_live / L"capture.json", restore_manifest, true);
        rejects([&] { plan_restore(capture, restore_live, workspace / L"restore-wrong-hii.txt"); },
                "Backup restore rejects a different HII identity");
        auto strings_capture = workspace / L"strings-capture", strings_export = workspace / L"strings.txt",
             strings_edit = workspace / L"strings-edited.txt";
        save_capture(strings_capture, true);
        export_capture(strings_capture, strings_export);
        check(inspect_script(strings_export)["questions"][2]["value"] == "AB" &&
                  plan_import(strings_capture, strings_export)["patches"].empty(),
              "String export/import preserves its decoded UTF16 value and unused bytes");
        const std::string quoted_path = "C:\\a\"b // c\xf0\x9f\x98\x80";
        edit_script(strings_export, strings_edit, 3, quoted_path);
        auto string_plan = plan_import(strings_capture, strings_edit);
        check(string_plan["patches"].size() == 1 && string_plan["patches"][0]["encoding"] == "utf16" &&
                  string_plan["patches"][0]["after"] == quoted_path,
              "String editing preserves backslashes, quotes, comment-like text and Unicode");
        auto string_before = unhex(string_plan["variables"][0]["before_data"]),
             string_after = unhex(string_plan["variables"][0]["after_data"]);
        check(Bytes(string_after.begin(), string_after.begin() + 4) ==
                      Bytes(string_before.begin(), string_before.begin() + 4) &&
                  decode_utf16(Bytes(string_after.begin() + 4, string_after.end())) == quoted_path,
              "String transaction stays inside its declared field");
        auto string_live = string_before;
        VariableOperations string_ops{
            [&](const std::string& name, const std::string& guid) {
                return Json{{"name", name}, {"guid", guid}, {"attributes", 7}, {"data", hex(string_live)}};
            },
            [&](const std::string&, const std::string&, uint32_t, const Bytes& data) { string_live = data; }};
        auto string_receipt =
            execute_import(string_plan, workspace / L"string-roundtrip.json", string_ops, true);
        check(string_receipt["rollback_verified"] == true && string_live == string_before,
              "UTF16 transaction verifies and restores the exact original variable");
        edit_script(strings_export, workspace / L"empty-string.txt", 3, "");
        rejects([&] { plan_import(strings_capture, workspace / L"empty-string.txt"); },
                "String minimum is enforced against IFR");
        rejects([&] { edit_script(strings_export, workspace / L"long-string.txt", 3, std::string(33, 'X')); },
                "String maximum is enforced before file publication");
        edit_script(strings_export, workspace / L"multiline-string.txt", 3, "A\nB");
        rejects([&] { plan_import(strings_capture, workspace / L"multiline-string.txt"); },
                "Single-line IFR string rejects newlines");
        edit_script(strings_export, workspace / L"max-string.txt", 3, std::string(32, 'X'));
        check(plan_import(strings_capture, workspace / L"max-string.txt")["patches"][0]["width"] == 64,
              "Full maximum string length uses the published width without writing a terminator beyond it");
        auto signed_capture = workspace / L"signed-capture", signed_export = workspace / L"signed.txt",
             signed_edit = workspace / L"signed-edited.txt";
        save_capture(signed_capture, false, true);
        export_capture(signed_capture, signed_export);
        check(inspect_script(signed_export)["questions"][1]["value"] == "<-20>" &&
                  plan_import(signed_capture, signed_export)["patches"].empty(),
              "Signed field export and unchanged import interpret negative current values");
        edit_script(signed_export, signed_edit, 2, "-50");
        auto signed_plan = plan_import(signed_capture, signed_edit);
        check(signed_plan["patches"][0]["after"] == 0xffce &&
                  signed_plan["variables"][0]["after_data"] == "01ceff00",
              "Negative signed value is encoded into the correct field bytes");
        auto signed_hex_bytes = read_file(signed_edit);
        std::string signed_hex_text(signed_hex_bytes.begin(), signed_hex_bytes.end());
        auto decimal_position = signed_hex_text.find("<-50>");
        signed_hex_text.replace(decimal_position, 5, "FFCE");
        write_file(workspace / L"signed-hex.txt", text(signed_hex_text));
        check(plan_import(signed_capture, workspace / L"signed-hex.txt")["patches"][0]["after"] == 0xffce,
              "Signed hexadecimal input retains its two's-complement field interpretation");
        edit_script(signed_export, workspace / L"signed-step.txt", 2, "-51");
        rejects([&] { plan_import(signed_capture, workspace / L"signed-step.txt"); },
                "Signed step constraints hold across negative values");
        edit_script(signed_export, workspace / L"signed-range.txt", 2, "-110");
        rejects([&] { plan_import(signed_capture, workspace / L"signed-range.txt"); },
                "Signed minimum is enforced");
        rejects([&] { edit_script(signed_export, workspace / L"signed-overflow.txt", 2, "-32769"); },
                "Signed physical width overflow is rejected");
        edit_script(original, edited, 1, "0");
        check(sha256(read_file(original)) == baseline, "Editing leaves original intact");
        check(script_diff(original, edited)["changes"].size() == 1, "Diff reports exactly one change");
        auto planned = plan_import(capture, edited);
        check(planned["patches"].size() == 1 && planned["patches"][0]["after"] == 0,
              "Import resolves option to original varstore bytes");
        check(planned["writes_firmware"] == false && planned["hardware_apply_ready"] == false,
              "Planning cannot silently write firmware");
        check(planned["variables"].size() == 1 && planned["variables"][0]["before_data"] == "01640000" &&
                  planned["variables"][0]["after_data"] == "00640000",
              "Import prepares the whole variable while changing only the selected field");
        auto multi = planned;
        auto other = multi["variables"][0];
        other["name"] = "Other";
        other["before_data"] = "01020304";
        other["after_data"] = "01020904";
        other["before_sha256"] = sha256(unhex("01020304"));
        other["after_sha256"] = sha256(unhex("01020904"));
        multi["variables"].push_back(other);
        multi["patches"].push_back({{"variable", "Other"},
                                    {"guid", other["guid"]},
                                    {"offset", 2},
                                    {"width", 1},
                                    {"before", 3},
                                    {"after", 9}});
        std::map<std::string, Bytes> nv;
        std::vector<std::string> write_order;
        auto reset_nv = [&] {
            nv = {{"Setup", unhex("01640000")}, {"Other", unhex("01020304")}};
            write_order.clear();
        };
        auto read_nv = [&](const std::string& name, const std::string& guid) {
            return Json{{"name", name}, {"guid", guid}, {"attributes", 7}, {"data", hex(nv.at(name))}};
        };
        auto write_nv = [&](const std::string& name, const std::string&, uint32_t attributes,
                            const Bytes& data) {
            check(attributes == 7, "Transaction retains NV/BS/RT attributes");
            write_order.push_back(name);
            nv.at(name) = data;
        };
        reset_nv();
        auto committed = execute_import(multi, workspace / L"multi-success.json", {read_nv, write_nv});
        check(committed["stage"] == "committed" && nv["Setup"] == unhex("00640000") &&
                  nv["Other"] == unhex("01020904"),
              "Multi-variable transaction commits only exact prepared payloads");
        reset_nv();
        nv["Other"][0] = 2;
        rejects([&] { execute_import(multi, workspace / L"stale.json", {read_nv, write_nv}); },
                "Stale later variable aborts before any earlier write");
        check(write_order.empty() && !std::filesystem::exists(workspace / L"stale.json"),
              "All baselines are checked before mutations or journal publication");
        reset_nv();
        size_t import_calls = 0;
        auto failing_second = [&](const std::string& name, const std::string& guid, uint32_t attributes,
                                  const Bytes& data) {
            write_nv(name, guid, attributes, data);
            if (++import_calls == 2)
                throw Error("simulated partial second-store failure");
        };
        rejects([&] { execute_import(multi, workspace / L"multi-failed.json", {read_nv, failing_second}); },
                "Multi-store partial write triggers reverse rollback");
        check(nv["Setup"] == unhex("01640000") && nv["Other"] == unhex("01020304") &&
                  write_order == std::vector<std::string>({"Setup", "Other", "Other", "Setup"}),
              "Rollback includes the failing target and restores earlier writes in reverse order");
        check(Json::parse(read_file(workspace / L"multi-failed.json"))["rollback_verified"] == true,
              "Journal verifies recovery after a failed multi-store write");
        reset_nv();
        import_calls = 0;
        auto external_change = [&](const std::string& name, const std::string& guid, uint32_t attributes,
                                   const Bytes& data) {
            write_nv(name, guid, attributes, data);
            if (++import_calls == 2) {
                nv["Setup"][3] = 99;
                throw Error("simulated unrelated external change");
            }
        };
        rejects(
            [&] { execute_import(multi, workspace / L"external-change.json", {read_nv, external_change}); },
            "Rollback after external changes reports the original failure");
        check(nv["Setup"] == unhex("01640063") && nv["Other"] == unhex("01020304"),
              "Rollback preserves unrelated external bytes");
        reset_nv();
        import_calls = 0;
        auto conflicting_change = [&](const std::string& name, const std::string& guid, uint32_t attributes,
                                      const Bytes& data) {
            write_nv(name, guid, attributes, data);
            if (++import_calls == 2) {
                nv["Setup"][0] = 9;
                throw Error("simulated overlapping external change");
            }
        };
        rejects(
            [&] { execute_import(multi, workspace / L"field-conflict.json", {read_nv, conflicting_change}); },
            "Rollback refuses an overlapping external value");
        auto conflict_receipt = Json::parse(read_file(workspace / L"field-conflict.json"));
        check(nv["Setup"][0] == 9 && nv["Other"] == unhex("01020304") &&
                  conflict_receipt["stage"] == "restore_failed",
              "Conflicting field is retained and incomplete restoration is recorded");
        reset_nv();
        auto restored_transaction =
            execute_import(multi, workspace / L"multi-roundtrip.json", {read_nv, write_nv}, true);
        check(restored_transaction["rollback_verified"] == true && nv["Setup"] == unhex("01640000") &&
                  nv["Other"] == unhex("01020304"),
              "Full import transaction can validate readback and then restore every changed store");
        auto tampered_plan = multi;
        tampered_plan["variables"][0]["after_data"] = "ffffffff";
        reset_nv();
        rejects([&] { execute_import(tampered_plan, workspace / L"bad-payload.json", {read_nv, write_nv}); },
                "Payload checksum failure rejects the transaction before writes");
        check(write_order.empty(), "Tampered transaction cannot reach a write callback");
        auto unreviewed = multi;
        unreviewed["variables"][0]["after_data"] = "00640063";
        unreviewed["variables"][0]["after_sha256"] = sha256(unhex("00640063"));
        rejects([&] { execute_import(unreviewed, workspace / L"unreviewed-byte.json", {read_nv, write_nv}); },
                "Rehashed payload cannot add bytes outside reviewed patches");
        reset_nv();
        import_calls = 0;
        auto restore_error = [&](const std::string& name, const std::string& guid, uint32_t attributes,
                                 const Bytes& data) {
            ++import_calls;
            if (import_calls == 3)
                throw Error("simulated second-store restore failure");
            write_nv(name, guid, attributes, data);
            if (import_calls == 2)
                throw Error("simulated partially accepted second-store write");
        };
        rejects(
            [&] {
                execute_import(multi, workspace / L"multi-restore-failed.json", {read_nv, restore_error});
            },
            "One rollback failure is reported without skipping other targets");
        check(nv["Setup"] == unhex("01640000") && nv["Other"] == unhex("01020904") &&
                  Json::parse(read_file(workspace / L"multi-restore-failed.json"))["restoration"].size() == 2,
              "Recovery attempts earlier targets even after a later target cannot be restored");
        edit_script(original, numeric, 2, "250");
        check(plan_import(capture, numeric)["patches"][0]["after"] == 250,
              "Numeric import resolves 16-bit field");
        auto invalid = workspace / L"invalid.txt";
        edit_script(original, invalid, 2, "251");
        rejects([&] { (void)plan_import(capture, invalid); }, "Reject numeric step violation");
        rejects([&] { edit_script(original, workspace / L"bad-option.txt", 1, "9"); },
                "Reject option outside firmware choices");
        rejects([&] { edit_script(original, workspace / L"overflow.txt", 2, "65536"); },
                "Reject setting overflow");
        rejects([&] { export_capture(capture, original); }, "Reject accidental export overwrite");
        rejects([&] { edit_script(original, original, 1, "0"); }, "Reject edits over original");
        auto corrupted = read_file(capture / L"hii.bin");
        corrupted.back() ^= 1;
        write_file(capture / L"hii.bin", corrupted, true);
        rejects([&] { (void)plan_import(capture, edited); }, "Reject tampered HII capture");
        check(sha256(read_file(original)) == baseline, "Failed operations preserve original");
        auto hexfile = workspace / L"legacy-hex.txt", hexedited = workspace / L"legacy-hex-edited.txt";
        write_file(hexfile, text("HIICrc32= 12345678\nSetup Question = Hex field\nToken = 1\nOffset = "
                                 "0\nWidth = 1\nValue = 85\n"));
        check(inspect_script(hexfile)["questions"][0]["value"] == 133, "AMI bare Value is hexadecimal");
        edit_script(hexfile, hexedited, 1, "128");
        check(inspect_script(hexedited)["questions"][0]["value"] == 128,
              "Editing bare hexadecimal preserves AMI numeric interpretation");
        Json baseline_variable = {{"name", "Setup"},
                                  {"guid", "12345678-1234-5678-9012-123456789012"},
                                  {"data", "010203"},
                                  {"attributes", 7}};
        Bytes fake = {1, 2, 3};
        size_t writes = 0;
        auto reader = [&] {
            auto v = baseline_variable;
            v["data"] = hex(fake);
            return v;
        };
        auto writer = [&](const Bytes& data) {
            ++writes;
            fake = data;
        };
        auto receipt = verify_roundtrip(baseline_variable, {0, 2, 3}, planned, workspace / L"roundtrip.json",
                                        reader, writer);
        check(receipt["restored_verified"] == true && fake == Bytes({1, 2, 3}) && writes == 2,
              "Write validation restores exact baseline after success");
        auto existing_writes = writes;
        rejects(
            [&] {
                verify_roundtrip(baseline_variable, {0, 2, 3}, planned, workspace / L"roundtrip.json", reader,
                                 writer);
            },
            "Existing recovery journal prevents any write");
        check(writes == existing_writes, "Journal rejection occurs before firmware callback");
        size_t reads = 0;
        auto bad_first = [&] {
            auto v = reader();
            if (++reads == 1)
                v["data"] = "000000";
            return v;
        };
        rejects(
            [&] {
                verify_roundtrip(baseline_variable, {0, 2, 3}, planned, workspace / L"readback-failed.json",
                                 bad_first, writer);
            },
            "Failed changed readback triggers restoration");
        check(fake == Bytes({1, 2, 3}) &&
                  Json::parse(read_file(workspace / L"readback-failed.json"))["restored_verified"] == true,
              "Readback failure retains verified restoration evidence");
        size_t calls = 0;
        auto partial_failure = [&](const Bytes& data) {
            writer(data);
            if (++calls == 1)
                throw Error("simulated failure after firmware accepted write");
        };
        rejects(
            [&] {
                verify_roundtrip(baseline_variable, {0, 2, 3}, planned, workspace / L"partial-failed.json",
                                 reader, partial_failure);
            },
            "Failed SetVariable return still triggers restoration");
        check(fake == Bytes({1, 2, 3}) && calls == 2, "Partially accepted write restores baseline");
        calls = 0;
        auto restore_failure = [&](const Bytes& data) {
            if (++calls == 2)
                throw Error("simulated restore failure");
            writer(data);
        };
        rejects(
            [&] {
                verify_roundtrip(baseline_variable, {0, 2, 3}, planned, workspace / L"restore-failed.json",
                                 reader, restore_failure);
            },
            "Restore failure is explicit");
        auto failed = Json::parse(read_file(workspace / L"restore-failed.json"));
        check(failed["stage"] == "restore_failed" && failed["before"]["data"] == "010203" &&
                  failed["restored_verified"] == false,
              "Restore failure keeps usable original recovery bytes");
        rejects(
            [&] {
                verify_roundtrip(baseline_variable, {0, 0, 3}, planned, workspace / L"multiple.json", reader,
                                 writer);
            },
            "Roundtrip refuses multiple changed bytes before writes");
        // Reboot recovery must retain unrelated firmware updates instead of
        // writing the entire stale pre-reboot Setup snapshot.
        Json reboot_plan = {{"format", "luminami-import-plan-v1"},
                            {"numlock_reboot_test",
                             {{"boot_identifier", "12345678-1234-5678-9012-123456789012"},
                              {"baseline", 1},
                              {"expected_after_reboot", 0}}},
                            {"variables", Json::array({{{"name", "Setup"},
                                                        {"guid", "ec87d643-eba4-4bb5-a1e5-3f3e36b20da9"},
                                                        {"attributes", 7},
                                                        {"size", 3},
                                                        {"before_data", "010203"},
                                                        {"after_data", "000203"},
                                                        {"before_sha256", sha256(Bytes{1, 2, 3})},
                                                        {"after_sha256", sha256(Bytes{0, 2, 3})}}})},
                            {"patches", Json::array({{{"name", "Bootup NumLock State"},
                                                      {"variable", "Setup"},
                                                      {"guid", "ec87d643-eba4-4bb5-a1e5-3f3e36b20da9"},
                                                      {"offset", 0},
                                                      {"width", 1},
                                                      {"before", 1},
                                                      {"after", 0}}})}};
        Json staged = {{"format", "luminami-import-journal-v1"},
                       {"stage", "committed"},
                       {"ok", true},
                       {"applied", Json::array({0})},
                       {"attempted", Json::array({0})},
                       {"plan", reboot_plan}};
        Json boot_live = {{"name", "Setup"},
                          {"guid", "ec87d643-eba4-4bb5-a1e5-3f3e36b20da9"},
                          {"attributes", 7},
                          {"data", "000907"}};
        auto restore_plan = prepare_numlock_restore(staged, boot_live);
        check(restore_plan["variables"][0]["after_data"] == "010907",
              "Reboot restore preserves unrelated firmware changes");
        check(restore_plan["variables"][0]["before_data"] == "000907",
              "Reboot restore uses fresh live baseline");
        boot_live["data"] = "010907";
        check(prepare_numlock_restore(staged, boot_live)["variables"].empty(),
              "Already restored NumLock requires no write");
        boot_live["data"] = "020907";
        rejects([&] { prepare_numlock_restore(staged, boot_live); },
                "Unexpected NumLock value prevents restoration write");
        boot_live["data"] = "000907";
        auto tampered_stage = staged;
        tampered_stage["plan"]["variables"][0]["before_sha256"] = "invalid";
        rejects([&] { prepare_numlock_restore(tampered_stage, boot_live); },
                "Corrupt reboot recovery baseline rejected");
        tampered_stage = staged;
        tampered_stage["stage"] = "restore_failed";
        rejects([&] { prepare_numlock_restore(tampered_stage, boot_live); },
                "Unverified staged transaction rejected");
        tampered_stage = staged;
        tampered_stage["plan"]["patches"][0]["name"] = "Another setting";
        rejects([&] { prepare_numlock_restore(tampered_stage, boot_live); },
                "Reboot recovery cannot target another setting");
        tampered_stage = staged;
        tampered_stage["plan"]["numlock_reboot_test"]["boot_identifier"] = "invalid";
        rejects([&] { prepare_numlock_restore(tampered_stage, boot_live); },
                "Invalid boot identity rejected");
        boot_live["attributes"] = 3;
        rejects([&] { prepare_numlock_restore(staged, boot_live); },
                "Unexpected reboot variable attributes rejected");
        check(windows_boot_identifier() == windows_boot_identifier(),
              "Boot identifier remains stable during this boot");
        // Leave a clean capture for CLI tests; earlier rejection tests corrupt their fixtures.
        save_capture(workspace / L"cli-capture");
        std::cout << "PASS: " << checks << " checks; firmware was not accessed.\n";
        std::cout << "Fixture artifacts: " << utf8(workspace.wstring()) << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
