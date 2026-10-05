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
} // namespace luminami
