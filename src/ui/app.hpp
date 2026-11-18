#pragma once

#include <string>

#include "dtb/model.hpp"

namespace ui {

// Run the fullscreen tree viewer.  Returns the process exit code.
int run(const dtb::Tree& tree);

// Show a fullscreen load-error page.  Returns 1 after the user quits.
int run_error(const std::string& source, const std::string& message);

} // namespace ui