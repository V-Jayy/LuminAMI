#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include "core.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace luminami {
Bytes read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw Error("Cannot open file: " + path.string());
    const auto length = std::filesystem::file_size(path);
    if (length > 64 * 1024 * 1024)
        throw Error("File exceeds 64 MiB limit");
    Bytes bytes(static_cast<size_t>(length));
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw Error("Incomplete file read");
    return bytes;
}
void write_file(const std::filesystem::path& path, const Bytes& bytes, bool replace) {
    auto temporary = path;
    GUID id{};
    if (FAILED(CoCreateGuid(&id)))
        throw Error("Could not create output identity");
    temporary += "." +
                 hex(Bytes(reinterpret_cast<uint8_t*>(&id), reinterpret_cast<uint8_t*>(&id) + sizeof(id))) +
                 ".tmp";
    HANDLE file =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw Error("Cannot create output file");
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(),
                            MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) {
        DeleteFileW(temporary.c_str());
        throw Error("Cannot publish output (file may already exist): " + path.string());
    }
}
void write_json(const std::filesystem::path& path, const Json& value, bool replace) {
    auto text = value.dump(2) + "\n";
    write_file(path, Bytes(text.begin(), text.end()), replace);
}
std::string hex(const Bytes& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (auto byte : bytes) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}
Bytes unhex(const std::string& text) {
    if (text.size() % 2)
        throw Error("Odd-length hexadecimal data");
    Bytes bytes;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        throw Error("Invalid hexadecimal data");
    };
    for (size_t i = 0; i < text.size(); i += 2)
        bytes.push_back(static_cast<uint8_t>((digit(text[i]) << 4) | digit(text[i + 1])));
    return bytes;
}
uint64_t read_le(const Bytes& data, size_t offset, size_t width) {
    if (width > 8 || offset > data.size() || width > data.size() - offset)
        throw Error("Truncated binary field");
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i)
        value |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
    return value;
}
void put_le(Bytes& data, size_t offset, size_t width, uint64_t value) {
    if (width > 8 || offset > data.size() || width > data.size() - offset)
        throw Error("Packet field out of bounds");
    if (width < 8 && (value >> (width * 8)))
        throw Error("Packet field overflow");
    for (size_t i = 0; i < width; ++i)
        data[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
uint32_t crc32(const Bytes& bytes) {
    uint32_t result = 0xffffffff;
    for (auto byte : bytes) {
        result ^= byte;
        for (int i = 0; i < 8; ++i)
            result = (result >> 1) ^ ((result & 1) ? 0xedb88320 : 0);
    }
    return result ^ 0xffffffff;
}
std::string sha256(const Bytes& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw Error("SHA256 unavailable");
    Bytes digest(32);
    auto status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                             static_cast<ULONG>(bytes.size()), digest.data(), 32);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0)
        throw Error("SHA256 failed");
    return hex(digest);
}
std::string utf8(const std::wstring& value) {
    if (value.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                nullptr, 0, nullptr, nullptr);
    if (!n)
        throw Error("Invalid Unicode text");
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring wide(const std::string& value) {
    if (value.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                nullptr, 0);
    if (!n)
        throw Error("Invalid UTF-8 text");
    std::wstring out(static_cast<size_t>(n), '\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        out.data(), n);
    return out;
}
std::string guid_string(const Bytes& bytes, size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 16)
        throw Error("Truncated GUID");
    GUID guid{};
    memcpy(&guid, bytes.data() + offset, 16);
    wchar_t text[40]{};
    StringFromGUID2(guid, text, 40);
    auto value = utf8(text);
    value = value.substr(1, value.size() - 2);
    for (auto& c : value)
        if (c >= 'A' && c <= 'F')
            c = static_cast<char>(c - 'A' + 'a');
    return value;
}
Bytes guid_bytes(const std::string& value) {
    if (value.empty())
        throw Error("Empty GUID");
    GUID guid{};
    auto text = wide(value.front() == '{' ? value : "{" + value + "}");
    if (FAILED(CLSIDFromString(text.c_str(), &guid)))
        throw Error("Invalid GUID");
    return Bytes(reinterpret_cast<uint8_t*>(&guid), reinterpret_cast<uint8_t*>(&guid) + 16);
}
// ---- BIOS administrator password handling (docs/PASSWORD.md) ----
SecretBytes::SecretBytes(Bytes value) : bytes_(std::move(value)) {}
SecretBytes::SecretBytes(SecretBytes&& other) noexcept : bytes_(std::move(other.bytes_)) {
    other.bytes_.clear();
}
SecretBytes& SecretBytes::operator=(SecretBytes&& other) noexcept {
    if (this != &other) {
        wipe();
        bytes_ = std::move(other.bytes_);
        other.bytes_.clear();
    }
    return *this;
}
SecretBytes::~SecretBytes() {
    wipe();
}
void SecretBytes::wipe() {
    if (!bytes_.empty())
        secure_zero(bytes_.data(), bytes_.size());
    bytes_.clear();
    bytes_.shrink_to_fit();
}
void secure_zero(void* data, size_t size) {
    if (data && size)
        SecureZeroMemory(data, size);
}
SecretBytes secret_from_utf8(std::string& value) {
    SecretBytes secret(Bytes(value.begin(), value.end()));
    if (!value.empty())
        secure_zero(value.data(), value.size());
    value.clear();
    value.shrink_to_fit();
    return secret;
}
namespace {
// Why a password cannot be submitted, or an empty string when it can. Never
// repeats any part of the password.
std::string unusable_reason(const SecretBytes& secret) {
    if (secret.size() > MaxPasswordBytes)
        return "it is longer than " + std::to_string(MaxPasswordBytes) + " characters";
    for (auto byte : secret.bytes())
        if (byte < 0x20 || byte > 0x7e)
            return "it contains characters other than printable ASCII, which LuminAMI does not submit "
                   "(it avoids spending one of the firmware's limited attempts on text the BIOS setup "
                   "screen cannot normally enter)";
    return {};
}
// Defense in depth: if a transport's text ever contained the password, it is
// replaced before it can reach a report or an error message.
std::string redact(std::string text, const SecretBytes& secret) {
    // Very short passwords are not searched for: they would match ordinary words.
    if (secret.size() < 4 || text.size() < secret.size())
        return text;
    const std::string needle(secret.bytes().begin(), secret.bytes().end());
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at))
        text.replace(at, needle.size(), "[removed]");
    return text;
}
} // namespace
void validate_setup_password(const SecretBytes& secret) {
    if (secret.empty())
        return;
    const auto problem = unusable_reason(secret);
    if (problem.empty())
        return;
    PasswordUnlockOutcome outcome;
    outcome.reason = "The BIOS administrator password was not used: " + problem + ". Nothing was sent to the firmware.";
    throw SetupPasswordFailure(outcome.reason, outcome);
}
PasswordAttempt::PasswordAttempt(SecretBytes secret) : secret_(std::move(secret)) {
    supplied_ = !secret_.empty();
    try {
        validate_setup_password(secret_);
    } catch (...) {
        secret_.wipe();
        spent_ = true; // nothing will be sent
        throw;
    }
}
void PasswordAttempt::submit(const PasswordUnlock& unlock, const std::string& refusal) {
    spent_ = true; // before anything can fail: there is no second attempt
    PasswordUnlockOutcome outcome;
    if (!unlock) {
        outcome.unsupported = true;
        outcome.reason = "this connection to the firmware has no way to submit a password";
    } else {
        try {
            outcome = unlock(secret_);
        } catch (const std::exception& error) {
            outcome = {};
            outcome.submitted = true;
            outcome.reason = std::string("the password submission failed: ") + error.what();
        }
    }
    outcome.reason = redact(outcome.reason, secret_);
    secret_.wipe(); // the password is not needed again, whatever the answer was
    outcome_ = outcome;
    if (outcome.accepted)
        return;
    std::string message;
    if (outcome.unsupported) {
        message = refusal + " A BIOS administrator password was supplied, but this build of LuminAMI "
                            "cannot submit one to firmware yet (" +
                  outcome.reason + "); nothing was sent. See docs/PASSWORD.md.";
    } else {
        message = "The firmware did not accept the BIOS administrator password";
        if (!outcome.reason.empty())
            message += " (" + outcome.reason + ")";
        message += ".";
        if (outcome.remaining_attempts)
            message += " The firmware reports " + std::to_string(*outcome.remaining_attempts) +
                       " attempt(s) remaining before it locks further tries.";
        if (outcome.locked_out)
            message += " The firmware has stopped accepting attempts; restart the computer before trying again.";
        message += " LuminAMI does not retry.";
    }
    throw SetupPasswordFailure(message, outcome);
}
Json password_report(const PasswordAttempt& attempt) {
    if (!attempt.supplied_)
        return nullptr;
    const auto& o = attempt.outcome_;
    Json report = {{"supplied", true}, {"needed", attempt.spent_}};
    report["submitted"] = o.submitted;
    report["accepted"] = o.accepted;
    report["unsupported"] = o.unsupported;
    report["locked_out"] = o.locked_out;
    report["remaining_attempts"] = o.remaining_attempts ? Json(*o.remaining_attempts) : Json(nullptr);
    report["reason"] = o.reason;
    return report;
}
VariableOperations with_password_unlock(const VariableOperations& base, PasswordAttempt& attempt) {
    if (!attempt.supplied())
        return base;
    VariableOperations guarded = base;
    const auto unlock = base.unlock_setup_password;
    if (base.read)
        guarded.read = [read = base.read, unlock, &attempt](const std::string& name, const std::string& guid) {
            return attempt.guard(unlock, [&] { return read(name, guid); });
        };
    if (base.write)
        guarded.write = [write = base.write, unlock, &attempt](const std::string& name, const std::string& guid,
                                                              uint32_t attributes, const Bytes& data) {
            attempt.guard(unlock, [&] { write(name, guid, attributes, data); });
        };
    return guarded;
}
} // namespace luminami
