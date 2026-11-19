#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dtb {

enum class ValueKind {
    Empty,      // len == 0
    Bytes,      // fallback: hex dump
    Cells,      // big-endian u32 sequence
    Reg,        // split by the parent's #address-cells/#size-cells
    String,     // one NUL-terminated string
    StringList, // several NUL-terminated strings
};

// Device class inferred from `compatible`, `device_type` and the node name.
// Structural marks nodes that are not devices: containers, pin groups, OPP
// entries, graph ports, thermal policy and metadata.
enum class DeviceKind {
    Structural, // not a device
    Cpu,
    Bus,
    Memory,
    Interrupt,
    Iommu,
    Clock,
    Power,
    Pinctrl,
    Storage,
    Serial,
    I2c,
    Spi,
    Usb,
    Network,
    Wireless,
    Display,
    Panel,
    Gpu,
    Video,
    Camera,
    Audio,
    Pwm,
    Adc,
    Thermal,
    Timer,
    Dma,
    Crypto,
    Led,
    Firmware,
    Other,
};

// A 4-byte cell in a property value that refers to another node.
struct Ref {
    size_t cell_index = 0; // which cell holds the phandle
    int node = -1;         // target node index, -1 when unresolved
};

// One property as it appears in the dtb.  `raw` holds the exact value bytes
// (big-endian, as stored in the blob); `lines` holds the decoded display.
struct Property {
    std::string name;
    std::vector<uint8_t> raw;
    ValueKind kind = ValueKind::Bytes;
    std::string kind_reason;        // why this kind was chosen
    std::vector<std::string> lines; // decoded display lines
    std::vector<Ref> refs;          // resolvable node references
};

// One device-tree node.  Nodes are stored in a vector and refer to each other
// by index, so growing the arena never invalidates a reference.
struct Node {
    std::string name;                         // includes the unit address, e.g. "serial@ff0000"
    std::string path;                         // absolute path, e.g. "/soc/serial@ff0000"
    int depth = 0;                            // root is 0
    int parent = -1;                          // index into Tree::nodes; root is -1
    std::vector<int> children;                // indices, in dtb order
    std::vector<Property> props;              // in dtb order
    bool disabled = false;                    // status == "disabled"
    int phandle = -1;                         // this node's phandle value; -1 when absent
    DeviceKind kind = DeviceKind::Structural; // filled by dtb::classify()
};

struct Tree {
    std::vector<Node> nodes;                       // arena; nodes[0] is always the root
    std::unordered_map<uint32_t, int> phandle_map; // phandle value -> node index
    std::string source;                            // file path or "<stdin>"
    size_t total_props = 0;
};

} // namespace dtb