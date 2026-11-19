#include "ui/detail_view.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "dtb/decode.hpp"
#include "dtb/model.hpp"
#include "ui/state.hpp"
#include "ui/text_util.hpp"

namespace ui {
namespace {

class Pane : public ftxui::Node {
public:
    explicit Pane(State& state)
        : state_(state) {}

    void ComputeRequirement() override {
        requirement_.min_x = 0;
        requirement_.min_y = 0;
        requirement_.flex_grow_x = 1;
        requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = 1;
        requirement_.flex_shrink_y = 1;
    }

    void SetBox(ftxui::Box box) override { box_ = box; }

    void Render(ftxui::Screen& screen) override {
        const int height = box_.y_max - box_.y_min + 1;
        const int width = box_.x_max - box_.x_min + 1;
        if (height <= 0 || width <= 0) return;

        ftxui::Elements rows;
        rows.reserve(static_cast<size_t>(height));

        const dtb::Property* prop = state_.selected_property();
        if (prop == nullptr) { rows.push_back(ftxui::text("(无选中属性)") | ftxui::dim); }
        else {
            const std::vector<std::string> meta = {
                fmt::format("字节长度: {}", prop->raw.size()),
                fmt::format("类型: {}", dtb::kind_name(prop->kind)),
            };
            const size_t decode_count = prop->lines.size();
            const size_t raw_count = raw_hex_line_count(prop->raw.size());
            const size_t total = 3 + decode_count + raw_count + meta.size();
            const bool clipped = total > static_cast<size_t>(height);
            const size_t budget = clipped ? static_cast<size_t>(height) - 1 : total;

            for (size_t i = 0; i < budget; ++i)
                rows.push_back(line_at(*prop, meta, i, width));
            if (clipped) {
                rows.push_back(
                    ftxui::text(fmt::format("… (共 {} 字节)", prop->raw.size())) | ftxui::dim
                );
            }
        }

        ftxui::Element content = ftxui::vbox(std::move(rows));
        content->ComputeRequirement();
        content->SetBox(box_);
        content->Render(screen);
    }

private:
    static ftxui::Element heading(const std::string& text) {
        return ftxui::text(text) | ftxui::bold;
    }

    static ftxui::Element line_at(
        const dtb::Property& prop, const std::vector<std::string>& meta, size_t index, int width
    ) {
        const size_t decode_count = prop.lines.size();
        const size_t raw_count = raw_hex_line_count(prop.raw.size());

        if (index == 0) return heading("解码");

        if (index <= decode_count) {
            const std::string line = fit_line(prop.lines[index - 1], width);
            return ftxui::text(line) | ftxui::color(value_color(prop));
        }

        size_t next = 1 + decode_count;
        if (index == next) return heading("原始");

        next += 1;
        if (index < next + raw_count) {
            const std::string line = fit_line(raw_hex_line(prop.raw, index - next), width);
            return ftxui::text(line) | ftxui::color(ftxui::Color::GrayDark);
        }

        next += raw_count;
        if (index == next) return heading("类型");

        next += 1;
        const size_t meta_index = index - next;
        if (meta_index < meta.size())
            return ftxui::text(fit_line(meta[meta_index], width)) | ftxui::dim;

        return ftxui::text(" ");
    }

    State& state_;
};

} // namespace

ftxui::Element detail_pane(State& state) {
    return std::make_shared<Pane>(state);
}

} // namespace ui