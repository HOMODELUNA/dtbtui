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

bool cells_supported(uint32_t count) {
    return count >= 1 && count <= 2;
}

uint32_t read_cell_count(
    const Tree& tree, int node_index, std::string_view name, uint32_t fallback
) {
    if (node_index < 0) return fallback;
    for (const Property& prop : tree.nodes[node_index].props) {
        if (prop.name != name) continue;
        if (prop.raw.size() < sizeof(uint32_t)) return fallback;
        return read_cell(prop.raw, 0);
    }
    return fallback;
}

uint64_t read_wide_cells(const std::vector<uint8_t>& raw, size_t offset, uint32_t count) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < count; ++i)
        value = (value << 32) | read_cell(raw, offset + i);
    return value;
}

void decode_cells_or_bytes(Property& prop, std::string reason) {
    if (prop.raw.size() % sizeof(uint32_t) == 0) { decode_cells(prop, std::move(reason)); }
    else {
        decode_bytes(prop);
        prop.kind_reason = std::move(reason);
    }
}

void decode_reg(Property& prop, uint32_t address_cells, uint32_t size_cells) {
    const size_t entry_cells = static_cast<size_t>(address_cells) + size_cells;
    const size_t entry_bytes = entry_cells * sizeof(uint32_t);
    if (!cells_supported(address_cells) || !cells_supported(size_cells) || entry_bytes == 0
        || prop.raw.size() % entry_bytes != 0) {
        decode_cells_or_bytes(
            prop, fmt::format(
                      "{} 长度 {} 不是 (#address-cells={} + #size-cells={})*4 的整数倍，"
                      "退化为 u32 单元",
                      prop.name, prop.raw.size(), address_cells, size_cells
                  )
        );
        return;
    }

    const int address_width = static_cast<int>(address_cells) * 8 + 2;
    prop.kind = ValueKind::Reg;
    prop.kind_reason = fmt::format(
        "{} → 父节点 #address-cells={}, #size-cells={}", prop.name, address_cells, size_cells
    );
    prop.lines.clear();
    for (size_t offset = 0; offset < prop.raw.size(); offset += entry_bytes) {
        const uint64_t address = read_wide_cells(prop.raw, offset, address_cells);
        const uint64_t length = read_wide_cells(prop.raw, offset + address_cells, size_cells);
        prop.lines.push_back(
            fmt::format("{:#0{}x}  0x{:x}  ({} B)", address, address_width, length, length)
        );
    }
}

void decode_ranges(
    Property& prop, uint32_t child_address_cells, uint32_t parent_address_cells, uint32_t size_cells
) {
    const size_t entry_cells
        = static_cast<size_t>(child_address_cells) + parent_address_cells + size_cells;
    const size_t entry_bytes = entry_cells * sizeof(uint32_t);
    if (!cells_supported(child_address_cells) || !cells_supported(parent_address_cells)
        || !cells_supported(size_cells) || entry_bytes == 0 || prop.raw.size() % entry_bytes != 0) {
        decode_cells_or_bytes(
            prop,
            fmt::format(
                "{} 长度 {} 不是 (本节点 #address-cells={} + 父节点 "
                "#address-cells={} + #size-cells={})*4 的整数倍，退化为 u32 单元",
                prop.name, prop.raw.size(), child_address_cells, parent_address_cells, size_cells
            )
        );
        return;
    }

    const int child_width = static_cast<int>(child_address_cells) * 8 + 2;
    const int parent_width = static_cast<int>(parent_address_cells) * 8 + 2;
    prop.kind = ValueKind::Reg;
    prop.kind_reason = fmt::format(
        "{} → 本节点 #address-cells={}, #size-cells={}, 父节点 #address-cells={}", prop.name,
        child_address_cells, size_cells, parent_address_cells
    );
    prop.lines.clear();
    for (size_t offset = 0; offset < prop.raw.size(); offset += entry_bytes) {
        const uint64_t child_address = read_wide_cells(prop.raw, offset, child_address_cells);
        const uint64_t parent_address
            = read_wide_cells(prop.raw, offset + child_address_cells, parent_address_cells);
        const uint64_t length = read_wide_cells(
            prop.raw, offset + child_address_cells + parent_address_cells, size_cells
        );
        prop.lines.push_back(fmt::format(
            "{:#0{}x}  {:#0{}x}  0x{:x}  ({} B)", child_address, child_width, parent_address,
            parent_width, length, length
        ));
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

    const int parent = tree.nodes[node_index].parent;
    if (prop.name == "reg") {
        const uint32_t address_cells = read_cell_count(tree, parent, "#address-cells", 2);
        const uint32_t size_cells = read_cell_count(tree, parent, "#size-cells", 2);
        decode_reg(prop, address_cells, size_cells);
        return;
    }

    if (prop.name == "ranges" || prop.name == "dma-ranges") {
        const int current = static_cast<int>(node_index);
        const uint32_t child_address_cells = read_cell_count(tree, current, "#address-cells", 2);
        const uint32_t size_cells = read_cell_count(tree, current, "#size-cells", 2);
        const uint32_t parent_address_cells = read_cell_count(tree, parent, "#address-cells", 2);
        decode_ranges(prop, child_address_cells, parent_address_cells, size_cells);
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