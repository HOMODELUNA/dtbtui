#include "dtb/loader.hpp"

#include <libfdt.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "dtb/model.hpp"

namespace dtb {
namespace {

bool is_disabled(const Property& prop) {
    constexpr char kDisabled[] = "disabled";
    constexpr size_t kLen = sizeof(kDisabled) - 1;
    if (prop.raw.size() != kLen + 1) return false;
    if (std::memcmp(prop.raw.data(), kDisabled, kLen) != 0) return false;
    return prop.raw.back() == '\0';
}

class Builder {
public:
    Builder(const void* fdt, Tree tree)
        : fdt_(fdt)
        , tree_(std::move(tree)) {}

    Tree build() {
        const int root = fdt_path_offset(fdt_, "/");
        if (root < 0) {
            tree_.nodes.clear();
            return std::move(tree_);
        }
        build_node(root, -1, 0);
        build_phandle_index();
        return std::move(tree_);
    }

private:
    static uint32_t read_be32(const std::vector<uint8_t>& raw) {
        return (static_cast<uint32_t>(raw[0]) << 24) | (static_cast<uint32_t>(raw[1]) << 16)
             | (static_cast<uint32_t>(raw[2]) << 8) | static_cast<uint32_t>(raw[3]);
    }

    // Register every node reachable through phandle/linux,phandle.  The first
    // value seen in tree order wins, matching the plan's duplicate policy.
    void build_phandle_index() {
        for (size_t index = 0; index < tree_.nodes.size(); ++index) {
            Node& node = tree_.nodes[index];
            for (const Property& prop : node.props) {
                if (prop.name != "phandle" && prop.name != "linux,phandle") continue;
                if (prop.raw.size() < sizeof(uint32_t)) continue;
                const uint32_t value = read_be32(prop.raw);
                node.phandle = static_cast<int>(value);
                tree_.phandle_map.emplace(value, static_cast<int>(index));
                break;
            }
        }
    }

    int build_node(int offset, int parent, int depth) {
        const int index = static_cast<int>(tree_.nodes.size());
        tree_.nodes.emplace_back();
        Node& node = tree_.nodes[index];

        const char* raw_name = fdt_get_name(fdt_, offset, nullptr);
        const std::string parent_path = parent < 0 ? "" : tree_.nodes[parent].path;
        if (parent < 0) {
            node.name = "/";
            node.path = "/";
        }
        else {
            node.name = raw_name != nullptr ? raw_name : "";
            node.path = parent_path == "/" ? "/" + node.name : parent_path + "/" + node.name;
        }
        node.depth = depth;
        node.parent = parent;

        int prop_offset = fdt_first_property_offset(fdt_, offset);
        while (prop_offset >= 0) {
            int value_len = 0;
            const struct fdt_property* prop
                = fdt_get_property_by_offset(fdt_, prop_offset, &value_len);
            if (prop == nullptr) break;

            Property property;
            const char* name = fdt_string(fdt_, fdt32_to_cpu(prop->nameoff));
            property.name = name != nullptr ? name : "";
            const auto* data = reinterpret_cast<const uint8_t*>(prop->data);
            property.raw.assign(data, data + value_len);
            if (property.name == "status" && is_disabled(property)) node.disabled = true;
            node.props.push_back(std::move(property));
            ++tree_.total_props;

            prop_offset = fdt_next_property_offset(fdt_, prop_offset);
        }

        // Re-read through the arena index: recursion below may grow the vector.
        int child_offset = fdt_first_subnode(fdt_, offset);
        while (child_offset >= 0) {
            const int child_index = build_node(child_offset, index, depth + 1);
            tree_.nodes[index].children.push_back(child_index);
            child_offset = fdt_next_subnode(fdt_, child_offset);
        }

        return index;
    }

    const void* fdt_;
    Tree tree_;
};

} // namespace

LoadResult load(std::vector<uint8_t> blob, std::string source) {
    LoadResult result;

    if (blob.empty()) {
        result.error = fdt_strerror(-FDT_ERR_TRUNCATED);
        return result;
    }

    const int check = fdt_check_full(blob.data(), blob.size());
    if (check != 0) {
        result.error = fdt_strerror(check);
        return result;
    }

    Tree tree;
    tree.source = std::move(source);
    Builder builder(blob.data(), std::move(tree));
    result.tree = builder.build();
    return result;
}

} // namespace dtb