#include "dtb/decode.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "dtb/model.hpp"

namespace dtb {
namespace {

constexpr size_t kBytesPerHexLine = 16;
constexpr size_t kHexLineWidth = kBytesPerHexLine * 3 - 1;

constexpr std::array<std::string_view, 15> kStringListProps = {
    "compatible",         "model",       "status",          "device_type", "bootargs",
    "stdout-path",        "clock-names", "interrupt-names", "reg-names",   "pinctrl-names",
    "power-domain-names", "dma-names",   "reset-names",     "phy-names",   "gpio-line-names",
};

bool is_printable(uint8_t byte) {
    return byte >= 0x20 && byte <= 0x7e;
}

bool is_string_list_prop(std::string_view name) {
    return std::find(kStringListProps.begin(), kStringListProps.end(), name)
        != kStringListProps.end();
}

bool is_printable_string(const std::vector<uint8_t>& raw) {
    if (raw.empty() || raw.back() != '\0') return false;
    bool has_printable = false;
    for (uint8_t byte : raw) {
        if (byte == '\0') continue;
        if (!is_printable(byte)) return false;
        has_printable = true;
    }
    return has_printable;
}

std::vector<std::string> split_strings(const std::vector<uint8_t>& raw) {
    std::vector<std::string> strings;
    std::string current;
    for (uint8_t byte : raw) {
        if (byte == '\0') {
            strings.push_back(current);
            current.clear();
        }
        else { current.push_back(static_cast<char>(byte)); }
    }
    if (!current.empty()) strings.push_back(current);
    return strings;
}

void set_strings(Property& prop, std::vector<std::string> strings, std::string reason) {
    if (strings.size() == 1) { prop.kind = ValueKind::String; }
    else { prop.kind = ValueKind::StringList; }
    prop.kind_reason = std::move(reason);
    prop.lines = std::move(strings);
}

void decode_bytes(Property& prop) {
    prop.kind = ValueKind::Bytes;
    prop.kind_reason = fmt::format("长度 {} 不是 NUL 结尾的可打印字符串", prop.raw.size());
    prop.lines.clear();
    for (size_t offset = 0; offset < prop.raw.size(); offset += kBytesPerHexLine) {
        const size_t count = std::min(kBytesPerHexLine, prop.raw.size() - offset);
        std::string hex;
        std::string ascii;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t byte = prop.raw[offset + i];
            if (i != 0) hex.push_back(' ');
            hex += fmt::format("{:02x}", byte);
            ascii.push_back(is_printable(byte) ? static_cast<char>(byte) : '.');
        }
        hex.append(kHexLineWidth - hex.size(), ' ');
        prop.lines.push_back(fmt::format("{} | {}", hex, ascii));
    }
}

void decode_property(Property& prop) {
    prop.kind = ValueKind::Bytes;
    prop.kind_reason.clear();
    prop.lines.clear();

    if (prop.raw.empty()) {
        prop.kind = ValueKind::Empty;
        prop.kind_reason = "len == 0";
        prop.lines.push_back("(空属性 / boolean)");
        return;
    }

    if (is_string_list_prop(prop.name)) {
        set_strings(prop, split_strings(prop.raw), fmt::format("{} 在字符串列表白名单", prop.name));
        return;
    }

    if (is_printable_string(prop.raw)) {
        set_strings(prop, split_strings(prop.raw), "全部字节可打印且以 0x00 结尾");
        return;
    }

    decode_bytes(prop);
}

} // namespace

std::string_view kind_name(ValueKind kind) {
    switch (kind) {
    case ValueKind::Empty: return "Empty";
    case ValueKind::Bytes: return "Bytes";
    case ValueKind::Cells: return "Cells";
    case ValueKind::Reg: return "Reg";
    case ValueKind::String: return "String";
    case ValueKind::StringList: return "StringList";
    }
    return "Bytes";
}

void decode(Tree& tree) {
    for (Node& node : tree.nodes) {
        for (Property& prop : node.props)
            decode_property(prop);
    }
}

} // namespace dtb