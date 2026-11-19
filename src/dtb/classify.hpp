#pragma once

#include <array>
#include <string_view>

#include "dtb/model.hpp"

namespace dtb {

// Every selectable kind, in panel order.  `Structural` is not selectable: those
// nodes appear only as ancestors of a kept node.
constexpr std::array<DeviceKind, 30> kSelectableKinds = {
    DeviceKind::Cpu,      DeviceKind::Bus,     DeviceKind::Memory,   DeviceKind::Interrupt,
    DeviceKind::Iommu,    DeviceKind::Clock,   DeviceKind::Power,    DeviceKind::Pinctrl,
    DeviceKind::Storage,  DeviceKind::Serial,  DeviceKind::I2c,      DeviceKind::Spi,
    DeviceKind::Usb,      DeviceKind::Network, DeviceKind::Wireless, DeviceKind::Display,
    DeviceKind::Panel,    DeviceKind::Gpu,     DeviceKind::Video,    DeviceKind::Camera,
    DeviceKind::Audio,    DeviceKind::Pwm,     DeviceKind::Adc,      DeviceKind::Thermal,
    DeviceKind::Timer,    DeviceKind::Dma,     DeviceKind::Crypto,   DeviceKind::Led,
    DeviceKind::Firmware, DeviceKind::Other,
};

// Fill `Node::kind` for every node.  Rules are ordered: the first match wins.
// The decision uses `compatible`, `device_type` and the node name only, so it
// works on any blob without a driver database.
void classify(Tree& tree);

// Classify one node; exposed for tests and for --dump.
DeviceKind classify_node(const Node& node);

// Stable Chinese display name, used by the type panel, --dump and the help.
std::string_view device_kind_name(DeviceKind kind);

} // namespace dtb