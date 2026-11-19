#pragma once

#include <ftxui/dom/elements.hpp>

#include "ui/state.hpp"

namespace ui {

// Custom ftxui element listing the device types with their node counts.  Each
// row acts as a filter: `Space` enables/disables the type, `Enter` shows it
// alone.
ftxui::Element types_pane(State& state);

} // namespace ui