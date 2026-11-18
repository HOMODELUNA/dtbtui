#pragma once

#include <string_view>

#include "dtb/model.hpp"

namespace dtb {

// Fill `kind`, `kind_reason` and `lines` for every property in the tree.
void decode(Tree& tree);

// Stable display name for a value kind, used by --dump and later the UI.
std::string_view kind_name(ValueKind kind);

} // namespace dtb