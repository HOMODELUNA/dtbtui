#include "ui/app.hpp"

#include <algorithm>
#include <string>

#include <fmt/format.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "dtb/model.hpp"
#include "ui/props_view.hpp"
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
    app.ForceHandleCtrlC(false);

    auto renderer = ftxui::Renderer([&] {
        const int left_width = std::max(24, app.dimx() * 34 / 100);
        const ftxui::Element title = ftxui::text(fmt::format(
                                         "dtbtui — {} — {} 节点 / {} 属性", tree.source,
                                         tree.nodes.size(), tree.total_props
                                     ))
                                   | ftxui::bold;

        const bool tree_focused = state.focus() == Focus::Tree;
        const bool props_focused = state.focus() == Focus::Props;

        ftxui::Element tree_title = ftxui::text("设备树");
        ftxui::Element props_title = ftxui::text("属性");
        if (tree_focused) tree_title = tree_title | ftxui::bold;
        if (props_focused) props_title = props_title | ftxui::bold;

        const ftxui::Element tree_panel
            = ftxui::window(
                  tree_title, tree_pane(tree, state), tree_focused ? ftxui::HEAVY : ftxui::ROUNDED
              )
            | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, left_width);
        const ftxui::Element props_panel
            = ftxui::window(
                  props_title, props_pane(state), props_focused ? ftxui::HEAVY : ftxui::ROUNDED
              )
            | ftxui::flex;
        const ftxui::Element detail_panel
            = ftxui::window(ftxui::text("详情"), ftxui::text("")) | ftxui::flex;
        const ftxui::Element right = ftxui::vbox({props_panel, detail_panel}) | ftxui::flex;
        const ftxui::Element body = ftxui::hbox({tree_panel, right}) | ftxui::flex;

        const std::string focus_name = tree_focused ? "设备树" : "属性";
        const ftxui::Element status
            = ftxui::text(fmt::format("焦点: {} | {}", focus_name, state.current_node().path))
            | ftxui::dim;
        return ftxui::vbox({title, ftxui::separator(), body, ftxui::separator(), status})
             | ftxui::flex;
    });

    auto component = ftxui::CatchEvent(renderer, [&](const ftxui::Event& event) {
        if (is_quit(event)) {
            app.Exit();
            return true;
        }
        if (event == ftxui::Event::Tab || event == ftxui::Event::TabReverse) {
            state.toggle_focus();
            return true;
        }

        const bool tree_focused = state.focus() == Focus::Tree;
        if (event == ftxui::Event::Character('j') || event == ftxui::Event::ArrowDown) {
            if (tree_focused)
                state.move_cursor(1);
            else
                state.move_props_cursor(1);
            return true;
        }
        if (event == ftxui::Event::Character('k') || event == ftxui::Event::ArrowUp) {
            if (tree_focused)
                state.move_cursor(-1);
            else
                state.move_props_cursor(-1);
            return true;
        }
        if (event == ftxui::Event::Character('l') || event == ftxui::Event::ArrowRight) {
            if (tree_focused) state.expand_current();
            return true;
        }
        if (event == ftxui::Event::Character('h') || event == ftxui::Event::ArrowLeft) {
            if (tree_focused) state.collapse_current();
            return true;
        }
        if (event == ftxui::Event::Character(' ')) {
            if (tree_focused) state.toggle_current();
            return true;
        }
        if (event == ftxui::Event::Character('g')) {
            if (tree_focused)
                state.move_to_first();
            else
                state.props_first();
            return true;
        }
        if (event == ftxui::Event::Character('G')) {
            if (tree_focused)
                state.move_to_last();
            else
                state.props_last();
            return true;
        }
        if (event == ftxui::Event::CtrlD || event == ftxui::Event::PageDown) {
            if (tree_focused)
                state.page(1);
            else
                state.props_page(1);
            return true;
        }
        if (event == ftxui::Event::CtrlU || event == ftxui::Event::PageUp) {
            if (tree_focused)
                state.page(-1);
            else
                state.props_page(-1);
            return true;
        }
        return false;
    });

    app.Loop(component);
    return 0;
}

int run_error(const std::string& source, const std::string& message) {
    ftxui::App app = ftxui::App::Fullscreen();
    app.ForceHandleCtrlC(false);
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