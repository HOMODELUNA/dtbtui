#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <ftxui/screen/string.hpp>

#include "dtb/model.hpp"

namespace ui {

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

// One-line summary reusing the decode layer's lines.
inline std::string property_summary(const dtb::Property& prop) {
    std::string summary;
    for (size_t i = 0; i < prop.lines.size(); ++i) {
        if (i != 0) summary += ", ";
        summary += prop.lines[i];
    }
    return summary;
}

} // namespace ui