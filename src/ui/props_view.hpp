#pragma once

#include <ftxui/dom/elements.hpp>

#include "ui/state.hpp"

namespace ui {

// Custom ftxui element that renders the current node's property table.
ftxui::Element props_pane(State& state);

} // namespace ui