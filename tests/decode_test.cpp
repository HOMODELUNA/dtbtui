#include "doctest.h"

#include <cstdint>
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