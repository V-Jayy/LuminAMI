#include "core.hpp"
#include <limits>

namespace luminami::protocol {
static uint32_t address(uint32_t physical, size_t offset) {
    if (offset > UINT32_MAX - physical)
        throw Error("Firmware pointer exceeds 32-bit physical address range");
    return physical + static_cast<uint32_t>(offset);
}
Bytes allocation(uint32_t size) {
    if (size < 0x1000 || size > MaxBuffer)
        throw Error("Unsupported AMI allocation size");
    Bytes packet(0x20);
    put_le(packet, 0, 4, size);
    return packet;
}
Bytes smi(uint16_t port, uint8_t command, uint32_t physical) {
    if (!port || !physical)
        throw Error("Invalid SMI port or buffer address");
    Bytes packet(0x26);
    put_le(packet, 0, 4, port);
    put_le(packet, 4, 1, command);
    // F1C embeds seven 32-bit registers at unaligned offset 0x0a.
    // SCEWIN's InvokeSMI puts the physical request address in the second register.
    put_le(packet, 0x0e, 4, physical);
    return packet;
}
Bytes get_variable(uint32_t physical, const std::string& name, const std::string& guid, uint32_t capacity) {
    if (name.empty() || name.find('\0') != std::string::npos || !capacity)
        throw Error("Invalid variable read request");
    auto text = wide(name);
    auto length = (text.size() + 1) * 2;
    if (length + capacity + 0x50 >= MaxBuffer)
        throw Error("Variable exceeds AMI communication buffer");
    Bytes packet(0x50 + length + capacity);
    put_le(packet, 0, 4, 0x100);
    put_le(packet, 4, 4, address(physical, 0x30));
    put_le(packet, 0x18, 4, 1);
    auto vendor = guid_bytes(guid);
    std::copy(vendor.begin(), vendor.end(), packet.begin() + 0x30);
    put_le(packet, 0x40, 4, address(physical, 0x50));
    put_le(packet, 0x48, 4, capacity);
    put_le(packet, 0x4c, 4, address(physical, 0x50 + length));
    for (size_t i = 0; i < text.size(); ++i)
        put_le(packet, 0x50 + i * 2, 2, text[i]);
    return packet;
}
Bytes next_variable(uint32_t physical, const std::string& name, const std::string& guid) {
    auto text = wide(name);
    if (text.size() >= 0x100 || name.find('\0') != std::string::npos)
        throw Error("Variable name exceeds enumeration buffer");
    Bytes packet(0x248);
    put_le(packet, 0, 4, 0x200);
    put_le(packet, 4, 4, address(physical, 0x30));
    put_le(packet, 0x18, 4, 1);
    auto vendor = guid_bytes(guid);
    std::copy(vendor.begin(), vendor.end(), packet.begin() + 0x30);
    put_le(packet, 0x40, 4, address(physical, 0x48));
    put_le(packet, 0x44, 4, 0x200);
    for (size_t i = 0; i < text.size(); ++i)
        put_le(packet, 0x48 + i * 2, 2, text[i]);
    return packet;
}
Bytes set_variable(uint32_t physical, const std::string& name, const std::string& guid, uint32_t attributes,
                   const Bytes& data) {
    // 0x1019b80 in the researched executable uses the same arguments as GetVariable.
    // Empty data means deletion in UEFI and is intentionally unsupported here.
    if (data.empty() || data.size() > MaxBuffer || attributes != 7)
        throw Error("Only existing, ordinary NV/BS/RT setup variables can be written");
    auto packet = get_variable(physical, name, guid, static_cast<uint32_t>(data.size()));
    put_le(packet, 0, 4, 0x300);
    put_le(packet, 0x44, 4, attributes);
    std::copy(data.begin(), data.end(), packet.end() - static_cast<ptrdiff_t>(data.size()));
    return packet;
}
Bytes hii_read(uint32_t physical, uint32_t source, uint32_t size) {
    constexpr uint32_t request_offset = 0xfc4;
    if (!size || size > request_offset || source > UINT32_MAX - size)
        throw Error("Invalid HII chunk");
    Bytes packet(0x1000);
    put_le(packet, request_offset, 4, 0x500);
    put_le(packet, request_offset + 4, 4, address(physical, request_offset + 0x30));
    put_le(packet, request_offset + 0x30, 4, source);
    put_le(packet, request_offset + 0x34, 4, physical);
    put_le(packet, request_offset + 0x38, 4, size);
    return packet;
}
Bytes wsmt(uint16_t port) {
    if (!port)
        throw Error("Invalid WSMT SMI port");
    Bytes packet(0x3e);
    put_le(packet, 0, 2, port);
    put_le(packet, 2, 4, MaxBuffer);
    return packet;
}
WsmtMapping decode_wsmt(const Bytes& packet, uint16_t port, uint64_t expected_context) {
    if (packet.size() != 0x3e || !port || read_le(packet, 0, 2) != port || read_le(packet, 2, 4) != MaxBuffer)
        throw Error("AMI WSMT negotiation returned an invalid packet header");
    auto physical = read_le(packet, 6, 8), virtual_address = read_le(packet, 0x0e, 8),
         context_physical = read_le(packet, 0x16, 8), context_virtual = read_le(packet, 0x1e, 8);
    if (!physical || physical > UINT32_MAX - MaxBuffer || !virtual_address ||
        virtual_address > UINTPTR_MAX - MaxBuffer || !context_physical ||
        context_physical > UINT64_MAX - 0x50 || !context_virtual || context_virtual > UINTPTR_MAX - 0x50 ||
        read_le(packet, 0x26 + 8, 8) != physical)
        throw Error("AMI WSMT negotiation returned invalid or inconsistent fixed-buffer mappings");
    if (expected_context && context_physical != expected_context)
        throw Error("WSMT context does not match the ACPI-published firmware buffer");
    // The last fields of the 24-byte context template are opaque firmware state,
    // not a version number. Let the hash-verified driver manage that context.
    return {static_cast<uint32_t>(physical), virtual_address, context_physical, context_virtual};
}
Bytes wsmt_context(const Bytes& initial, const Bytes& registers) {
    if (initial.size() != 24 || registers.size() != 0x26)
        throw Error("Invalid WSMT context inputs");
    Bytes context(0x50);
    std::copy(initial.begin(), initial.end(), context.begin());
    put_le(context, 0x10, 4, 0xc0000004);
    put_le(context, 0x18, 4, read_le(registers, 4, 1));
    constexpr size_t offsets[] = {0x18, 0x0c, 0x14, 0x10, 4, 0};
    for (size_t i = 0; i < 6; ++i)
        put_le(context, 0x20 + i * 8, 8, read_le(registers, 0x0a + offsets[i], 4));
    return context;
}
} // namespace luminami::protocol
