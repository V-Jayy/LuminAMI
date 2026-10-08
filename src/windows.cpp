#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "core.hpp"
#include "version.hpp"
#include <set>

namespace luminami {
namespace {
struct Privilege {
    HANDLE token = nullptr;
    TOKEN_PRIVILEGES previous{};
    bool adjusted = false;
    Privilege() {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
            throw Error("Cannot open process token");
        TOKEN_PRIVILEGES desired{};
        desired.PrivilegeCount = 1;
        if (!LookupPrivilegeValueW(nullptr, L"SeSystemEnvironmentPrivilege", &desired.Privileges[0].Luid)) {
            CloseHandle(token);
            throw Error("Cannot resolve firmware privilege");
        }
        desired.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        DWORD length = sizeof(previous);
        SetLastError(0);
        if (!AdjustTokenPrivileges(token, FALSE, &desired, sizeof(previous), &previous, &length) ||
            GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
            CloseHandle(token);
            token = nullptr;
            throw Error("Firmware access requires an administrator terminal (SeSystemEnvironmentPrivilege)");
        }
        adjusted = true;
    }
    ~Privilege() {
        if (token) {
            if (adjusted)
                AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr);
            CloseHandle(token);
        }
    }
};
Bytes table(DWORD provider, DWORD id) {
    auto size = GetSystemFirmwareTable(provider, id, nullptr, 0);
    if (!size)
        return {};
    if (size > 16 * 1024 * 1024)
        throw Error("Firmware table exceeds limit");
    Bytes bytes(size);
    if (GetSystemFirmwareTable(provider, id, bytes.data(), size) != size)
        throw Error("Firmware table read failed");
    return bytes;
}
bool elevated() {
    HANDLE token = nullptr;
    TOKEN_ELEVATION value{};
    DWORD length = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    auto result = GetTokenInformation(token, TokenElevation, &value, sizeof(value), &length);
    CloseHandle(token);
    return result && value.TokenIsElevated;
}
} // namespace
std::string windows_boot_identifier() {
    // SystemBootEnvironmentInformation (90), queried dynamically because the
    // native API is not part of the stable Win32 surface. Fail before writes
    // when unavailable, rather than infer a reboot from elapsed time.
    struct BootEnvironment {
        GUID identifier;
        ULONG firmware_type;
        ULONGLONG flags;
    };
    using Query = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    auto module = GetModuleHandleW(L"ntdll.dll");
    auto query =
        reinterpret_cast<Query>(module ? GetProcAddress(module, "NtQuerySystemInformation") : nullptr);
    BootEnvironment boot{};
    ULONG length = 0;
    if (!query || query(90, &boot, sizeof(boot), &length) < 0 || length < sizeof(GUID))
        throw Error("Cannot obtain Windows boot identifier for reboot validation");
    const auto* start = reinterpret_cast<const uint8_t*>(&boot.identifier);
    Bytes id(start, start + sizeof(GUID));
    if (id == Bytes(16, 0))
        throw Error("Windows returned an empty boot identifier");
    return guid_string(id, 0);
}
Json probe_windows() {
    FIRMWARE_TYPE type = FirmwareTypeUnknown;
    GetFirmwareType(&type);
    auto fadt = table(0x41435049, 0x50434146);
    auto wsmt = table(0x41435049, 0x544d5357);
    auto smbios = table(0x52534d42, 0);
    auto ami = table(0x41435049, 0x49464555);
    uint32_t port = 0;
    if (fadt.size() >= 52)
        port = static_cast<uint32_t>(read_le(fadt, 48, 4));
    Json wsmt_info = {{"present", !wsmt.empty()}};
    if (wsmt.size() >= 40)
        wsmt_info["protection_flags"] = read_le(wsmt, 36, 4);
    Json ami_info = {{"present", !ami.empty()}};
    if (ami.size() >= 72) {
        ami_info["guid"] = guid_string(ami, 36);
        ami_info["data_offset"] = read_le(ami, 52, 2);
        ami_info["version"] = read_le(ami, 54, 2);
        ami_info["context_physical"] = read_le(ami, 56, 8);
        ami_info["smi_command"] = read_le(ami, 64, 4);
    }
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    bool existing = false;
    if (scm) {
        SC_HANDLE service = OpenServiceW(scm, L"GENERICDRV", SERVICE_QUERY_STATUS);
        if (service) {
            existing = true;
            CloseServiceHandle(service);
        }
        CloseServiceHandle(scm);
    }
    return {{"name", "LuminAMI"},
            {"version", Version},
            {"elevated", elevated()},
            {"firmware", type == FirmwareTypeUefi   ? "uefi"
                         : type == FirmwareTypeBios ? "legacy"
                                                    : "unknown"},
            {"smi_port", port},
            {"smi_port_valid", port > 0 && port <= 0xffff},
            {"wsmt", wsmt_info},
            {"ami_acpi", ami_info},
            {"smbios_sha256", smbios.empty() ? "" : sha256(smbios)},
            {"genericdrv_service_exists", existing},
            {"writes_firmware", false},
            {"ami_transport", "experimental; compatibility depends on board firmware and driver"}};
}
Json read_windows_variable(const std::string& name, const std::string& guid) {
    if (name.empty() || name.find('\0') != std::string::npos)
        throw Error("Invalid variable name");
    (void)guid_bytes(guid);
    Privilege privilege;
    auto variable = wide(name), vendor = wide("{" + guid_string(guid_bytes(guid), 0) + "}");
    for (DWORD size = 4096; size <= 1024 * 1024; size *= 2) {
        Bytes bytes(size);
        DWORD attributes = 0;
        auto length = GetFirmwareEnvironmentVariableExW(variable.c_str(), vendor.c_str(), bytes.data(), size,
                                                        &attributes);
        if (length) {
            bytes.resize(length);
            return {{"name", name},
                    {"guid", guid_string(guid_bytes(guid), 0)},
                    {"attributes", attributes},
                    {"size", length},
                    {"sha256", sha256(bytes)},
                    {"data", hex(bytes)}};
        }
        auto error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER)
            throw Error("UEFI variable read failed (Windows error " + std::to_string(error) + ")");
    }
    throw Error("UEFI variable exceeds 1 MiB limit");
}
void write_windows_variable(const std::string& name, const std::string& guid, uint32_t attributes,
                            const Bytes& expected, const Bytes& data) {
    // Only replace an existing ordinary variable. No creation, deletion,
    // attribute changes, authenticated writes or guessed Windows-only payloads.
    if (attributes != 7 || expected.empty() || data.size() != expected.size() || data.size() > 1024 * 1024)
        throw Error("Windows UEFI write requires an existing NV/BS/RT variable of unchanged size");
    Privilege privilege;
    const auto current = read_windows_variable(name, guid);
    if (current.at("attributes") != attributes || unhex(current.at("data").get<std::string>()) != expected)
        throw Error("Windows UEFI variable changed before write: " + name);
    auto variable = wide(name), vendor = wide("{" + guid_string(guid_bytes(guid), 0) + "}");
    if (!SetFirmwareEnvironmentVariableExW(variable.c_str(), vendor.c_str(),
                                           const_cast<uint8_t*>(data.data()), static_cast<DWORD>(data.size()),
                                           attributes)) {
        const auto error = GetLastError();
        throw Error("Windows UEFI write rejected for " + name + " (Windows error " + std::to_string(error) +
                    ")");
    }
}

Json enumerate_windows_variables() {
    Privilege privilege;
    using Enumerate = LONG(NTAPI*)(ULONG, void*, ULONG*);
    auto fn = reinterpret_cast<Enumerate>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtEnumerateSystemEnvironmentValuesEx"));
    if (!fn)
        throw Error("Native UEFI enumeration unavailable");
    Bytes bytes(4096);
    ULONG used = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        used = static_cast<ULONG>(bytes.size());
        LONG status = fn(1, bytes.data(), &used);
        if (status == static_cast<LONG>(0xc0000023U)) {
            if (used <= bytes.size() || used > 16 * 1024 * 1024)
                throw Error("Invalid UEFI enumeration size");
            bytes.resize(used);
            continue;
        }
        if (status < 0)
            throw Error(
                "UEFI enumeration failed (NTSTATUS " +
                hex(Bytes(reinterpret_cast<uint8_t*>(&status), reinterpret_cast<uint8_t*>(&status) + 4)) +
                ")");
        if (used > bytes.size())
            throw Error("UEFI enumeration exceeded buffer");
        bytes.resize(used);
        Json list = Json::array();
        size_t offset = 0;
        std::set<std::string> seen;
        while (offset < bytes.size()) {
            if (bytes.size() - offset < 22)
                throw Error("Truncated UEFI enumeration record");
            auto next = read_le(bytes, offset, 4);
            size_t end = next ? offset + static_cast<size_t>(next) : bytes.size();
            if (end > bytes.size() || end <= offset + 20 || next % 2)
                throw Error("Invalid UEFI enumeration chain");
            auto guid = guid_string(bytes, offset + 4);
            std::wstring text;
            bool terminated = false;
            for (size_t i = offset + 20; i + 2 <= end; i += 2) {
                auto c = read_le(bytes, i, 2);
                if (!c) {
                    terminated = true;
                    break;
                }
                text += static_cast<wchar_t>(c);
            }
            if (!terminated || text.empty())
                throw Error("Invalid UEFI enumeration name");
            auto name = utf8(text);
            if (!seen.insert(guid + ":" + name).second)
                throw Error("Repeated UEFI variable");
            list.push_back({{"name", name}, {"guid", guid}});
            if (!next)
                break;
            offset = end;
        }
        return {{"backend", "windows-uefi"},
                {"variables", list},
                {"count", list.size()},
                {"writes_firmware", false}};
    }
    throw Error("UEFI variable list changed too often during enumeration");
}
Json diagnose_windows() {
    Json result = {{"platform", probe_windows()}, {"writes_firmware", false}, {"driver_loaded", false}};
    try {
        result["enumeration"] = enumerate_windows_variables();
    } catch (const Error& e) {
        result["enumeration_error"] = e.what();
    }
    result["setup_variables"] = Json::array();
    if (result.contains("enumeration"))
        for (const auto& item : result["enumeration"]["variables"]) {
            auto name = item["name"].get<std::string>();
            if (name == "Setup" || name == "HiiDB" || name == "CpuSetup" || name == "SaSetup" ||
                name == "PchSetup") {
                try {
                    result["setup_variables"].push_back(
                        read_windows_variable(name, item["guid"].get<std::string>()));
                } catch (const Error& e) {
                    result["setup_variables"].push_back(
                        {{"name", name}, {"guid", item["guid"]}, {"error", e.what()}});
                }
            }
        }
    return result;
}
} // namespace luminami
