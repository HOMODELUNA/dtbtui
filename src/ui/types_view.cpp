#include "ui/types_view.hpp"

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
#include <ftxui/screen/string.hpp>

#include "dtb/classify.hpp"
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

    void SetBox(ftxui::Box box) override {
        box_ = box;
        state_.set_types_box(PaneBox{box.x_min, box.y_min, box.x_max, box.y_max});
    }

    void Render(ftxui::Screen& screen) override {
        const int height = box_.y_max - box_.y_min + 1;
        const int width = box_.x_max - box_.x_min + 1;
        if (height <= 0 || width <= 0) return;

        const std::vector<TypeEntry>& types = state_.types();
        ftxui::Elements rows;
        rows.reserve(static_cast<size_t>(height));

        if (types.empty()) { rows.push_back(ftxui::text("(无类型)") | ftxui::dim); }
        else {
            for (int row = 0; row < height; ++row) {
                const int index = state_.types_scroll() + row;
                if (index >= static_cast<int>(types.size())) {
                    rows.push_back(ftxui::text(" "));
                    continue;
                }
                rows.push_back(item_row(types[static_cast<size_t>(index)], index, width));
            }
        }

        ftxui::Element content = ftxui::vbox(std::move(rows));
        content->ComputeRequirement();
        content->SetBox(box_);
        content->Render(screen);
    }

private:
    // `[x] 串口            3` — the box reports the enabled state, `*` marks the
    // solo view, the name carries the device colour.
    ftxui::Element item_row(const TypeEntry& entry, int index, int width) {
        const bool enabled = state_.type_enabled(entry.kind);
        const bool solo = state_.is_solo(entry.kind);
        const std::string box = solo ? "[*]" : (enabled ? "[x]" : "[ ]");
        const std::string name = std::string(dtb::device_kind_name(entry.kind));
        const std::string count = fmt::format("{}", entry.count);

        const int used = 4 + ftxui::string_width(name) + ftxui::string_width(count);
        const int gap = std::max(1, width - used);
        const std::string line
            = box + " " + name + std::string(static_cast<size_t>(gap), ' ') + count;

        ftxui::Element element
            = ftxui::text(fit_line(line, width)) | ftxui::color(device_color(entry.kind));
        if (!enabled) element = element | ftxui::dim;
        if (index == state_.types_cursor()) element = element | ftxui::inverted;
        return element;
    }

    State& state_;
};

} // namespace

ftxui::Element types_pane(State& state) {
    return std::make_shared<Pane>(state);
}

} // namespace ui