#include "ui/tree_view.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
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

class Pane : public ftxui::Node {
public:
    Pane(const dtb::Tree& tree, State& state)
        : tree_(tree)
        , state_(state) {}

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
        state_.set_window_height(box.y_max - box.y_min + 1);
    }

    void Render(ftxui::Screen& screen) override {
        const int height = box_.y_max - box_.y_min + 1;
        const int width = box_.x_max - box_.x_min + 1;
        if (height <= 0 || width <= 0) return;

        const std::vector<int>& visible = state_.visible();
        ftxui::Elements rows;
        rows.reserve(static_cast<size_t>(height));
        for (int row = 0; row < height; ++row) {
            const int index = state_.scroll() + row;
            if (index >= static_cast<int>(visible.size())) {
                rows.push_back(ftxui::text(" "));
                continue;
            }

            const int node_index = visible[index];
            const dtb::Node& node = tree_.nodes[node_index];
            std::string line(static_cast<size_t>(node.depth) * 2, ' ');
            if (node.children.empty())
                line.append("  ");
            else if (state_.is_expanded(node_index))
                line.append("▾ ");
            else
                line.append("▸ ");
            line += node.name;

            ftxui::Element element = ftxui::text(fit_line(std::move(line), width));
            if (node.disabled) element = element | ftxui::dim;
            if (index == state_.cursor()) element = element | ftxui::inverted;
            rows.push_back(std::move(element));
        }

        ftxui::Element content = ftxui::vbox(std::move(rows));
        content->ComputeRequirement();
        content->SetBox(box_);
        content->Render(screen);
    }

private:
    const dtb::Tree& tree_;
    State& state_;
};

} // namespace

ftxui::Element tree_pane(const dtb::Tree& tree, State& state) {
    return std::make_shared<Pane>(tree, state);
}

} // namespace ui