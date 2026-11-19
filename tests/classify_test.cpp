#include "doctest.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "dtb/classify.hpp"
#include "dtb/model.hpp"

namespace {

dtb::Property string_prop(std::string name, std::string value) {
    dtb::Property prop;
    prop.name = std::move(name);
    prop.raw.assign(value.begin(), value.end());
    prop.raw.push_back(0);
    return prop;
}

// `compatible` is a string list; NUL keeps the entries apart in the raw bytes.
dtb::Property compatible_prop(std::initializer_list<std::string> values) {
    dtb::Property prop;
    prop.name = "compatible";
    for (const std::string& value : values) {
        prop.raw.insert(prop.raw.end(), value.begin(), value.end());
        prop.raw.push_back(0);
    }
    return prop;
}

dtb::Node device(std::string name, std::string compatible) {
    dtb::Node node;
    node.name = std::move(name);
    node.path = "/" + node.name;
    node.depth = 1;
    if (!compatible.empty()) node.props.push_back(compatible_prop({compatible}));
    return node;
}

} // namespace

TEST_CASE("classify: compatible strings win over the node name") {
    CHECK(
        dtb::classify_node(device("uart@2500000", "allwinner,uart-v100")) == dtb::DeviceKind::Serial
    );
    CHECK(
        dtb::classify_node(device("twi@2510000", "allwinner,sunxi-twi-v101"))
        == dtb::DeviceKind::I2c
    );
    CHECK(
        dtb::classify_node(device("spi@2540000", "allwinner,sunxi-spi-v1.3"))
        == dtb::DeviceKind::Spi
    );
    CHECK(
        dtb::classify_node(device("sdmmc@4020000", "allwinner,sunxi-mmc-v5p3x"))
        == dtb::DeviceKind::Storage
    );
    CHECK(
        dtb::classify_node(device("ethernet@4500000", "snps,dwmac-5.20"))
        == dtb::DeviceKind::Network
    );
    CHECK(dtb::classify_node(device("gpu@1800000", "img,gpu")) == dtb::DeviceKind::Gpu);
    CHECK(
        dtb::classify_node(device("ve@1c0e000", "allwinner,sunxi-cedar-ve"))
        == dtb::DeviceKind::Video
    );
    CHECK(dtb::classify_node(device("dsi0@5506000", "allwinner,dsi0")) == dtb::DeviceKind::Display);
    CHECK(
        dtb::classify_node(device("interrupt-controller@3400000", "arm,gic-v3"))
        == dtb::DeviceKind::Interrupt
    );
}

TEST_CASE("classify: specific rules run before the generic bus rule") {
    // A video-in controller advertises itself as a simple bus as well.
    dtb::Node vind = device("vind@5800800", "");
    vind.props.push_back(compatible_prop({"allwinner,sunxi-vin-media", "simple-bus"}));
    CHECK(dtb::classify_node(vind) == dtb::DeviceKind::Camera);

    // The SoC container stays structure so that hiding "总线" never hides the
    // whole tree.
    CHECK(dtb::classify_node(device("soc@3000000", "simple-bus")) == dtb::DeviceKind::Structural);
    CHECK(dtb::classify_node(device("bus@1000000", "simple-bus")) == dtb::DeviceKind::Bus);
}

TEST_CASE("classify: the node name is the fallback") {
    CHECK(dtb::classify_node(device("usbc1@11", "")) == dtb::DeviceKind::Usb);
    CHECK(dtb::classify_node(device("led-0", "")) == dtb::DeviceKind::Led);
    CHECK(dtb::classify_node(device("rtc@51", "haoyu,hym8563")) == dtb::DeviceKind::Timer);
}

TEST_CASE("classify: device_type is used when compatible is absent") {
    dtb::Node node = device("some-peripheral@1000", "");
    node.props.push_back(string_prop("device_type", "sdc1"));
    CHECK(dtb::classify_node(node) == dtb::DeviceKind::Storage);
}

TEST_CASE("classify: structure and data are not devices") {
    CHECK(dtb::classify_node(device("aliases", "")) == dtb::DeviceKind::Structural);
    CHECK(dtb::classify_node(device("__symbols__", "")) == dtb::DeviceKind::Structural);
    CHECK(
        dtb::classify_node(device("cluster0-opp-table", "allwinner,sun50i-operating-points"))
        == dtb::DeviceKind::Structural
    );
    CHECK(dtb::classify_node(device("opp@408000000", "")) == dtb::DeviceKind::Structural);
    CHECK(dtb::classify_node(device("cpul_thermal_zone", "")) == dtb::DeviceKind::Structural);
    CHECK(dtb::classify_node(device("port", "")) == dtb::DeviceKind::Structural);
    CHECK(dtb::classify_node(device("endpoint", "")) == dtb::DeviceKind::Structural);
    CHECK(
        dtb::classify_node(device("pd_vi_test@0", "allwinner,sunxi-power-domain-test"))
        == dtb::DeviceKind::Structural
    );
}

TEST_CASE("classify: pin groups are structure even when named after a device") {
    dtb::Node group = device("uart0_pins@0", "");
    group.props.push_back(string_prop("function", "uart0"));
    CHECK(dtb::classify_node(group) == dtb::DeviceKind::Structural);

    // The same name with a real compatible string stays a device.
    CHECK(dtb::classify_node(device("uart0", "allwinner,uart-v100")) == dtb::DeviceKind::Serial);
}

TEST_CASE("classify: the root is a container") {
    dtb::Node root = device("/", "");
    root.props.push_back(compatible_prop({"xunlong,orangepi-4-pro", "arm,sun60iw2p1"}));
    root.depth = 0;
    CHECK(dtb::classify_node(root) == dtb::DeviceKind::Structural);
}

TEST_CASE("classify: unknown devices land in Other, unknown data in Structural") {
    CHECK(dtb::classify_node(device("widget@1", "vendor,widget")) == dtb::DeviceKind::Other);
    CHECK(dtb::classify_node(device("scratch", "")) == dtb::DeviceKind::Structural);
}

TEST_CASE("classify: classify() fills every node and names are stable") {
    dtb::Tree tree;
    dtb::Node root = device("/", "simple-bus");
    root.depth = 0;
    tree.nodes.push_back(root);
    tree.nodes.push_back(device("uart@1", "allwinner,uart-v100"));
    tree.nodes.push_back(device("aliases", ""));

    dtb::classify(tree);
    CHECK(tree.nodes[0].kind == dtb::DeviceKind::Structural);
    CHECK(tree.nodes[1].kind == dtb::DeviceKind::Serial);
    CHECK(tree.nodes[2].kind == dtb::DeviceKind::Structural);

    CHECK(dtb::device_kind_name(dtb::DeviceKind::Serial) == "串口");
    CHECK(dtb::device_kind_name(dtb::DeviceKind::Structural) == "结构");
    CHECK(dtb::kSelectableKinds.size() == 30);
}