#pragma once

#include <ftxui/dom/elements.hpp>

namespace ui {

// Key-binding help overlay.  Meant to be drawn on top of the normal view with
// `dbox`, `clear_under` and `center`.
ftxui::Element help_overlay();

} // namespace ui