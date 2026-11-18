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
constexpr size_t kCellsPerLine = 8;

constexpr std::array<std::string_view, 15> kStringListProps = {
    "compatible",         "model",       "status",          "device_type", "bootargs",
    "stdout-path",        "clock-names", "interrupt-names", "reg-names",   "pinctrl-names",
    "power-domain-names", "dma-names",   "reset-names",     "phy-names",   "gpio-line-names",
};

constexpr std::array<std::string_view, 5> kPhandleProps = {
    "phandle", "linux,phandle", "interrupt-parent", "next-level-cache", "iommu-parent",
};

bool is_printable(uint8_t byte) {
    return byte >= 0x20 && byte <= 0x7e;
}

bool is_string_list_prop(std::string_view name) {
    return std::find(kStringListProps.begin(), kStringListProps.end(), name)
        != kStringListProps.end();
}

bool is_phandle_ref_prop(std::string_view name) {
    if (std::find(kPhandleProps.begin(), kPhandleProps.end(), name) != kPhandleProps.end()) {
        return true;
    }
    return name.ends_with("-parent") || name.ends_with("-handle");
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

uint32_t read_cell(const std::vector<uint8_t>& raw, size_t index) {
    const size_t offset = index * sizeof(uint32_t);
    return (static_cast<uint32_t>(raw[offset]) << 24)
         | (static_cast<uint32_t>(raw[offset + 1]) << 16)
         | (static_cast<uint32_t>(raw[offset + 2]) << 8) | static_cast<uint32_t>(raw[offset + 3]);
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

void decode_cells(Property& prop, std::string reason) {
    prop.kind = ValueKind::Cells;
    prop.kind_reason = std::move(reason);
    prop.lines.clear();

    const size_t count = prop.raw.size() / sizeof(uint32_t);
    std::string line;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t value = read_cell(prop.raw, i);
        if (i % kCellsPerLine != 0) line.append("  ");
        line += fmt::format("0x{:08x} ({})", value, value);
        if (i % kCellsPerLine == kCellsPerLine - 1 || i + 1 == count) {
            prop.lines.push_back(line);
            line.clear();
        }
    }
}

void decode_bytes(Property& prop) {
    prop.kind = ValueKind::Bytes;
    prop.kind_reason
        = fmt::format("长度 {} 既不是 NUL 结尾的可打印字符串，也不是 4 的倍数", prop.raw.size());
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

void note_duplicate_phandle(Property& prop, size_t node_index, const Tree& tree) {
    if (prop.name != "phandle" && prop.name != "linux,phandle") return;
    if (prop.raw.size() < sizeof(uint32_t)) return;
    const uint32_t value = read_cell(prop.raw, 0);
    const auto it = tree.phandle_map.find(value);
    if (it != tree.phandle_map.end() && it->second != static_cast<int>(node_index)) {
        prop.kind_reason += fmt::format("；phandle 0x{:08x} 重复，以先出现者为准", value);
    }
}

void decode_property(Property& prop, size_t node_index, const Tree& tree) {
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

    if (is_phandle_ref_prop(prop.name) && prop.raw.size() % sizeof(uint32_t) == 0) {
        decode_cells(prop, fmt::format("{} 是单 phandle 引用属性", prop.name));
        note_duplicate_phandle(prop, node_index, tree);
        return;
    }

    if (is_printable_string(prop.raw)) {
        set_strings(prop, split_strings(prop.raw), "全部字节可打印且以 0x00 结尾");
        return;
    }

    if (prop.raw.size() % sizeof(uint32_t) == 0) {
        decode_cells(prop, fmt::format("长度 {} 是 4 的倍数", prop.raw.size()));
        return;
    }

    decode_bytes(prop);
}

void resolve_refs(Tree& tree) {
    for (Node& node : tree.nodes) {
        for (Property& prop : node.props) {
            prop.refs.clear();
            if (!is_phandle_ref_prop(prop.name)) continue;
            if (prop.raw.empty() || prop.raw.size() % sizeof(uint32_t) != 0) continue;

            const size_t count = prop.raw.size() / sizeof(uint32_t);
            for (size_t i = 0; i < count; ++i) {
                const uint32_t value = read_cell(prop.raw, i);
                int target = -1;
                const auto it = tree.phandle_map.find(value);
                if (it != tree.phandle_map.end()) target = it->second;
                prop.refs.push_back(Ref{i, target});

                const std::string suffix = target >= 0
                                             ? fmt::format(" → {}", tree.nodes[target].path)
                                             : fmt::format(" → <未解析 0x{:08x}>", value);
                const size_t line_index = i / kCellsPerLine;
                if (line_index < prop.lines.size()) { prop.lines[line_index] += suffix; }
                else { prop.lines.push_back(suffix); }
            }
        }
    }
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
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        for (Property& prop : tree.nodes[i].props)
            decode_property(prop, i, tree);
    }
    resolve_refs(tree);
}

} // namespace dtb