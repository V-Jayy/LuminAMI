#pragma once
#include <json.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace luminami {
using Json = nlohmann::ordered_json;
using Bytes = std::vector<uint8_t>;
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct FirmwareWriteBlocked : Error {
    using Error::Error;
};
// A request the firmware answered with a status code (AMI numbering).
struct FirmwareError : Error {
    uint32_t status;
    FirmwareError(const std::string& message, uint32_t value) : Error(message), status(value) {}
};
// The firmware refused a variable read or write with EFI_SECURITY_VIOLATION
// (AMI status 0x9A). On a board with a BIOS administrator password this is the
// refusal AMI's own tools resolve by submitting that password. It carries
// exactly the message this refusal always had, so output is unchanged for
// anyone who did not supply a password. See docs/PASSWORD.md.
struct SetupPasswordRequired : FirmwareError {
    using FirmwareError::FirmwareError;
};
// Holds a BIOS administrator password. Overwrites its storage before releasing
// it and cannot be copied, so the password spends the least time possible in
// process memory. It is never logged, serialized, journaled or put on a command
// line; docs/PASSWORD.md lists the only channels that fill one.
class SecretBytes {
  public:
    SecretBytes() = default;
    explicit SecretBytes(Bytes value);
    SecretBytes(const SecretBytes&) = delete;
    SecretBytes& operator=(const SecretBytes&) = delete;
    SecretBytes(SecretBytes&& other) noexcept;
    SecretBytes& operator=(SecretBytes&& other) noexcept;
    ~SecretBytes();
    bool empty() const { return bytes_.empty(); }
    size_t size() const { return bytes_.size(); }
    const Bytes& bytes() const { return bytes_; }
    void wipe();

  private:
    Bytes bytes_;
};
// Zeroes memory in a way the optimizer cannot remove.
void secure_zero(void* data, size_t size);
// Takes the text out of `value` (which is wiped and emptied) and returns it as a secret.
SecretBytes secret_from_utf8(std::string& value);
// Longest password LuminAMI will hand to a transport. A sanity bound, not a
// claim about any board: the firmware applies its own, usually shorter, limit.
inline constexpr size_t MaxPasswordBytes = 64;
// The result of one password attempt. Never carries the password; `reason` is
// safe to print and to put in a report.
struct PasswordUnlockOutcome {
    bool submitted = false;   // anything was actually sent to the firmware
    bool accepted = false;
    bool unsupported = false; // this build cannot submit a password; not a verdict on the password
    bool locked_out = false;  // the firmware reported its retry limit
    std::optional<int> remaining_attempts;
    std::string reason;
};
// A password was supplied but did not unlock anything (rejected, unusable, or
// not submittable). `outcome` is safe to report. Never retried automatically.
struct SetupPasswordFailure : Error {
    PasswordUnlockOutcome outcome;
    SetupPasswordFailure(const std::string& message, PasswordUnlockOutcome value)
        : Error(message), outcome(std::move(value)) {}
};
// What a transport must offer to take part: submit the password once and say
// what the firmware answered. A transport that cannot leaves this empty.
using PasswordUnlock = std::function<PasswordUnlockOutcome(const SecretBytes&)>;
// One password, at most one submission, per operation. Absent (empty secret)
// means every guarded call is a plain pass-through and no code below runs.
class PasswordAttempt {
  public:
    // Empty secret = no password supplied. Otherwise it is checked now, before
    // any firmware access; an unusable one throws SetupPasswordFailure and has
    // not been sent anywhere.
    explicit PasswordAttempt(SecretBytes secret = {});
    PasswordAttempt(const PasswordAttempt&) = delete;
    PasswordAttempt& operator=(const PasswordAttempt&) = delete;
    bool supplied() const { return supplied_; }
    bool spent() const { return spent_; }
    // Runs `operation`. If the firmware refuses it with SetupPasswordRequired and
    // a password is waiting, submits it exactly once and runs `operation` exactly
    // once more. Anything else, including a second refusal, propagates.
    template <class Operation>
    auto guard(const PasswordUnlock& unlock, Operation&& operation) -> decltype(operation()) {
        if (!supplied_)
            return operation();
        try {
            return operation();
        } catch (const SetupPasswordRequired& refusal) {
            if (spent_)
                throw;
            submit(unlock, refusal.what());
        }
        return operation();
    }

  private:
    void submit(const PasswordUnlock& unlock, const std::string& refusal);
    SecretBytes secret_;
    bool supplied_ = false;
    bool spent_ = false;
    PasswordUnlockOutcome outcome_;
    friend Json password_report(const PasswordAttempt&);
};
// Safe to print: whether a password was submitted and what the firmware said.
// A null value when none was supplied.
Json password_report(const PasswordAttempt& attempt);
// Throws SetupPasswordFailure (before anything is sent anywhere) if a non-empty
// password is one LuminAMI will not submit. The message never repeats it.
void validate_setup_password(const SecretBytes& secret);
// Whether this build can submit a password to real AMI firmware.
bool password_submission_supported();
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
void write_windows_variable(const std::string& name, const std::string& guid, uint32_t attributes,
                            const Bytes& expected, const Bytes& data);
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
Json capture_ami(const std::filesystem::path& driver, const std::filesystem::path& output,
                 SecretBytes password = {});
Json export_ami(const std::filesystem::path& driver, const std::filesystem::path& capture,
                const std::filesystem::path& script, const std::filesystem::path& duplicates = {},
                SecretBytes password = {});
Json export_live_workflow(const std::filesystem::path& capture, const std::filesystem::path& script,
                          const std::filesystem::path& duplicates, const std::function<Json()>& capture_now);
Json export_capture(const std::filesystem::path& capture, const std::filesystem::path& script,
                    const std::filesystem::path& duplicates = {});
Json plan_import(const std::filesystem::path& capture, const std::filesystem::path& script);
Json apply_ami(const std::filesystem::path& driver, const std::filesystem::path& capture,
               const std::filesystem::path& script, const std::filesystem::path& journal,
               bool restore_after = false, SecretBytes password = {});
Json plan_restore(const std::filesystem::path& target_capture, const std::filesystem::path& live_capture,
                  const std::filesystem::path& rebased_script);
Json restore_ami(const std::filesystem::path& driver, const std::filesystem::path& target_capture,
                 const std::filesystem::path& journal, SecretBytes password = {});
struct VariableOperations {
    std::function<Json(const std::string&, const std::string&)> read;
    std::function<void(const std::string&, const std::string&, uint32_t, const Bytes&)> write;
    // Empty (the default) = this transport cannot submit a password at all.
    PasswordUnlock unlock_setup_password;
};
// The same operations, each guarded by `attempt`. With no password supplied this
// returns `base` unchanged.
VariableOperations with_password_unlock(const VariableOperations& base, PasswordAttempt& attempt);
void check_amd_write_policy(const Json& catalog, const Json& plan, const VariableOperations& operations);
void write_runtime_fallback(const Json& before, const Bytes& data, const VariableOperations& ami,
                            const VariableOperations& runtime);
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
