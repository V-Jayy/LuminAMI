#include "core.hpp"
#include "version.hpp"
#include <iostream>
#include <map>
#include <cwctype>

namespace {
using namespace luminami;
const std::string help = std::string("LuminAMI ") + Version + R"( - AMI BIOS settings CLI
Made by Lumin | discord.gg/lumin

SYSTEM INFORMATION
  LuminAMI probe [--output status.json]
  LuminAMI diagnose [--output diagnostics.json]
  LuminAMI list-variables [--output variables.json]
  LuminAMI read-variable --name Setup --guid GUID [--output variable.json]

SETTINGS FILES (offline)
  LuminAMI inspect --script BIOSSettings.txt [--output parsed.json]
  LuminAMI edit --script input.txt --output edited.txt --token 0x2B --value 1
  LuminAMI diff --before original.txt --after edited.txt [--output diff.json]
  LuminAMI inspect-hii --hii hii.bin [--output catalog.json]

EXPORT AND IMPORT
  LuminAMI capture-ami --driver PATH --output NEW-DIR [--report result.json]
  LuminAMI export --capture capture-directory --script settings.txt [--dupes Dupes.txt]
  LuminAMI export --driver PATH --capture NEW-DIR --script settings.txt [--dupes Dupes.txt] [--report PATH]
  LuminAMI import --capture capture-directory --script edited.txt [--output plan.json]
  LuminAMI import --driver PATH --capture DIR --script edited.txt --journal PATH [--report PATH]
  LuminAMI test-import --driver PATH --capture DIR --script edited.txt --journal PATH [--report PATH]
  LuminAMI restore --driver PATH --capture BACKUP-DIR --journal PATH [--report PATH]

HARDWARE VALIDATION (writes firmware)
  LuminAMI test-roundtrip --capture DIR --script numlock-off.txt --driver PATH --journal PATH [--report PATH]
  LuminAMI stage-numlock-test --capture DIR --script numlock-off.txt --driver PATH --journal PATH [--report PATH]
  LuminAMI finish-numlock-test --driver PATH --journal STAGED --restore-journal PATH [--report PATH]
  LuminAMI restore-numlock-test --driver PATH --journal STAGED --restore-journal PATH [--report PATH]

SCEWIN-STYLE ALIASES
  LuminAMI /O /S settings.txt /SD Dupes.txt --capture capture-directory
  LuminAMI /I /S edited.txt --capture capture-directory

Import without --driver only plans changes. Live import needs --driver and a new
--journal path. It validates, writes, reads back, and attempts rollback on failure.
test-import writes and immediately restores. The NumLock validation commands are
for a reviewed one-byte test; stage-numlock-test leaves that change for a reboot.
Password unlock and authenticated-variable writes are unsupported.

Live commands need an administrator terminal and a supported AMI driver.
Offline commands need no elevation. Driver hashes: docs/DRIVERS.md.
Full reference: docs/COMMANDS.md. Run LuminAMI.cmd for the command menu.
)";
} // namespace
int wmain(int argc, wchar_t** argv) {
    std::filesystem::path error_report;
    bool possible_firmware_write = false;
    try {
        using namespace luminami;
        if (argc < 2 || std::wstring(argv[1]) == L"--help" || std::wstring(argv[1]) == L"/?" ||
            std::wstring(argv[1]) == L"help") {
            std::cout << help;
            return 0;
        }
        if (std::wstring(argv[1]) == L"--version") {
            std::cout << "LuminAMI " << Version << "\n";
            return 0;
        }
        std::string command = utf8(argv[1]);
        if (command == "/O" || command == "/o")
            command = "export";
        if (command == "/I" || command == "/i")
            command = "import";
        std::map<std::string, std::string> args;
        for (int i = 2; i < argc; ++i) {
            auto key = utf8(argv[i]);
            if (key == "/S" || key == "/s")
                key = "--script";
            if (key == "/SD" || key == "/sd")
                key = "--dupes";
            if (key == "/CPWD" || key == "/cpwd" || key == "--apply")
                throw Error("Use import --driver --journal for ordinary firmware writes; password unlock is "
                            "unsupported");
            if (key.rfind("--", 0) != 0 || i + 1 >= argc)
                throw Error("Every option requires a value: " + key);
            if (!args.emplace(key, utf8(argv[++i])).second)
                throw Error("Repeated option: " + key);
        }
        auto require = [&](const std::string& key) -> std::string {
            auto it = args.find(key);
            if (it == args.end())
                throw Error("Missing option: " + key);
            auto value = it->second;
            args.erase(it);
            return value;
        };
        auto optional = [&](const std::string& key) -> std::string {
            auto it = args.find(key);
            if (it == args.end())
                return {};
            auto value = it->second;
            args.erase(it);
            return value;
        };
        auto path = [](const std::string& value) { return std::filesystem::path(wide(value)); };
        // Validate all option names before performing any side effect.
        std::map<std::string, std::vector<std::string>> allowed = {
            {"probe", {"--output"}},
            {"diagnose", {"--output"}},
            {"inspect", {"--script", "--output"}},
            {"edit", {"--script", "--output", "--token", "--value"}},
            {"diff", {"--before", "--after", "--output"}},
            {"inspect-hii", {"--hii", "--output"}},
            {"list-variables", {"--output"}},
            {"read-variable", {"--name", "--guid", "--output"}},
            {"capture-ami", {"--driver", "--output", "--report"}},
            {"export", {"--capture", "--script", "--dupes", "--driver", "--report"}},
            {"import", {"--capture", "--script", "--output", "--driver", "--journal", "--report"}},
            {"test-import", {"--capture", "--script", "--driver", "--journal", "--report"}},
            {"restore", {"--capture", "--driver", "--journal", "--report"}},
            {"test-roundtrip", {"--capture", "--script", "--driver", "--journal", "--report"}},
            {"stage-numlock-test", {"--capture", "--script", "--driver", "--journal", "--report"}},
            {"finish-numlock-test", {"--driver", "--journal", "--restore-journal", "--report"}},
            {"restore-numlock-test", {"--driver", "--journal", "--restore-journal", "--report"}}};
        auto choices = allowed.find(command);
        if (choices == allowed.end())
            throw Error("Unknown command: " + command);
        for (const auto& [key, value] : args) {
            (void)value;
            if (std::find(choices->second.begin(), choices->second.end(), key) == choices->second.end())
                throw Error("Unknown option: " + key);
        }
        // Reject report collisions before transport setup or firmware writes.
        if (args.contains("--report")) {
            auto report_path = path(args.at("--report"));
            if (report_path.empty() || std::filesystem::exists(report_path))
                throw Error("Report output already exists or is empty");
            auto identity = [](const std::filesystem::path& p) {
                auto text = std::filesystem::weakly_canonical(std::filesystem::absolute(p)).wstring();
                std::transform(text.begin(), text.end(), text.begin(),
                               [](wchar_t c) { return std::towlower(c); });
                return text;
            };
            auto report_id = identity(report_path);
            std::vector<std::filesystem::path> reserved;
            for (const auto& [key, value] : args) {
                if (key == "--report")
                    continue;
                if (key == "--driver" || key == "--capture" || key == "--script" || key == "--dupes" ||
                    key == "--journal" || key == "--restore-journal" || key == "--output")
                    reserved.push_back(path(value));
            }
            if (args.contains("--capture")) {
                auto capture_path = path(args.at("--capture"));
                for (const auto* name : {"capture.json", "catalog.json", "hii.bin"})
                    reserved.push_back(capture_path / name);
            }
            if (command == "restore" && args.contains("--journal")) {
                auto journal_text = args.at("--journal");
                reserved.push_back(path(journal_text + ".capture"));
                reserved.push_back(path(journal_text + ".settings.txt"));
            }
            for (const auto& destination : reserved) {
                if (!destination.empty() && identity(destination) == report_id)
                    throw Error("Report path collides with an input or transaction output");
            }
        }
        Json result;
        std::string output;
        if (command == "probe") {
            output = optional("--output");
            result = probe_windows();
        } else if (command == "diagnose") {
            output = optional("--output");
            result = diagnose_windows();
        } else if (command == "inspect") {
            output = optional("--output");
            result = inspect_script(path(require("--script")));
        } else if (command == "diff") {
            output = optional("--output");
            auto before = path(require("--before")), after = path(require("--after"));
            result = script_diff(before, after);
        } else if (command == "edit") {
            auto input = path(require("--script")), destination = path(require("--output"));
            auto text = require("--token"), value = require("--value");
            size_t used = 0;
            uint64_t token;
            if (text.empty() || text[0] == '-' || text[0] == '+')
                throw Error("Invalid token");
            try {
                token = std::stoull(text, &used, 0);
            } catch (...) {
                throw Error("Invalid token");
            }
            if (used != text.size())
                throw Error("Invalid token suffix");
            edit_script(input, destination, token, value);
            result = {{"ok", true}, {"output", utf8(destination.wstring())}, {"writes_firmware", false}};
        } else if (command == "inspect-hii") {
            output = optional("--output");
            result = inspect_hii(read_file(path(require("--hii"))));
        } else if (command == "list-variables") {
            output = optional("--output");
            result = enumerate_windows_variables();
        } else if (command == "read-variable") {
            output = optional("--output");
            auto name = require("--name"), guid = require("--guid");
            result = read_windows_variable(name, guid);
        } else if (command == "capture-ami") {
            auto report = optional("--report");
            if (!report.empty())
                error_report = path(report);
            auto driver = path(require("--driver")), destination = path(require("--output"));
            result = capture_ami(driver, destination);
            if (!error_report.empty())
                write_json(error_report, result);
        } else if (command == "export") {
            auto capture = path(require("--capture")), script = path(require("--script"));
            auto dupes = optional("--dupes");
            auto driver = optional("--driver"), report = optional("--report");
            if (driver.empty() && !report.empty())
                throw Error("Export --report requires --driver for a live export");
            if (!report.empty()) {
                error_report = path(report);
                if (std::filesystem::exists(error_report))
                    throw Error("Report output already exists");
            }
            auto duplicate_path = dupes.empty() ? std::filesystem::path{} : path(dupes);
            result = driver.empty() ? export_capture(capture, script, duplicate_path)
                                    : export_ami(path(driver), capture, script, duplicate_path);
            if (!error_report.empty())
                write_json(error_report, result);
        } else if (command == "import" || command == "test-import") {
            output = optional("--output");
            auto capture = path(require("--capture")), script = path(require("--script"));
            auto driver = optional("--driver");
            if (driver.empty()) {
                if (command == "test-import" || args.contains("--journal") || args.contains("--report"))
                    throw Error("Live import requires --driver");
                result = plan_import(capture, script);
            } else {
                if (!output.empty())
                    throw Error("Live import uses --report, not --output");
                auto journal = path(require("--journal"));
                auto report = optional("--report");
                if (!report.empty())
                    error_report = path(report);
                possible_firmware_write = true;
                result = apply_ami(path(driver), capture, script, journal, command == "test-import");
                if (!error_report.empty())
                    write_json(error_report, result);
            }
        } else if (command == "restore") {
            auto driver = path(require("--driver")), capture = path(require("--capture")),
                 journal = path(require("--journal"));
            auto report = optional("--report");
            if (!report.empty())
                error_report = path(report);
            possible_firmware_write = true;
            result = restore_ami(driver, capture, journal);
            if (!error_report.empty())
                write_json(error_report, result);
        } else if (command == "test-roundtrip" || command == "stage-numlock-test") {
            auto report = optional("--report");
            if (!report.empty())
                error_report = path(report);
            auto capture = path(require("--capture")), script = path(require("--script"));
            auto driver = path(require("--driver")), journal = path(require("--journal"));
            possible_firmware_write = true;
            result = test_roundtrip(capture, script, driver, journal, command == "stage-numlock-test");
            if (!error_report.empty())
                write_json(error_report, result);
        } else if (command == "finish-numlock-test" || command == "restore-numlock-test") {
            auto report = optional("--report");
            if (!report.empty())
                error_report = path(report);
            auto driver = path(require("--driver")), journal = path(require("--journal")),
                 restore = path(require("--restore-journal"));
            possible_firmware_write = true;
            result = finish_numlock_reboot(driver, journal, restore, command == "finish-numlock-test");
            if (!error_report.empty())
                write_json(error_report, result);
        }
        if (!output.empty()) {
            write_json(path(output), result);
            Json summary = {{"ok", true},
                            {"output", output},
                            {"writes_firmware", result.value("writes_firmware", false)}};
            if (result.contains("summary"))
                summary["summary"] = result["summary"];
            if (result.contains("patches"))
                summary["changes"] = result["patches"].size();
            if (result.contains("variables") && result["variables"].is_array())
                summary["variables"] = result["variables"].size();
            result = std::move(summary);
        }
        if (output.empty() && command == "inspect") {
            result.erase("questions");
        }
        if (output.empty() && command == "inspect-hii") {
            result.erase("questions");
            result.erase("varstores");
        }
        std::cout << result.dump(2) << "\n";
        return 0;
    } catch (const std::exception& error) {
        auto report = luminami::Json({{"ok", false}, {"error", error.what()}});
        if (possible_firmware_write)
            report["firmware_writes_may_have_occurred"] = true;
        else
            report["writes_firmware"] = false;
        if (!error_report.empty()) {
            try {
                luminami::write_json(error_report, report);
            } catch (...) {
            }
        }
        std::cerr << report.dump() << "\n";
        return 1;
    }
}
