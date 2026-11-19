#include "ui/help_view.hpp"

#include <array>
#include <string>
#include <string_view>

#include <ftxui/dom/elements.hpp>

namespace ui {
namespace {

struct Binding {
    std::string_view keys;
    std::string_view action;
};

struct Group {
    std::string_view title;
    const Binding* bindings;
    size_t count;
};

// Mirrors the key table in wang/plan-overview.md §7, split by scope.
constexpr std::array<Binding, 6> kGlobal = {{
    {"q / Ctrl-C", "退出（帮助/搜索打开时先关闭浮层）"},
    {"?", "帮助浮层开/关"},
    {"/", "打开搜索输入行，增量过滤"},
    {"Esc", "清空搜索 / 关闭帮助浮层"},
    {"Tab / Shift-Tab", "在树面板与属性面板之间切换焦点"},
    {"鼠标滚轮", "滚动 3 行（指针所在面板）"},
}};

constexpr std::array<Binding, 9> kTree = {{
    {"j / ↓", "光标下移"},
    {"k / ↑", "光标上移"},
    {"l / →", "展开当前节点一层"},
    {"h / ←", "折叠当前节点；已折叠时跳到父节点"},
    {"Space", "切换折叠状态"},
    {"Enter", "焦点移到属性面板"},
    {"g / G", "跳到首行 / 末行"},
    {"Ctrl-D / Ctrl-U · PgDn / PgUp", "翻页"},
    {"鼠标左键", "点行选中；点 ▸/▾ 只切换折叠"},
}};

constexpr std::array<Binding, 8> kProps = {{
    {"j / ↓", "光标下移"},
    {"k / ↑", "光标上移"},
    {"Enter", "跳到可解析 phandle 的目标节点"},
    {"Backspace", "沿跳转栈回退上一个节点"},
    {"g / G", "跳到首个 / 最后一个属性"},
    {"Ctrl-D / Ctrl-U · PgDn / PgUp", "翻页"},
    {"Tab / Shift-Tab", "回到树面板"},
    {"鼠标左键", "点行选中"},
}};

constexpr std::array<Group, 3> kGroups = {{
    {"全局", kGlobal.data(), kGlobal.size()},
    {"设备树", kTree.data(), kTree.size()},
    {"属性", kProps.data(), kProps.size()},
}};

constexpr int kKeyColumnWidth = 30;

ftxui::Element group_rows(const Group& group) {
    ftxui::Elements rows;
    rows.push_back(ftxui::text(group.title) | ftxui::bold | ftxui::color(ftxui::Color::Cyan));
    for (size_t i = 0; i < group.count; ++i) {
        const Binding& binding = group.bindings[i];
        rows.push_back(ftxui::hbox({
            ftxui::text(std::string(binding.keys))
                | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kKeyColumnWidth),
            ftxui::text(std::string(binding.action)),
        }));
    }
    return ftxui::vbox(std::move(rows));
}

} // namespace

ftxui::Element help_overlay() {
    ftxui::Elements groups;
    groups.push_back(ftxui::text("dtbtui 键位帮助 —— ? 或 Esc 关闭") | ftxui::bold | ftxui::center);
    groups.push_back(ftxui::separator());
    for (size_t i = 0; i < kGroups.size(); ++i) {
        if (i != 0) groups.push_back(ftxui::separatorEmpty());
        groups.push_back(group_rows(kGroups[i]));
    }
    return ftxui::window(ftxui::text("帮助"), ftxui::vbox(std::move(groups)));
}

} // namespace ui