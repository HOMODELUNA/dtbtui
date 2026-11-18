#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <ftxui/screen/color.hpp>
#include <ftxui/screen/string.hpp>

#include "dtb/model.hpp"

namespace ui {

constexpr size_t kRawBytesPerLine = 16;
constexpr size_t kRawHexWidth = kRawBytesPerLine * 3 - 1;

inline std::string fit_line(std::string line, int width) {
    if (width <= 0) return {};
    if (ftxui::string_width(line) <= width) return line;

    std::string fitted;
    int used = 0;
    for (const std::string& glyph : ftxui::Utf8ToGlyphs(line)) {
        const int glyph_width = ftxui::string_width(glyph);
        if (used + glyph_width > width - 1) break;
        fitted += glyph;
        used += glyph_width;
    }
    fitted += "…";
    return fitted;
}

inline bool is_printable_byte(uint8_t byte) {
    return byte >= 0x20 && byte <= 0x7e;
}

inline size_t raw_hex_line_count(size_t size) {
    return size == 0 ? 1 : (size + 15) / 16;
}

inline std::string raw_hex_line(const std::vector<uint8_t>& raw, size_t line_index) {
    if (raw.empty()) return "(空)";

    const size_t offset = line_index * kRawBytesPerLine;
    const size_t count = std::min(kRawBytesPerLine, raw.size() - offset);
    std::string hex;
    std::string ascii;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t byte = raw[offset + i];
        if (i != 0) hex.push_back(' ');
        hex += fmt::format("{:02x}", byte);
        ascii.push_back(is_printable_byte(byte) ? static_cast<char>(byte) : '.');
    }
    hex.append(kRawHexWidth - hex.size(), ' ');
    return fmt::format("{} | {}", hex, ascii);
}

// One-line summary reusing the decode layer's lines.
inline std::string property_summary(const dtb::Property& prop) {
    std::string summary;
    for (size_t i = 0; i < prop.lines.size(); ++i) {
        if (i != 0) summary += ", ";
        summary += prop.lines[i];
    }
    return summary;
}

inline ftxui::Color value_color(const dtb::Property& prop) {
    if (!prop.refs.empty()) return ftxui::Color::Yellow;
    switch (prop.kind) {
    case dtb::ValueKind::String:
    case dtb::ValueKind::StringList: return ftxui::Color::Green;
    case dtb::ValueKind::Cells:
    case dtb::ValueKind::Reg: return ftxui::Color::Cyan;
    case dtb::ValueKind::Bytes: return ftxui::Color::GrayDark;
    case dtb::ValueKind::Empty: return ftxui::Color::GrayDark;
    }
    return ftxui::Color::GrayDark;
}

} // namespace ui