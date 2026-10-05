#pragma once
#include <json.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace luminami {
using Json = nlohmann::ordered_json;
using Bytes = std::vector<uint8_t>;
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};
Bytes read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, const Bytes& bytes, bool replace = false);
void write_json(const std::filesystem::path& path, const Json& value, bool replace = false);
std::string hex(const Bytes& bytes);
Bytes unhex(const std::string& value);
uint64_t read_le(const Bytes& data, size_t offset, size_t width);
void put_le(Bytes& data, size_t offset, size_t width, uint64_t value);
uint32_t crc32(const Bytes& bytes);
std::string sha256(const Bytes& bytes);
std::string utf8(const std::wstring& value);
std::wstring wide(const std::string& value);
std::string guid_string(const Bytes& bytes, size_t offset);
Bytes guid_bytes(const std::string& value);
std::string decode_utf16(const Bytes& bytes);
Bytes encode_utf16(const std::string& value, size_t characters);
int64_t signed_value(uint64_t value, size_t width);
Json inspect_script(const std::filesystem::path& path);
Json script_diff(const std::filesystem::path& before, const std::filesystem::path& after);
void edit_script(const std::filesystem::path& input, const std::filesystem::path& output, uint64_t token,
                 const std::string& value);
Json probe_windows();
std::string windows_boot_identifier();
Json read_windows_variable(const std::string& name, const std::string& guid);
Json enumerate_windows_variables();
Json diagnose_windows();

namespace protocol {
constexpr uint32_t Allocate = 0xFA002F20;
constexpr uint32_t Release = 0xFA002F24;
constexpr uint32_t Smi = 0xFA002F1C;
constexpr uint32_t Version = 0xFA002F08;
constexpr uint32_t Wsmt = 0xFA002F34;
constexpr uint32_t MaxBuffer = 0x11000;
constexpr const char* HiiGuid = "1b838190-4625-4ead-abc9-cd5e6af18fe0";
Bytes allocation(uint32_t size);
Bytes smi(uint16_t port, uint8_t command, uint32_t physical);
Bytes get_variable(uint32_t physical, const std::string& name, const std::string& guid, uint32_t capacity);
Bytes set_variable(uint32_t physical, const std::string& name, const std::string& guid, uint32_t attributes,
                   const Bytes& data);
Bytes next_variable(uint32_t physical, const std::string& name, const std::string& guid);
Bytes hii_read(uint32_t physical, uint32_t address, uint32_t size);
Bytes wsmt(uint16_t port);
struct WsmtMapping {
    uint32_t physical;
    uint64_t virtual_address;
    uint64_t context_physical;
    uint64_t context_virtual;
};
WsmtMapping decode_wsmt(const Bytes& packet, uint16_t port, uint64_t expected_context = 0);
Bytes wsmt_context(const Bytes& initial, const Bytes& registers);
} // namespace protocol
Json inspect_hii(const Bytes& bytes);
Json capture_ami(const std::filesystem::path& driver, const std::filesystem::path& output);
Json export_ami(const std::filesystem::path& driver, const std::filesystem::path& capture,
                const std::filesystem::path& script, const std::filesystem::path& duplicates = {});
Json export_live_workflow(const std::filesystem::path& capture, const std::filesystem::path& script,
                          const std::filesystem::path& duplicates, const std::function<Json()>& capture_now);
Json export_capture(const std::filesystem::path& capture, const std::filesystem::path& script,
                    const std::filesystem::path& duplicates = {});
Json plan_import(const std::filesystem::path& capture, const std::filesystem::path& script);
Json apply_ami(const std::filesystem::path& driver, const std::filesystem::path& capture,
               const std::filesystem::path& script, const std::filesystem::path& journal,
               bool restore_after = false);
Json plan_restore(const std::filesystem::path& target_capture, const std::filesystem::path& live_capture,
                  const std::filesystem::path& rebased_script);
Json restore_ami(const std::filesystem::path& driver, const std::filesystem::path& target_capture,
                 const std::filesystem::path& journal);
struct VariableOperations {
    std::function<Json(const std::string&, const std::string&)> read;
    std::function<void(const std::string&, const std::string&, uint32_t, const Bytes&)> write;
};
Json execute_import(const Json& plan, const std::filesystem::path& journal,
                    const VariableOperations& operations, bool restore_after = false);
Json test_roundtrip(const std::filesystem::path& capture, const std::filesystem::path& script,
                    const std::filesystem::path& driver, const std::filesystem::path& journal,
                    bool stage_reboot = false);
Json finish_numlock_reboot(const std::filesystem::path& driver, const std::filesystem::path& journal,
                           const std::filesystem::path& restore_journal, bool require_reboot = true);
Json prepare_numlock_restore(const Json& staged, const Json& live);
Json verify_roundtrip(const Json& before, const Bytes& modified, const Json& plan,
                      const std::filesystem::path& journal, const std::function<Json()>& read,
                      const std::function<void(const Bytes&)>& write);
} // namespace luminami
