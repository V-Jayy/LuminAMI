#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "core.hpp"
#include <chrono>
#include <thread>
#include <set>

namespace luminami {
namespace {
struct FirmwareError : Error {
    uint32_t status;
    FirmwareError(const std::string& message, uint32_t value) : Error(message), status(value) {}
};
struct ServiceHandle {
    SC_HANDLE value = nullptr;
    explicit ServiceHandle(SC_HANDLE h) : value(h) {}
    ~ServiceHandle() {
        if (value)
            CloseServiceHandle(value);
    }
    ServiceHandle(const ServiceHandle&) = delete;
};
class AmiSession {
    SC_HANDLE service_ = nullptr;
    HANDLE device_ = INVALID_HANDLE_VALUE;
    uint8_t* memory_ = nullptr;
    uint32_t physical_ = 0;
    uint32_t capacity_ = 0;
    uint16_t port_ = 0;
    bool owns_service_ = false;
    bool wsmt_ = false;
    uint32_t driver_version_ = 0;
    static bool mapped(uint8_t* address, size_t length) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            info.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;
        auto displacement = static_cast<size_t>(address - static_cast<uint8_t*>(info.BaseAddress));
        return displacement <= info.RegionSize && length <= info.RegionSize - displacement;
    }
    void cleanup() noexcept {
        if (device_ != INVALID_HANDLE_VALUE) {
            if (memory_ && !wsmt_) {
                // Release takes the user-mapped virtual address, not the physical address.
                uint64_t address = reinterpret_cast<uint64_t>(memory_);
                DWORD returned = 0;
                DeviceIoControl(device_, protocol::Release, &address, 8, &address, 8, &returned, nullptr);
            }
            // WSMT mappings and fixed-buffer release are owned by the driver
            // and disposed when this session's service is stopped.
            memory_ = nullptr;
            CloseHandle(device_);
            device_ = INVALID_HANDLE_VALUE;
        }
        if (service_) {
            if (owns_service_) {
                SERVICE_STATUS status{};
                ControlService(service_, SERVICE_CONTROL_STOP, &status);
                // Delete only the service this session created; never touch a pre-existing one.
                DeleteService(service_);
            }
            CloseServiceHandle(service_);
            service_ = nullptr;
        }
    }
    void ioctl(uint32_t code, Bytes& packet) {
        DWORD returned = 0;
        if (!DeviceIoControl(device_, code, packet.data(), static_cast<DWORD>(packet.size()), packet.data(),
                             static_cast<DWORD>(packet.size()), &returned, nullptr)) {
            const auto error = GetLastError();
            const char* operation = code == protocol::Wsmt       ? "WSMT negotiation"
                                    : code == protocol::Version  ? "driver version"
                                    : code == protocol::Allocate ? "buffer allocation"
                                    : code == protocol::Smi      ? "SMI invocation"
                                                                 : "unknown operation";
            throw Error(std::string("AMI ") + operation + " failed (IOCTL " + std::to_string(code) +
                        ", Windows error " + std::to_string(error) + ")");
        }
        if (returned != packet.size())
            throw Error("AMI IOCTL returned an unexpected byte count");
    }

  public:
    explicit AmiSession(const std::filesystem::path& driver) {
        try {
            auto status = probe_windows();
            if (!status["elevated"].get<bool>())
                throw Error("AMI capture requires an administrator terminal");
            if (!status["smi_port_valid"].get<bool>())
                throw Error("No valid ACPI SMI port; hardware access refused");
            wsmt_ = (status["wsmt"].value("protection_flags", uint64_t{0}) & 3) != 0;
            // WSMT describes protections, not AMI protocol compatibility. The
            // hash-verified driver discovers/negotiates the firmware interface,
            // including boards whose UEFI ACPI table is not exposed to Windows.
            port_ = static_cast<uint16_t>(status["smi_port"].get<uint32_t>());
            auto bytes = read_file(driver);
            const auto driver_hash = sha256(bytes);
            const bool generic =
                driver_hash == "ffc72f0bde21ba20aa97bee99d9e96870e5aa40cce9884e44c612757f939494f";
            if (!generic && driver_hash != "e7cbfb16261de1c7f009431d374d90e9eb049ba78246e38bc4c8b9e06f324b6f")
                throw Error("Driver does not match either researched AMI driver SHA256");
            if (wsmt_ && !generic)
                throw Error("WSMT protected transport requires the supported amigendrv64.sys");
            ServiceHandle scm(
                OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
            if (!scm.value)
                throw Error("Cannot open Service Control Manager");
            SC_HANDLE previous = OpenServiceW(scm.value, L"GENERICDRV", SERVICE_QUERY_STATUS);
            if (previous) {
                CloseServiceHandle(previous);
                throw Error("GENERICDRV already exists; close other AMI utilities before capture");
            }
            if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST)
                throw Error("Cannot verify GENERICDRV ownership");
            auto absolute = std::filesystem::absolute(driver);
            service_ = CreateServiceW(scm.value, L"GENERICDRV", L"LuminAMI temporary AMI transport",
                                      SERVICE_START | SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS,
                                      SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                                      absolute.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
            if (!service_)
                throw Error("Cannot create AMI driver service (Windows error " +
                            std::to_string(GetLastError()) + ")");
            owns_service_ = true;
            if (!StartServiceW(service_, 0, nullptr))
                throw Error("Windows could not load the AMI driver (error " + std::to_string(GetLastError()) +
                            "); security settings have not been changed");
            for (int attempt = 0; attempt < 20; ++attempt) {
                device_ = CreateFileW(L"\\\\.\\Global\\GENERICDRV", GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (device_ != INVALID_HANDLE_VALUE)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (device_ == INVALID_HANDLE_VALUE)
                throw Error("Cannot open AMI driver device");
            Bytes version(4);
            ioctl(protocol::Version, version);
            driver_version_ = static_cast<uint32_t>(read_le(version, 0, 4));
            if (read_le(version, 0, 2) < 5)
                throw Error("AMI driver version is older than the researched interface");
            capacity_ = protocol::MaxBuffer;
            uint64_t virtual_address = 0, physical = 0;
            if (wsmt_) {
                auto packet = protocol::wsmt(port_);
                ioctl(protocol::Wsmt, packet);
                const auto& acpi = status["ami_acpi"];
                const auto expected_context =
                    acpi.value("guid", std::string{}) == "baedb05d-f2ce-485b-b454-c251870cdefc"
                        ? acpi.value("context_physical", uint64_t{0})
                        : uint64_t{0};
                const auto mapping = protocol::decode_wsmt(packet, port_, expected_context);
                physical = mapping.physical;
                virtual_address = mapping.virtual_address;
                auto mapped_context = reinterpret_cast<uint8_t*>(mapping.context_virtual);
                if (!mapped(mapped_context, 0x50))
                    throw Error("WSMT context is not fully mapped");
                // The supported driver prepares and reads back the invocation
                // context inside its SMI IOCTL. Do not write a guessed layout
                // from userspace; the UEFI table can be absent or unrelated.
            } else {
                auto packet = protocol::allocation(capacity_);
                ioctl(protocol::Allocate, packet);
                virtual_address = read_le(packet, 4, 8);
                physical = read_le(packet, 12, 4);
            }
            if (!virtual_address || !physical || physical > UINT32_MAX - capacity_)
                throw Error("AMI allocation did not return a valid low physical buffer");
            memory_ = reinterpret_cast<uint8_t*>(virtual_address);
            physical_ = static_cast<uint32_t>(physical);
            if (!mapped(memory_, capacity_))
                throw Error("AMI allocation is not fully mapped into this process");
        } catch (...) {
            cleanup();
            throw;
        }
    }
    ~AmiSession() { cleanup(); }
    AmiSession(const AmiSession&) = delete;
    uint32_t physical() const { return physical_; }
    Json transport() const {
        return {{"mode", wsmt_ ? "wsmt-driver-managed" : "unprotected-ami"},
                {"driver_version", driver_version_},
                {"buffer_capacity", capacity_},
                {"negotiated", true}};
    }
    Bytes invoke(Bytes packet, size_t header_offset = 0) {
        if (packet.size() > capacity_ || packet.size() < header_offset + 0x30)
            throw Error("AMI request exceeds mapped buffer");
        std::fill(memory_, memory_ + capacity_, uint8_t{0});
        std::copy(packet.begin(), packet.end(), memory_);
        auto registers = protocol::smi(port_, 0xef, physical_ + static_cast<uint32_t>(header_offset));
        ioctl(protocol::Smi, registers);
        std::copy(memory_, memory_ + packet.size(), packet.begin());
        return packet;
    }
    Json variable(const std::string& name, const std::string& guid) {
        uint32_t capacity = 512;
        for (int attempt = 0; attempt < 3; ++attempt) {
            auto packet = invoke(protocol::get_variable(physical_, name, guid, capacity));
            auto status = (read_le(packet, 0, 4) & 0xff00) >> 8;
            auto needed = read_le(packet, 0x48, 4);
            if (status == 0x85) {
                if (needed <= capacity || needed > protocol::MaxBuffer - 0x100)
                    throw Error("AMI variable returned invalid resize length");
                capacity = static_cast<uint32_t>(needed);
                continue;
            }
            if (status)
                throw FirmwareError("AMI GetVariable failed for " + name + " (firmware status " +
                                        std::to_string(status) + ")",
                                    static_cast<uint32_t>(status));
            size_t offset = 0x50 + (wide(name).size() + 1) * 2;
            if (needed > capacity || needed > packet.size() - offset)
                throw Error("AMI variable response exceeds its buffer");
            Bytes data(packet.begin() + static_cast<ptrdiff_t>(offset),
                       packet.begin() + static_cast<ptrdiff_t>(offset + needed));
            return {{"name", name},      {"guid", guid},        {"attributes", read_le(packet, 0x44, 4)},
                    {"data", hex(data)}, {"size", data.size()}, {"sha256", sha256(data)}};
        }
        throw Error("AMI variable changed size repeatedly");
    }
    void set(const std::string& name, const std::string& guid, uint32_t attributes, const Bytes& data) {
        auto packet = invoke(protocol::set_variable(physical_, name, guid, attributes, data));
        auto status = (read_le(packet, 0, 4) & 0xff00) >> 8;
        if (status)
            throw FirmwareError("AMI SetVariable failed for " + name + " (firmware status " +
                                    std::to_string(status) + ")",
                                static_cast<uint32_t>(status));
    }
    Bytes hii() {
        auto variable_info = variable("HiiDB", protocol::HiiGuid);
        auto descriptor = unhex(variable_info["data"].get<std::string>());
        if (descriptor.size() < 8)
            throw Error("AMI HiiDB descriptor is shorter than 8 bytes");
        auto size = read_le(descriptor, 0, 4), address = read_le(descriptor, 4, 4);
        if (!size || size > 32 * 1024 * 1024 || !address || address > UINT32_MAX - size)
            throw Error("AMI HiiDB descriptor has an invalid range");
        Bytes data;
        data.reserve(static_cast<size_t>(size));
        for (uint32_t offset = 0; offset < size;) {
            auto count = static_cast<uint32_t>(std::min(uint64_t{0xfc4}, size - offset));
            auto packet =
                invoke(protocol::hii_read(physical_, static_cast<uint32_t>(address) + offset, count), 0xfc4);
            auto status = (read_le(packet, 0xfc4, 4) & 0xff00) >> 8;
            if (status)
                throw Error("AMI HII read failed (firmware status " + std::to_string(status) + ")");
            data.insert(data.end(), packet.begin(), packet.begin() + count);
            offset += count;
        }
        return data;
    }
};
} // namespace
Json capture_ami(const std::filesystem::path& driver, const std::filesystem::path& output) {
    if (std::filesystem::exists(output))
        throw Error("Capture directory already exists");
    auto status = probe_windows();
    AmiSession session(driver);
    status["ami_session"] = session.transport();
    auto hii = session.hii();
    std::filesystem::create_directories(output);
    write_file(output / L"hii.bin", hii); // Retain raw evidence even when a later decoder fails.
    auto catalog = inspect_hii(hii);
    write_json(output / L"catalog.json", catalog);
    Json variables = Json::array(), errors = Json::array(), unavailable = Json::array();
    std::set<std::string> seen;
    for (const auto& store : catalog["varstores"])
        if (store.contains("name")) {
            auto name = store["name"].get<std::string>(), guid = store["guid"].get<std::string>();
            if (!seen.insert(guid + ":" + name).second)
                continue;
            try {
                variables.push_back(session.variable(name, guid));
            } catch (const FirmwareError& e) {
                // AMI 0x8e represents EFI_NOT_FOUND. Published HII also contains
                // callback-only/dynamic varstores that legitimately do not exist.
                if (e.status == 0x8e)
                    unavailable.push_back({{"name", name},
                                           {"guid", guid},
                                           {"status", e.status},
                                           {"reason", "not present in NVRAM"}});
                else
                    errors.push_back(
                        {{"name", name}, {"guid", guid}, {"status", e.status}, {"error", e.what()}});
            } catch (const Error& e) {
                errors.push_back({{"name", name}, {"guid", guid}, {"error", e.what()}});
            }
        }
    Json manifest = {{"format", "luminami-capture-v1"},
                     {"platform", status},
                     {"hii_sha256", sha256(hii)},
                     {"variables", variables},
                     {"unavailable_variables", unavailable},
                     {"errors", errors},
                     {"capture_complete", errors.empty()},
                     {"writes_firmware", false},
                     {"hardware_validated", false}};
    write_json(output / L"capture.json", manifest);
    if (!errors.empty())
        throw Error("Capture has " + std::to_string(errors.size()) +
                    " unexpected variable read failures; see capture.json");
    return {{"ok", true},
            {"path", utf8(output.wstring())},
            {"questions", catalog["questions"].size()},
            {"variables", variables.size()},
            {"unavailable_variables", unavailable.size()},
            {"transport", session.transport()},
            {"writes_firmware", false},
            {"validation", "read capture only; write and reboot checks still required"}};
}
Json export_ami(const std::filesystem::path& driver, const std::filesystem::path& capture,
                const std::filesystem::path& script, const std::filesystem::path& duplicates) {
    return export_live_workflow(capture, script, duplicates, [&] { return capture_ami(driver, capture); });
}
Json apply_ami(const std::filesystem::path& driver, const std::filesystem::path& capture,
               const std::filesystem::path& script, const std::filesystem::path& journal,
               bool restore_after) {
    if (std::filesystem::exists(journal))
        throw Error("Import journal already exists");
    auto plan = plan_import(capture, script);
    auto current = probe_windows();
    if (current.at("smbios_sha256").get<std::string>().empty() ||
        current.at("smbios_sha256") != plan.at("platform").at("smbios_sha256"))
        throw Error("Live hardware identity differs from the export");
    AmiSession session(driver);
    if (sha256(session.hii()) != plan.at("hii_sha256").get<std::string>())
        throw Error("Live HII changed since export; read BIOS again before importing");
    VariableOperations operations{
        [&](const std::string& n, const std::string& g) { return session.variable(n, g); },
        [&](const std::string& n, const std::string& g, uint32_t attributes, const Bytes& data) {
            session.set(n, g, attributes, data);
        }};
    auto receipt = execute_import(plan, journal, operations, restore_after);
    receipt["writes_firmware"] = !plan.at("variables").empty();
    receipt["changed_fields"] = plan.at("patches").size();
    receipt["hardware_readback_verified"] = true;
    write_json(journal, receipt, true);
    return {{"ok", true},
            {"writes_firmware", receipt["writes_firmware"]},
            {"changed_fields", receipt["changed_fields"]},
            {"hardware_readback_verified", true},
            {"restored_verified", restore_after},
            {"journal", utf8(journal.wstring())}};
}
Json restore_ami(const std::filesystem::path& driver, const std::filesystem::path& target_capture,
                 const std::filesystem::path& journal) {
    if (std::filesystem::exists(journal))
        throw Error("Restore journal already exists");
    auto current_capture = journal;
    current_capture += L".capture";
    auto rebased_script = journal;
    rebased_script += L".settings.txt";
    capture_ami(driver, current_capture);
    plan_restore(target_capture, current_capture, rebased_script);
    return apply_ami(driver, current_capture, rebased_script, journal);
}
Json test_roundtrip(const std::filesystem::path& capture, const std::filesystem::path& script,
                    const std::filesystem::path& driver, const std::filesystem::path& journal,
                    bool stage_reboot) {
    // First hardware write validation is deliberately restricted to this one
    // reviewed boot preference, then restored within the same driver session.
    if (std::filesystem::exists(journal))
        throw Error("Roundtrip journal already exists");
    auto plan = plan_import(capture, script);
    if (plan["patches"].size() != 1)
        throw Error("Roundtrip requires exactly one reviewed NumLock patch");
    const auto patch = plan["patches"][0];
    const std::string name = "Setup", guid = "ec87d643-eba4-4bb5-a1e5-3f3e36b20da9";
    if (patch["name"] != "Bootup NumLock State" || patch["variable"] != name || patch["guid"] != guid ||
        patch["offset"] != 0 || patch["width"] != 1 || patch["before"] != 1 || patch["after"] != 0)
        throw Error("Roundtrip accepts only the reviewed Bootup NumLock On -> Off test");
    auto current = probe_windows();
    if (stage_reboot) {
        plan["numlock_reboot_test"] = {
            {"boot_identifier", windows_boot_identifier()}, {"baseline", 1}, {"expected_after_reboot", 0}};
    }
    if (current["smbios_sha256"] != plan["platform"]["smbios_sha256"])
        throw Error("Hardware identity differs from the reviewed capture");
    AmiSession session(driver);
    if (sha256(session.hii()) != plan["hii_sha256"].get<std::string>())
        throw Error("Live HII changed since the capture");
    auto manifest = Json::parse(read_file(capture / L"capture.json"));
    Json before;
    for (const auto& v : manifest["variables"])
        if (v["name"] == name && v["guid"] == guid)
            before = v;
    if (before.is_null())
        throw Error("Reviewed setup snapshot missing");
    auto live = session.variable(name, guid);
    if (live["data"] != before["data"] || live["attributes"] != before["attributes"] ||
        live["attributes"] != 7)
        throw Error("Setup changed since capture or has unsupported attributes");
    auto original = unhex(live["data"].get<std::string>()), modified = original;
    if (original.empty() || original[0] != 1)
        throw Error("Live NumLock baseline differs");
    modified[0] = 0;
    if (plan["variables"].size() != 1 || plan["variables"][0]["before_data"] != live["data"] ||
        plan["variables"][0]["after_data"] != hex(modified))
        throw Error("Prepared transaction differs from the reviewed one-byte test");
    VariableOperations operations{
        [&](const std::string& n, const std::string& g) { return session.variable(n, g); },
        [&](const std::string& n, const std::string& g, uint32_t attributes, const Bytes& data) {
            session.set(n, g, attributes, data);
        }};
    auto receipt = execute_import(plan, journal, operations, !stage_reboot);
    if (stage_reboot) {
        return {{"ok", true},
                {"writes_firmware", true},
                {"changed_readback_verified", true},
                {"ready_to_reboot", true},
                {"reboot_validated", false},
                {"journal", utf8(journal.wstring())}};
    }
    auto restored = session.variable(name, guid);
    receipt["complete_baseline_matches"] =
        restored["data"] == live["data"] && restored["attributes"] == live["attributes"];
    write_json(journal, receipt, true);
    if (receipt["complete_baseline_matches"] != true)
        throw Error("Test fields were restored but unrelated live bytes differ from the original baseline; "
                    "see journal");
    return {{"ok", true},
            {"writes_firmware", true},
            {"changed_readback_verified", true},
            {"restored_verified", true},
            {"reboot_validated", false},
            {"journal", utf8(journal.wstring())}};
}
Json finish_numlock_reboot(const std::filesystem::path& driver, const std::filesystem::path& journal,
                           const std::filesystem::path& restore_journal, bool require_reboot) {
    if (std::filesystem::exists(restore_journal))
        throw Error("Restore journal already exists");
    auto staged = Json::parse(read_file(journal));
    // Validate recovery identity and payload before loading the driver.
    const auto& variable = staged.at("plan").at("variables").at(0);
    Json baseline = {{"name", variable.at("name")},
                     {"guid", variable.at("guid")},
                     {"attributes", variable.at("attributes")},
                     {"data", variable.at("before_data")}};
    prepare_numlock_restore(staged, baseline);
    auto boot = windows_boot_identifier();
    bool rebooted =
        boot != staged.at("plan").at("numlock_reboot_test").at("boot_identifier").get<std::string>();
    if (require_reboot && !rebooted)
        throw Error("Reboot has not occurred; use restore-numlock-test to cancel without rebooting");
    if (probe_windows()["smbios_sha256"] != staged.at("plan").at("platform").at("smbios_sha256"))
        throw Error("Hardware identity differs from the staged test");
    AmiSession session(driver);
    auto catalog = inspect_hii(session.hii());
    bool question_matches = false;
    for (const auto& q : catalog.at("questions")) {
        if (q.value("name", std::string{}) != "Bootup NumLock State" || !q.value("writable", false) ||
            q.value("offset", size_t(-1)) != 0 || q.value("width", size_t(0)) != 1 || !q.contains("variable"))
            continue;
        const auto& v = q.at("variable");
        if (v.value("name", std::string{}) == "Setup" &&
            v.value("guid", std::string{}) == "ec87d643-eba4-4bb5-a1e5-3f3e36b20da9" &&
            v.at("size") == variable.at("size"))
            question_matches = true;
    }
    if (!question_matches)
        throw Error("Live HII no longer contains the reviewed writable NumLock field");
    auto live = session.variable("Setup", "ec87d643-eba4-4bb5-a1e5-3f3e36b20da9");
    auto restore = prepare_numlock_restore(staged, live);
    bool persisted = unhex(live.at("data").get<std::string>()).at(0) == 0;
    restore["numlock_reboot_observation"] = {{"boot_identifier", boot},
                                             {"reboot_occurred", rebooted},
                                             {"off_persisted", persisted},
                                             {"live_before_restore", live}};
    VariableOperations operations{
        [&](const std::string& n, const std::string& g) { return session.variable(n, g); },
        [&](const std::string& n, const std::string& g, uint32_t attributes, const Bytes& data) {
            session.set(n, g, attributes, data);
        }};
    auto receipt = execute_import(restore, restore_journal, operations);
    receipt["reboot_validated"] = rebooted && persisted;
    receipt["restored_verified"] = true;
    write_json(restore_journal, receipt, true);
    // Preserve the original stage journal as immutable recovery evidence.
    return {{"ok", true},
            {"writes_firmware", !restore.at("variables").empty()},
            {"reboot_validated", rebooted && persisted},
            {"off_persisted", persisted},
            {"restored_verified", true},
            {"journal", utf8(restore_journal.wstring())}};
}
Json verify_roundtrip(const Json& live, const Bytes& modified, const Json& plan,
                      const std::filesystem::path& journal, const std::function<Json()>& read,
                      const std::function<void(const Bytes&)>& write) {
    const auto original = unhex(live.at("data").get<std::string>());
    if (modified.size() != original.size() || original.empty() || live.at("attributes") != 7)
        throw Error("Roundtrip must retain variable size and ordinary attributes");
    size_t differences = 0;
    for (size_t i = 0; i < original.size(); ++i)
        if (original[i] != modified[i])
            ++differences;
    if (differences != 1)
        throw Error("Roundtrip requires exactly one changed byte");
    Json result = {{"format", "luminami-roundtrip-v1"},
                   {"plan", plan},
                   {"before", live},
                   {"expected_after_sha256", sha256(modified)},
                   {"stage", "prepared"},
                   {"writes_attempted", false},
                   {"changed_readback_verified", false},
                   {"restored_verified", false},
                   {"reboot_validated", false}};
    write_json(journal, result); // Durable restore bytes exist before any firmware write.
    std::string failure;
    result["writes_attempted"] = true;
    result["stage"] = "write_pending";
    write_json(journal, result, true);
    try {
        write(modified);
        auto after = read();
        if (after["data"] != hex(modified) || after["attributes"] != 7)
            throw Error("Changed readback differs from the exact one-byte patch");
        result["changed_readback_verified"] = true;
        result["stage"] = "changed_verified";
        write_json(journal, result, true);
    } catch (const std::exception& e) {
        failure = e.what();
        result["test_error"] = failure;
    }
    // Restore even after an unsuccessful SetVariable return: firmware may have
    // partially accepted a request. A failed journal update cannot skip restore.
    try {
        write(original);
        auto restored = read();
        if (restored["data"] != live["data"] || restored["attributes"] != live["attributes"])
            throw Error("Restored Setup readback does not match baseline");
        result["restored_verified"] = true;
        result["stage"] = "restored";
    } catch (const std::exception& e) {
        result["restore_error"] = e.what();
        result["stage"] = "restore_failed";
        try {
            write_json(journal, result, true);
        } catch (...) {
        }
        throw Error("Roundtrip restore failed; baseline bytes are in the journal: " + std::string(e.what()));
    }
    result["ok"] = failure.empty();
    write_json(journal, result, true);
    if (!failure.empty())
        throw Error("Roundtrip failed and baseline was restored: " + failure);
    return {{"ok", true},
            {"writes_firmware", true},
            {"changed_readback_verified", true},
            {"restored_verified", true},
            {"reboot_validated", false},
            {"journal", utf8(journal.wstring())}};
}
} // namespace luminami
