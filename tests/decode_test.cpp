#include "doctest.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dtb/decode.hpp"
#include "dtb/model.hpp"

namespace {

std::vector<uint8_t> bytes(std::string_view text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> string_bytes(const std::string& text) {
    std::vector<uint8_t> raw(text.begin(), text.end());
    raw.push_back('\0');
    return raw;
}

std::vector<uint8_t> string_list_bytes(const std::vector<std::string>& values) {
    std::vector<uint8_t> raw;
    for (const std::string& value : values) {
        raw.insert(raw.end(), value.begin(), value.end());
        raw.push_back('\0');
    }
    return raw;
}

dtb::Tree tree_with(std::vector<dtb::Property> props) {
    dtb::Tree tree;
    dtb::Node node;
    node.name = "/";
    node.path = "/";
    node.props = std::move(props);
    tree.nodes.push_back(std::move(node));
    tree.total_props = tree.nodes.front().props.size();
    return tree;
}

dtb::Property property(std::string name, std::vector<uint8_t> raw) {
    dtb::Property prop;
    prop.name = std::move(name);
    prop.raw = std::move(raw);
    return prop;
}

std::vector<uint8_t> cell(uint32_t value) {
    return {
        static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)
    };
}

std::vector<uint8_t> cells(std::initializer_list<uint32_t> values) {
    std::vector<uint8_t> raw;
    for (uint32_t value : values) {
        const std::vector<uint8_t> one = cell(value);
        raw.insert(raw.end(), one.begin(), one.end());
    }
    return raw;
}

dtb::Node node_at(std::string path) {
    dtb::Node node;
    const size_t slash = path.find_last_of('/');
    node.name = slash == std::string::npos ? path : path.substr(slash + 1);
    node.path = std::move(path);
    return node;
}

dtb::Tree tree_with_nodes(std::vector<dtb::Node> nodes) {
    dtb::Tree tree;
    tree.nodes = std::move(nodes);
    for (const dtb::Node& node : tree.nodes)
        tree.total_props += node.props.size();
    return tree;
}

} // namespace

TEST_CASE("decode: zero-length property is Empty") {
    dtb::Tree tree = tree_with({property("interrupt-controller", {})});
    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[0].props[0];
    CHECK(prop.kind == dtb::ValueKind::Empty);
    CHECK_FALSE(prop.kind_reason.empty());
    REQUIRE(prop.lines.size() == 1);
    CHECK(prop.lines[0] == "(空属性 / boolean)");
}

TEST_CASE("decode: whitelisted multi-string property is StringList") {
    dtb::Tree tree
        = tree_with({property("compatible", string_list_bytes({"test,machine", "test,soc"}))});
    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[0].props[0];
    CHECK(prop.kind == dtb::ValueKind::StringList);
    CHECK(prop.kind_reason.find("compatible") != std::string::npos);
    REQUIRE(prop.lines.size() == 2);
    CHECK(prop.lines[0] == "test,machine");
    CHECK(prop.lines[1] == "test,soc");
}

TEST_CASE("decode: whitelisted single string property is String") {
    dtb::Tree tree = tree_with({property("status", string_bytes("okay"))});
    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[0].props[0];
    CHECK(prop.kind == dtb::ValueKind::String);
    REQUIRE(prop.lines.size() == 1);
    CHECK(prop.lines[0] == "okay");
}

TEST_CASE("decode: printable NUL-terminated value is String without whitelist") {
    dtb::Tree tree = tree_with({property("vendor,foo", string_bytes("hello"))});
    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[0].props[0];
    CHECK(prop.kind == dtb::ValueKind::String);
    CHECK_FALSE(prop.kind_reason.empty());
    REQUIRE(prop.lines.size() == 1);
    CHECK(prop.lines[0] == "hello");
}

TEST_CASE("decode: printable value without trailing NUL is not a string") {
    dtb::Tree tree = tree_with({property("vendor,foo", bytes("hello"))});
    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[0].props[0];
    CHECK(prop.kind == dtb::ValueKind::Bytes);
    REQUIRE(prop.lines.size() == 1);
    CHECK(prop.lines[0].find("68 65 6c 6c 6f") != std::string::npos);
    CHECK(prop.lines[0].find("hello") != std::string::npos);
}

TEST_CASE("decode: five non-printable bytes fall back to hex dump") {
    dtb::Tree tree
        = tree_with({property("vendor,blob", std::vector<uint8_t>{0x00, 0x01, 0x02, 0x03, 0x04})});
    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[0].props[0];
    CHECK(prop.kind == dtb::ValueKind::Bytes);
    REQUIRE(prop.lines.size() == 1);
    CHECK(prop.lines[0].find("00 01 02 03 04") != std::string::npos);
    CHECK(prop.lines[0].find(".....") != std::string::npos);
}

TEST_CASE("decode: all-zero cells are not mistaken for strings") {
    dtb::Tree tree
        = tree_with({property("#size-cells", std::vector<uint8_t>{0x00, 0x00, 0x00, 0x00})});
    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[0].props[0];
    CHECK(prop.kind != dtb::ValueKind::String);
    CHECK(prop.kind != dtb::ValueKind::StringList);
}

TEST_CASE("decode: hex dump wraps after sixteen bytes") {
    std::vector<uint8_t> raw;
    for (uint8_t byte = 0; byte < 17; ++byte)
        raw.push_back(byte);
    dtb::Tree tree = tree_with({property("vendor,blob", raw)});
    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[0].props[0];
    CHECK(prop.kind == dtb::ValueKind::Bytes);
    REQUIRE(prop.lines.size() == 2);
    CHECK(prop.lines[0].find("0f") != std::string::npos);
    CHECK(prop.lines[0].find("10") == std::string::npos);
    CHECK(prop.lines[1].find("10") != std::string::npos);
}
TEST_CASE("decode: 4-byte aligned value is Cells with big-endian u32") {
    dtb::Tree tree = tree_with({property("clocks", cell(0x0102))});
    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[0].props[0];
    CHECK(prop.kind == dtb::ValueKind::Cells);
    REQUIRE(prop.lines.size() == 1);
    CHECK(prop.lines[0].find("0x00000102 (258)") != std::string::npos);
}

TEST_CASE("decode: Cells format wraps after eight units") {
    dtb::Tree tree = tree_with({property("clocks", cells({1, 2, 3, 4, 5, 6, 7, 8, 9}))});
    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[0].props[0];
    CHECK(prop.kind == dtb::ValueKind::Cells);
    REQUIRE(prop.lines.size() == 2);
    CHECK(prop.lines[0].find("0x00000008 (8)") != std::string::npos);
    CHECK(prop.lines[0].find("0x00000009 (9)") == std::string::npos);
    CHECK(prop.lines[1].find("0x00000009 (9)") != std::string::npos);
}

TEST_CASE("decode: forward phandle reference resolves to a later node") {
    dtb::Tree tree
        = tree_with_nodes({node_at("/"), node_at("/soc/uart@ff0000"), node_at("/soc/gic@3000000")});
    tree.nodes[1].props.push_back(property("interrupt-parent", cell(2)));
    tree.nodes[2].phandle = 2;
    tree.phandle_map.emplace(2, 2);
    tree.total_props = 1;

    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[1].props[0];
    CHECK(prop.kind == dtb::ValueKind::Cells);
    REQUIRE(prop.refs.size() == 1);
    CHECK(prop.refs[0].cell_index == 0);
    CHECK(prop.refs[0].node == 2);
    REQUIRE(prop.lines.size() == 1);
    CHECK(prop.lines[0].find("0x00000002 (2)") != std::string::npos);
    CHECK(prop.lines[0].find("→ /soc/gic@3000000") != std::string::npos);
}

TEST_CASE("decode: unresolved phandle keeps a marker and node == -1") {
    dtb::Tree tree = tree_with_nodes({node_at("/"), node_at("/soc/uart@ff0000")});
    tree.nodes[1].props.push_back(property("interrupt-parent", cell(0x2a)));
    tree.total_props = 1;

    dtb::decode(tree);

    const dtb::Property& prop = tree.nodes[1].props[0];
    REQUIRE(prop.refs.size() == 1);
    CHECK(prop.refs[0].node == -1);
    REQUIRE(prop.lines.size() == 1);
    CHECK(prop.lines[0].find("<未解析 0x0000002a>") != std::string::npos);
}

TEST_CASE("decode: reg and clocks are never treated as references") {
    dtb::Tree tree
        = tree_with_nodes({node_at("/"), node_at("/soc/gic@3000000"), node_at("/soc/uart@ff0000")});
    tree.phandle_map.emplace(5, 1);
    tree.nodes[2].props.push_back(property("reg", cell(5)));
    tree.nodes[2].props.push_back(property("clocks", cell(5)));
    tree.nodes[2].props.push_back(property("gpio-handle", cell(5)));
    tree.total_props = 3;

    dtb::decode(tree);

    CHECK(tree.nodes[2].props[0].kind == dtb::ValueKind::Cells);
    CHECK(tree.nodes[2].props[0].refs.empty());
    CHECK(tree.nodes[2].props[1].refs.empty());

    const dtb::Property& suffix = tree.nodes[2].props[2];
    REQUIRE(suffix.refs.size() == 1);
    CHECK(suffix.refs[0].node == 1);
    CHECK(suffix.lines[0].find("→ /soc/gic@3000000") != std::string::npos);
}

TEST_CASE("decode: duplicate phandle is noted in kind_reason") {
    dtb::Tree tree = tree_with_nodes({node_at("/"), node_at("/a"), node_at("/b")});
    tree.nodes[1].props.push_back(property("phandle", cell(7)));
    tree.nodes[2].props.push_back(property("phandle", cell(7)));
    tree.phandle_map.emplace(7, 1);
    tree.total_props = 2;

    dtb::decode(tree);

    const dtb::Property& first = tree.nodes[1].props[0];
    const dtb::Property& duplicate = tree.nodes[2].props[0];
    CHECK(first.kind_reason.find("重复") == std::string::npos);
    CHECK(duplicate.kind_reason.find("重复") != std::string::npos);
    REQUIRE(duplicate.refs.size() == 1);
    CHECK(duplicate.refs[0].node == 1);
}
