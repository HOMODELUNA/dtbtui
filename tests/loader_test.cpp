#include "doctest.h"

#include <libfdt.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "dtb/loader.hpp"
#include "dtb/model.hpp"

namespace {

constexpr size_t kBlobSize = 8192;

std::vector<uint8_t> u32_cell(uint32_t value) {
    std::vector<uint8_t> bytes(sizeof(value));
    value = __builtin_bswap32(value);
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

class Blob {
public:
    Blob()
        : bytes_(kBlobSize) {
        REQUIRE(fdt_create_empty_tree(bytes_.data(), static_cast<int>(bytes_.size())) == 0);
    }

    int root() const { return fdt_path_offset(bytes_.data(), "/"); }

    int add_node(const std::string& path) {
        const size_t slash = path.find_last_of('/');
        const std::string parent_path = slash == 0 ? "/" : path.substr(0, slash);
        const std::string name = path.substr(slash + 1);
        const int parent = fdt_path_offset(bytes_.data(), parent_path.c_str());
        REQUIRE(parent >= 0);
        const int node = fdt_add_subnode(bytes_.data(), parent, name.c_str());
        REQUIRE(node >= 0);
        return node;
    }

    void set_prop(int node, const std::string& name, const void* data, int len) {
        REQUIRE(fdt_setprop(bytes_.data(), node, name.c_str(), data, len) == 0);
    }

    void set_prop(int node, const std::string& name, const std::vector<uint8_t>& data) {
        set_prop(node, name, data.data(), static_cast<int>(data.size()));
    }

    // Device-tree strings include their trailing NUL.
    void set_string(int node, const std::string& name, const std::string& value) {
        set_prop(node, name, value.c_str(), static_cast<int>(value.size() + 1));
    }

    const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

dtb::Tree load_ok(const std::vector<uint8_t>& blob) {
    dtb::LoadResult result = dtb::load(blob, "test");
    REQUIRE(result.error.empty());
    REQUIRE(result.tree.has_value());
    return *result.tree;
}

int find_path(const dtb::Tree& tree, std::string_view path) {
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        if (tree.nodes[i].path == path) return static_cast<int>(i);
    }
    return -1;
}

// Builds:
//
//   /                       model, compatible, empty
//   /soc                    #address-cells, #size-cells
//   /soc/serial@ff0000      reg
//   /a                      status = "disabled"
//   /a/x
//   /b
//   /b/x
//
// libfdt inserts new properties and subnodes at the front of the existing
// list, so both are inserted in reverse to make the dtb order match the
// comments above.
dtb::Tree build_sample() {
    Blob blob;
    const int root = blob.root();
    blob.set_prop(root, "empty", std::vector<uint8_t>{});
    std::string compatible = std::string("test,machine") + '\0' + "test,soc";
    compatible.push_back('\0');
    blob.set_prop(root, "compatible", compatible.data(), static_cast<int>(compatible.size()));
    blob.set_string(root, "model", "test,machine");

    blob.add_node("/b");
    blob.add_node("/b/x");
    const int a = blob.add_node("/a");
    blob.set_string(a, "status", "disabled");
    blob.add_node("/a/x");
    const int soc = blob.add_node("/soc");
    blob.set_prop(soc, "#size-cells", u32_cell(1));
    blob.set_prop(soc, "#address-cells", u32_cell(2));

    const int serial = blob.add_node("/soc/serial@ff0000");
    blob.set_prop(serial, "reg", u32_cell(0xff0000));

    return load_ok(blob.bytes());
}

} // namespace

TEST_CASE("loader preserves hierarchy, paths and depths") {
    const dtb::Tree tree = build_sample();

    REQUIRE(tree.nodes.size() == 7);
    const dtb::Node& root = tree.nodes[0];
    CHECK(root.name == "/");
    CHECK(root.path == "/");
    CHECK(root.depth == 0);
    CHECK(root.parent == -1);
    REQUIRE(root.children.size() == 3);

    const dtb::Node& soc = tree.nodes[1];
    CHECK(soc.name == "soc");
    CHECK(soc.path == "/soc");
    CHECK(soc.depth == 1);
    CHECK(soc.parent == 0);
    REQUIRE(soc.children.size() == 1);

    const int serial = find_path(tree, "/soc/serial@ff0000");
    REQUIRE(serial >= 0);
    CHECK(tree.nodes[serial].name == "serial@ff0000");
    CHECK(tree.nodes[serial].depth == 2);
    CHECK(tree.nodes[serial].parent == 1);
    CHECK(tree.nodes[serial].children.empty());
}

TEST_CASE("loader keeps children in dtb order") {
    const dtb::Tree tree = build_sample();
    const dtb::Node& root = tree.nodes[0];

    REQUIRE(root.children.size() == 3);
    CHECK(tree.nodes[root.children[0]].name == "soc");
    CHECK(tree.nodes[root.children[1]].name == "a");
    CHECK(tree.nodes[root.children[2]].name == "b");

    const int a = find_path(tree, "/a");
    REQUIRE(a >= 0);
    REQUIRE(tree.nodes[a].children.size() == 1);
    CHECK(tree.nodes[tree.nodes[a].children[0]].path == "/a/x");
}

TEST_CASE("loader keeps properties in dtb order and raw bytes exact") {
    const dtb::Tree tree = build_sample();
    const dtb::Node& root = tree.nodes[0];

    REQUIRE(root.props.size() == 3);
    CHECK(root.props[0].name == "model");
    CHECK(root.props[1].name == "compatible");
    CHECK(root.props[2].name == "empty");
    CHECK(root.props[2].raw.empty());

    const std::string expected_compatible = std::string("test,machine") + '\0' + "test,soc" + '\0';
    REQUIRE(root.props[1].raw.size() == expected_compatible.size());
    CHECK(
        std::memcmp(
            root.props[1].raw.data(), expected_compatible.data(), expected_compatible.size()
        )
        == 0
    );

    const int serial = find_path(tree, "/soc/serial@ff0000");
    REQUIRE(serial >= 0);
    REQUIRE(tree.nodes[serial].props.size() == 1);
    CHECK(tree.nodes[serial].props[0].name == "reg");
    const std::vector<uint8_t> expected_reg = u32_cell(0xff0000);
    CHECK(tree.nodes[serial].props[0].raw == expected_reg);
}

TEST_CASE("loader marks status=disabled nodes") {
    const dtb::Tree tree = build_sample();
    const int a = find_path(tree, "/a");
    const int b = find_path(tree, "/b");
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    CHECK(tree.nodes[a].disabled);
    CHECK_FALSE(tree.nodes[b].disabled);
}

TEST_CASE("loader distinguishes equal names under different parents") {
    const dtb::Tree tree = build_sample();
    const int ax = find_path(tree, "/a/x");
    const int bx = find_path(tree, "/b/x");
    REQUIRE(ax >= 0);
    REQUIRE(bx >= 0);
    CHECK(ax != bx);
    CHECK(tree.nodes[ax].name == tree.nodes[bx].name);
}

TEST_CASE("loader counts nodes and properties") {
    const dtb::Tree tree = build_sample();
    CHECK(tree.source == "test");
    CHECK(tree.total_props == 7);

    size_t sum = 0;
    for (const dtb::Node& node : tree.nodes)
        sum += node.props.size();
    CHECK(sum == tree.total_props);
}

TEST_CASE("loader rejects empty, non-dtb and truncated blobs") {
    SUBCASE("empty") {
        const dtb::LoadResult result = dtb::load({}, "empty.dtb");
        CHECK_FALSE(result.tree.has_value());
        CHECK_FALSE(result.error.empty());
    }

    SUBCASE("non dtb") {
        const std::vector<uint8_t> blob = {'h', 'e', 'l', 'l', 'o'};
        const dtb::LoadResult result = dtb::load(blob, "text.dtb");
        CHECK_FALSE(result.tree.has_value());
        CHECK_FALSE(result.error.empty());
    }

    SUBCASE("truncated") {
        Blob blob;
        blob.set_string(blob.root(), "model", "test,machine");
        std::vector<uint8_t> truncated = blob.bytes();
        truncated.resize(truncated.size() / 2);
        const dtb::LoadResult result = dtb::load(truncated, "broken.dtb");
        CHECK_FALSE(result.tree.has_value());
        CHECK_FALSE(result.error.empty());
    }
}