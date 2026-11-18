#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dtb/model.hpp"

namespace dtb {

struct LoadResult {
    std::optional<Tree> tree;
    std::string error; // fdt_strerror() text when `tree` is empty
};

// Validate the whole blob with fdt_check_full() and build the tree model.
// A truncated or malformed blob never yields a partial tree.
LoadResult load(std::vector<uint8_t> blob, std::string source);

} // namespace dtb