#include "core.hpp"
#include <bit>

namespace luminami {
std::string decode_utf16(const Bytes& bytes) {
    if (bytes.empty() || bytes.size() % 2)
        throw Error("Invalid UTF16 field width");
    std::wstring value;
    for (size_t i = 0; i < bytes.size(); i += 2) {
        auto unit = read_le(bytes, i, 2);
        if (!unit)
            break;
        value += static_cast<wchar_t>(unit);
    }
    return utf8(value);
}
Bytes encode_utf16(const std::string& value, size_t characters) {
    if (!characters || characters > 255 || value.find('\0') != std::string::npos)
        throw Error("Invalid UTF16 string request");
    auto text = wide(value);
    if (text.size() > characters)
        throw Error("String exceeds its IFR maximum character count");
    Bytes result(characters * 2);
    for (size_t i = 0; i < text.size(); ++i)
        put_le(result, i * 2, 2, text[i]);
    return result;
}
int64_t signed_value(uint64_t value, size_t width) {
    if (width != 1 && width != 2 && width != 4 && width != 8)
        throw Error("Invalid signed scalar width");
    if (width < 8) {
        auto bits = width * 8, mask = (uint64_t{1} << bits) - 1;
        if (value > mask)
            throw Error("Signed scalar raw value exceeds width");
        if (value & (uint64_t{1} << (bits - 1)))
            value |= ~mask;
    }
    return std::bit_cast<int64_t>(value);
}
} // namespace luminami
