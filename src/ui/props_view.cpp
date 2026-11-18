#include "ui/props_view.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

#include "dtb/model.hpp"
#include "ui/state.hpp"
#include "ui/text_util.hpp"

namespace ui {
namespace {

int max_name_width(const std::vector<dtb::Property>& props) {
    int width = 0;
    for (const dtb::Property& prop : props)
        width = std::max(width, ftxui::string_width(prop.name));
    return width;
}

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
        state_.set_props_window_height(box.y_max - box.y_min + 1);
    }

    void Render(ftxui::Screen& screen) override {
        const int height = box_.y_max - box_.y_min + 1;
        const int width = box_.x_max - box_.x_min + 1;
        if (height <= 0 || width <= 0) return;

        const std::vector<dtb::Property>& props = state_.current_props();
        ftxui::Elements rows;
        rows.reserve(static_cast<size_t>(height));

        if (props.empty()) { rows.push_back(ftxui::text("(无属性)") | ftxui::dim); }
        else {
            const int name_width = std::min(max_name_width(props), std::max(1, width / 3));
            for (int row = 0; row < height; ++row) {
                const int index = state_.props_scroll() + row;
                if (index >= static_cast<int>(props.size())) {
                    rows.push_back(ftxui::text(" "));
                    continue;
                }

                const dtb::Property& prop = props[index];
                std::string name = prop.name;
                if (ftxui::string_width(name) > name_width)
                    name = fit_line(name, name_width);
                else
                    name.append(static_cast<size_t>(name_width - ftxui::string_width(name)), ' ');

                const int summary_width = width - name_width - 2;
                const std::string summary
                    = fit_line(property_summary(prop), std::max(0, summary_width));
                const bool selected = index == state_.props_cursor();

                if (selected) {
                    rows.push_back(ftxui::text(name + "  " + summary) | ftxui::inverted);
                }
                else {
                    rows.push_back(
                        ftxui::hbox({ftxui::text(name), ftxui::text("  "), ftxui::text(summary)})
                    );
                }
            }
        }

        ftxui::Element content = ftxui::vbox(std::move(rows));
        content->ComputeRequirement();
        content->SetBox(box_);
        content->Render(screen);
    }

private:
    State& state_;
};

} // namespace

ftxui::Element props_pane(State& state) {
    return std::make_shared<Pane>(state);
}

} // namespace ui