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

// One colour per device class.  The type panel and the tree use the same
// mapping, so a coloured type row matches the coloured device rows.
inline ftxui::Color device_color(dtb::DeviceKind kind) {
    switch (kind) {
    case dtb::DeviceKind::Cpu: return ftxui::Color::BlueLight;
    case dtb::DeviceKind::Bus: return ftxui::Color::GrayLight;
    case dtb::DeviceKind::Memory: return ftxui::Color::GreenLight;
    case dtb::DeviceKind::Interrupt: return ftxui::Color::YellowLight;
    case dtb::DeviceKind::Iommu: return ftxui::Color::MagentaLight;
    case dtb::DeviceKind::Clock: return ftxui::Color::CyanLight;
    case dtb::DeviceKind::Power: return ftxui::Color::RedLight;
    case dtb::DeviceKind::Pinctrl: return ftxui::Color::Green;
    case dtb::DeviceKind::Storage: return ftxui::Color::Blue;
    case dtb::DeviceKind::Serial: return ftxui::Color::Cyan;
    case dtb::DeviceKind::I2c: return ftxui::Color::Magenta;
    case dtb::DeviceKind::Spi: return ftxui::Color::Yellow;
    case dtb::DeviceKind::Usb: return ftxui::Color::Red;
    case dtb::DeviceKind::Network: return ftxui::Color::GreenLight;
    case dtb::DeviceKind::Wireless: return ftxui::Color::Magenta;
    case dtb::DeviceKind::Display: return ftxui::Color::Cyan;
    case dtb::DeviceKind::Panel: return ftxui::Color::YellowLight;
    case dtb::DeviceKind::Gpu: return ftxui::Color::BlueLight;
    case dtb::DeviceKind::Video: return ftxui::Color::RedLight;
    case dtb::DeviceKind::Camera: return ftxui::Color::MagentaLight;
    case dtb::DeviceKind::Audio: return ftxui::Color::Green;
    case dtb::DeviceKind::Pwm: return ftxui::Color::Yellow;
    case dtb::DeviceKind::Adc: return ftxui::Color::Cyan;
    case dtb::DeviceKind::Thermal: return ftxui::Color::Red;
    case dtb::DeviceKind::Timer: return ftxui::Color::Blue;
    case dtb::DeviceKind::Dma: return ftxui::Color::MagentaLight;
    case dtb::DeviceKind::Crypto: return ftxui::Color::GrayLight;
    case dtb::DeviceKind::Led: return ftxui::Color::YellowLight;
    case dtb::DeviceKind::Firmware: return ftxui::Color::GrayDark;
    case dtb::DeviceKind::Structural: return ftxui::Color::Default;
    case dtb::DeviceKind::Other: return ftxui::Color::White;
    }
    return ftxui::Color::Default;
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