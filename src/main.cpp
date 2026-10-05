#include "core.hpp"
#include "drivers.hpp"
#include "version.hpp"
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>
#include <iostream>
#include <map>
#include <cwctype>

namespace {
using namespace luminami;
std::string transaction_id() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id)))
        throw Error("Could not create an import transaction ID");
    wchar_t text[40]{};
    StringFromGUID2(id, text, 40);
    return utf8(text).substr(1, 36);
}
const std::string quick_help = std::string("LuminAMI ") + Version + R"(
Made by Jayy and Billz | discord.gg/lumin

  LuminAMI -e BIOSSettings.txt       Export settings and a matching .capture folder
  LuminAMI -i BIOSSettings.txt       Apply edited settings; save a unique journal
  LuminAMI -i BIOSSettings.txt --plan   Check changes without writing
  LuminAMI install                  Install/update from GitHub and add to user PATH
  LuminAMI install-drivers          Extract the provided AMI drivers offline
  LuminAMI use-driver --driver PATH Save a custom supported driver path
  LuminAMI driver-status            Show the saved driver
  LuminAMI -h                       Show this help (also: LuminAMI help)
  LuminAMI ami help                 Advanced AMI command reference

Export/import options: --capture DIR, --driver PATH, --install-drivers,
--non-interactive. A saved driver is reused from any directory.
-p <password> is reserved for the existing BIOS password; authentication is
currently unsupported and returns an error without logging the password.
Live export/import need an administrator terminal. Import writes firmware.
Keep the .capture folder with its settings file. Existing outputs are preserved.
)";
Json launch_install(const std::filesystem::path& directory, bool no_path) {
    auto literal = [](std::wstring text) {
        size_t pos = 0;
        while ((pos = text.find(L'\'', pos)) != std::wstring::npos) {
            text.insert(pos, 1, L'\'');
            pos += 2;
        }
        return L"'" + text + L"'";
    };
    auto log = driver_state_directory() / L"install.log";
    std::filesystem::create_directories(log.parent_path());
    auto script =
        L"$ErrorActionPreference='Stop'; try { & ([scriptblock]::Create((Invoke-RestMethod "
        L"'https://raw.githubusercontent.com/V-Jayy/LuminAMI/main/install.ps1'))) -WaitForProcess " +
        std::to_wstring(GetCurrentProcessId()) + L" -LogPath " + literal(log.wstring());
    if (!directory.empty())
        script += L" -Directory " + literal(std::filesystem::absolute(directory).wstring());
    if (no_path)
        script += L" -NoPath";
    script += L" } catch { $_ | Out-File -LiteralPath " + literal(log.wstring()) + L" -Append; exit 1 }";
    const auto* bytes = reinterpret_cast<const uint8_t*>(script.data());
    const size_t size = script.size() * sizeof(wchar_t);
    constexpr char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    for (size_t i = 0; i < size; i += 3) {
        uint32_t value = uint32_t(bytes[i]) << 16;
        if (i + 1 < size)
            value |= uint32_t(bytes[i + 1]) << 8;
        if (i + 2 < size)
            value |= bytes[i + 2];
        encoded += digits[(value >> 18) & 63];
        encoded += digits[(value >> 12) & 63];
        encoded += i + 1 < size ? digits[(value >> 6) & 63] : '=';
        encoded += i + 2 < size ? digits[value & 63] : '=';
    }
    wchar_t windows[MAX_PATH]{};
    if (!GetWindowsDirectoryW(windows, MAX_PATH))
        throw Error("Could not locate Windows PowerShell");
    auto exe = std::filesystem::path(windows) / L"System32/WindowsPowerShell/v1.0/powershell.exe";
    auto line =
        L"\"" + exe.wstring() + L"\" -NoProfile -ExecutionPolicy Bypass -EncodedCommand " + wide(encoded);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process))
        throw Error("Could not start the GitHub installer");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {{"ok", true},
            {"started", true},
            {"completed", false},
            {"process_id", process.dwProcessId},
            {"log", utf8(log.wstring())},
            {"writes_firmware", false}};
}
std::wstring read_console_line() {
    std::wstring text(32768, L'\0');
    DWORD count = 0;
    if (!ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &count,
                      nullptr))
        throw Error("Could not read driver selection; use --driver or --install-drivers for headless setup");
    text.resize(count);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n'))
        text.pop_back();
    return text;
}
const std::string help = std::string("LuminAMI ") + Version + R"( - AMI BIOS settings CLI
Made by Jayy and Billz | discord.gg/lumin

DRIVER SETUP (offline, no driver is loaded)
  LuminAMI install-drivers [--directory DIR]
  LuminAMI use-driver --driver PATH
  LuminAMI driver-status

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
  LuminAMI capture-ami --output NEW-DIR [--driver PATH] [--report result.json]
  LuminAMI export --capture capture-directory --script settings.txt [--dupes Dupes.txt]
  LuminAMI export --capture NEW-DIR --script settings.txt [--driver PATH] [--dupes Dupes.txt] [--report PATH]
  LuminAMI import --capture capture-directory --script edited.txt [--output plan.json]
  LuminAMI import --capture DIR --script edited.txt --journal PATH [--driver PATH] [--report PATH]
  LuminAMI test-import --capture DIR --script edited.txt --journal PATH [--driver PATH] [--report PATH]
  LuminAMI restore --capture BACKUP-DIR --journal PATH [--driver PATH] [--report PATH]

HARDWARE VALIDATION (writes firmware)
  LuminAMI test-roundtrip --capture DIR --script numlock-off.txt --driver PATH --journal PATH [--report PATH]
  LuminAMI stage-numlock-test --capture DIR --script numlock-off.txt --driver PATH --journal PATH [--report PATH]
  LuminAMI finish-numlock-test --driver PATH --journal STAGED --restore-journal PATH [--report PATH]
  LuminAMI restore-numlock-test --driver PATH --journal STAGED --restore-journal PATH [--report PATH]

SCEWIN-STYLE ALIASES
  LuminAMI /O /S settings.txt /SD Dupes.txt --capture capture-directory
  LuminAMI /I /S edited.txt --capture capture-directory

Import without --driver or --journal only plans changes. A new --journal path
selects live import with the saved driver. It validates, writes, reads back, and
attempts rollback on failure. An explicit --driver takes priority and is saved.
test-import writes and immediately restores. The NumLock validation commands are
for a reviewed one-byte test; stage-numlock-test leaves that change for a reboot.
Password unlock and authenticated-variable writes are unsupported.

Export to a new capture uses the saved driver, or offers provided/custom drivers
in an interactive terminal. --install-drivers extracts provided drivers without
prompting. --non-interactive refuses prompts. Both work with live commands.
Existing captures can still be exported offline without a driver.

Live commands need an administrator terminal. Installing drivers only extracts
and verifies files; it does not load them. Driver details: docs/DRIVERS.md.
Full reference: docs/COMMANDS.md. Run LuminAMI.cmd for the command menu.
)";
} // namespace
int wmain(int argc, wchar_t** argv) {
    std::filesystem::path error_report;
    bool possible_firmware_write = false;
    try {
        using namespace luminami;
        if (argc < 2 || std::wstring(argv[1]) == L"--help" || std::wstring(argv[1]) == L"/?" ||
            std::wstring(argv[1]) == L"-h" || std::wstring(argv[1]) == L"help") {
            std::cout << quick_help;
            return 0;
        }
        if (std::wstring(argv[1]) == L"--version") {
            std::cout << "LuminAMI " << Version << "\n";
            return 0;
        }
        if (std::wstring(argv[1]) == L"ami") {
            --argc;
            ++argv;
            if (argc < 2 || std::wstring(argv[1]) == L"help" || std::wstring(argv[1]) == L"-h" ||
                std::wstring(argv[1]) == L"--help") {
                std::cout << help;
                return 0;
            }
        }
        std::string command = utf8(argv[1]);
        const bool short_export = command == "-e", short_import = command == "-i";
        const bool short_command = short_export || short_import;
        if (short_command)
            command = short_export ? "export" : "import";
        if (command == "/O" || command == "/o")
            command = "export";
        if (command == "/I" || command == "/i")
            command = "import";
        std::map<std::string, std::string> args;
        if (short_command) {
            if (argc < 3 || std::wstring(argv[2]).empty() || argv[2][0] == L'-')
                throw Error("Use LuminAMI -e <path> or LuminAMI -i <path>");
            args.emplace("--script", utf8(argv[2]));
        }
        for (int i = short_command ? 3 : 2; i < argc; ++i) {
            auto key = utf8(argv[i]);
            if (key == "-p")
                throw Error(
                    "BIOS password authentication (-p <password>) is not supported by this transport");
            if (key == "--plan" && short_import) {
                if (!args.emplace(key, "true").second)
                    throw Error("Repeated option: --plan");
                continue;
            }
            if (key == "/S" || key == "/s")
                key = "--script";
            if (key == "/SD" || key == "/sd")
                key = "--dupes";
            if (key == "/CPWD" || key == "/cpwd" || key == "--apply")
                throw Error("Use import --driver --journal for ordinary firmware writes; password unlock is "
                            "unsupported");
            if (key == "--non-interactive" || key == "--install-drivers" || key == "--no-path") {
                if (!args.emplace(key, "true").second)
                    throw Error("Repeated option: " + key);
                continue;
            }
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
            {"install", {"--directory", "--no-path"}},
            {"install-drivers", {"--directory", "--non-interactive"}},
            {"use-driver", {"--driver"}},
            {"driver-status", {}},
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
        for (const auto* live :
             {"capture-ami", "export", "import", "test-import", "restore", "test-roundtrip",
              "stage-numlock-test", "finish-numlock-test", "restore-numlock-test"}) {
            allowed.at(live).push_back("--install-drivers");
            allowed.at(live).push_back("--non-interactive");
        }
        auto choices = allowed.find(command);
        if (choices == allowed.end())
            throw Error("Unknown command: " + command);
        if (short_import)
            choices->second.push_back("--plan");
        for (const auto& [key, value] : args) {
            (void)value;
            if (std::find(choices->second.begin(), choices->second.end(), key) == choices->second.end())
                throw Error("Unknown option: " + key);
        }
        if (short_command) {
            const auto script = args.at("--script");
            if (!args.contains("--capture"))
                args.emplace("--capture", script + ".capture");
            if (short_export) {
                if (std::filesystem::exists(path(script)))
                    throw Error("Export output already exists");
                if (!args.contains("--dupes"))
                    args.emplace("--dupes", script + ".dupes.txt");
                if (std::filesystem::exists(path(args.at("--dupes"))))
                    throw Error("Duplicate output already exists");
            } else {
                // Validate the complete offline plan before selecting/loading a driver.
                plan_import(path(args.at("--capture")), path(script));
                if (!optional("--plan").empty()) {
                    if (args.contains("--driver") || args.contains("--install-drivers") ||
                        args.contains("--journal") || args.contains("--report"))
                        throw Error("--plan cannot be combined with live import options");
                } else {
                    if (!args.contains("--journal"))
                        args.emplace("--journal", script + ".import-" + transaction_id() + ".json");
                    if (!args.contains("--report"))
                        args.emplace("--report", args.at("--journal") + ".result.json");
                }
            }
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
        auto driver_for_live = [&]() {
            auto explicit_path = args.contains("--driver");
            auto supplied = optional("--driver");
            bool install = !optional("--install-drivers").empty();
            bool headless = !optional("--non-interactive").empty();
            if (explicit_path && supplied.empty())
                throw Error("Driver path is empty");
            if (explicit_path && install)
                throw Error("Use either --driver or --install-drivers");
            std::filesystem::path selected;
            if (explicit_path)
                selected = resolve_ami_driver(path(supplied));
            else if (install)
                selected = path(install_ami_drivers().at("driver").get<std::string>());
            else {
                auto status = ami_driver_status();
                DWORD mode = 0;
                if (!status.at("configured").get<bool>() && !headless && command == "export" &&
                    GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) {
                    std::cerr << "No verified AMI driver is configured.\n"
                                 "1  Install provided drivers (offline)\n2  Use a custom driver path\n0  "
                                 "Cancel\nDriver choice: ";
                    auto choice = read_console_line();
                    if (choice == L"1")
                        selected = path(install_ami_drivers().at("driver").get<std::string>());
                    else if (choice == L"2") {
                        std::cerr << "AMI driver path: ";
                        auto custom = read_console_line();
                        if (custom.size() >= 2 && custom.front() == L'"' && custom.back() == L'"')
                            custom = custom.substr(1, custom.size() - 2);
                        if (custom.empty())
                            throw Error("Driver selection cancelled");
                        selected = resolve_ami_driver(std::filesystem::path(custom));
                    } else
                        throw Error("Driver selection cancelled");
                } else
                    selected = resolve_ami_driver();
            }
            if (!error_report.empty() && std::filesystem::exists(error_report))
                throw Error("Report output already exists or collides with the selected driver");
            return selected;
        };
        Json result;
        std::string output;
        if (command == "install") {
            auto directory = path(optional("--directory"));
            result = launch_install(directory, !optional("--no-path").empty());
        } else if (command == "install-drivers") {
            optional("--non-interactive");
            result = install_ami_drivers(path(optional("--directory")));
        } else if (command == "use-driver") {
            auto driver = require("--driver");
            if (driver.empty())
                throw Error("Driver path is empty");
            result = remember_ami_driver(path(driver));
        } else if (command == "driver-status") {
            result = ami_driver_status();
        } else if (command == "probe") {
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
            auto destination = path(require("--output"));
            auto driver = driver_for_live();
            result = capture_ami(driver, destination);
            if (!error_report.empty())
                write_json(error_report, result);
        } else if (command == "export") {
            auto capture = path(require("--capture")), script = path(require("--script"));
            auto dupes = optional("--dupes");
            bool live = args.contains("--driver") || args.contains("--install-drivers") ||
                        !std::filesystem::exists(capture);
            auto report = optional("--report");
            if (!live && !report.empty())
                throw Error("Offline export does not accept --report");
            if (!report.empty()) {
                error_report = path(report);
                if (std::filesystem::exists(error_report))
                    throw Error("Report output already exists");
            }
            auto duplicate_path = dupes.empty() ? std::filesystem::path{} : path(dupes);
            if (live)
                result = export_ami(driver_for_live(), capture, script, duplicate_path);
            else {
                optional("--non-interactive");
                result = export_capture(capture, script, duplicate_path);
            }
            if (!error_report.empty())
                write_json(error_report, result);
        } else if (command == "import" || command == "test-import") {
            output = optional("--output");
            auto capture = path(require("--capture")), script = path(require("--script"));
            bool live = command == "test-import" || args.contains("--driver") || args.contains("--journal");
            if (!live) {
                if (args.contains("--install-drivers") || args.contains("--report"))
                    throw Error("Live import requires --journal");
                optional("--non-interactive");
                result = plan_import(capture, script);
            } else {
                if (!output.empty())
                    throw Error("Live import uses --report, not --output");
                auto journal = path(require("--journal"));
                auto report = optional("--report");
                if (!report.empty())
                    error_report = path(report);
                auto driver = driver_for_live();
                possible_firmware_write = true;
                result = apply_ami(driver, capture, script, journal, command == "test-import");
                if (!error_report.empty())
                    write_json(error_report, result);
            }
        } else if (command == "restore") {
            auto capture = path(require("--capture")), journal = path(require("--journal"));
            auto report = optional("--report");
            if (!report.empty())
                error_report = path(report);
            auto driver = driver_for_live();
            possible_firmware_write = true;
            result = restore_ami(driver, capture, journal);
            if (!error_report.empty())
                write_json(error_report, result);
        } else if (command == "test-roundtrip" || command == "stage-numlock-test") {
            auto report = optional("--report");
            if (!report.empty())
                error_report = path(report);
            auto capture = path(require("--capture")), script = path(require("--script"));
            auto journal = path(require("--journal"));
            auto driver = driver_for_live();
            possible_firmware_write = true;
            result = test_roundtrip(capture, script, driver, journal, command == "stage-numlock-test");
            if (!error_report.empty())
                write_json(error_report, result);
        } else if (command == "finish-numlock-test" || command == "restore-numlock-test") {
            auto report = optional("--report");
            if (!report.empty())
                error_report = path(report);
            auto journal = path(require("--journal")), restore = path(require("--restore-journal"));
            auto driver = driver_for_live();
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
