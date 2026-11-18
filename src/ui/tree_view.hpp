#pragma once

#include <ftxui/dom/elements.hpp>

#include "dtb/model.hpp"
#include "ui/state.hpp"

namespace ui {

// Custom ftxui element that renders only the visible window of tree rows.
ftxui::Element tree_pane(const dtb::Tree& tree, State& state);

} // namespace ui