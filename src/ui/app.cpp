#include "ui/app.hpp"

#include <algorithm>
#include <string>

#include <fmt/format.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "dtb/model.hpp"
#include "ui/state.hpp"
#include "ui/tree_view.hpp"

namespace ui {
namespace {

bool is_quit(const ftxui::Event& event) {
    return event == ftxui::Event::Character('q') || event == ftxui::Event::CtrlC;
}

} // namespace

int run(const dtb::Tree& tree) {
    State state(tree);
    ftxui::App app = ftxui::App::Fullscreen();

    auto renderer = ftxui::Renderer([&] {
        const int left_width = std::max(24, app.dimx() * 34 / 100);
        const ftxui::Element title = ftxui::text(fmt::format(
                                         "dtbtui — {} — {} 节点 / {} 属性", tree.source,
                                         tree.nodes.size(), tree.total_props
                                     ))
                                   | ftxui::bold;

        const ftxui::Element tree_panel
            = ftxui::window(ftxui::text("设备树"), tree_pane(tree, state))
            | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, left_width);
        const ftxui::Element placeholder
            = ftxui::window(ftxui::text("属性"), ftxui::text("")) | ftxui::flex;
        const ftxui::Element body = ftxui::hbox({tree_panel, placeholder}) | ftxui::flex;

        const ftxui::Element status = ftxui::text(state.current_node().path) | ftxui::dim;
        return ftxui::vbox({title, ftxui::separator(), body, ftxui::separator(), status})
             | ftxui::flex;
    });

    auto component = ftxui::CatchEvent(renderer, [&](const ftxui::Event& event) {
        if (!is_quit(event)) return false;
        app.Exit();
        return true;
    });

    app.Loop(component);
    return 0;
}

int run_error(const std::string& source, const std::string& message) {
    ftxui::App app = ftxui::App::Fullscreen();
    auto renderer = ftxui::Renderer([&] {
        return ftxui::vbox({
                   ftxui::text("dtbtui — 无法加载设备树") | ftxui::bold,
                   ftxui::separator(),
                   ftxui::text("路径: " + source),
                   ftxui::text("错误: " + message),
                   ftxui::separator(),
                   ftxui::text("按 q 退出"),
               })
             | ftxui::center | ftxui::flex;
    });

    auto component = ftxui::CatchEvent(renderer, [&](const ftxui::Event& event) {
        if (!is_quit(event)) return false;
        app.Exit();
        return true;
    });

    app.Loop(component);
    return 1;
}

} // namespace ui