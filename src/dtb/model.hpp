#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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

// One property as it appears in the dtb.  `raw` holds the exact value bytes
// (big-endian, as stored in the blob); `lines` holds the decoded display.
struct Property {
    std::string name;
    std::vector<uint8_t> raw;
    ValueKind kind = ValueKind::Bytes;
    std::string kind_reason;        // why this kind was chosen
    std::vector<std::string> lines; // decoded display lines
};

// One device-tree node.  Nodes are stored in a vector and refer to each other
// by index, so growing the arena never invalidates a reference.
struct Node {
    std::string name;            // includes the unit address, e.g. "serial@ff0000"
    std::string path;            // absolute path, e.g. "/soc/serial@ff0000"
    int depth = 0;               // root is 0
    int parent = -1;             // index into Tree::nodes; root is -1
    std::vector<int> children;   // indices, in dtb order
    std::vector<Property> props; // in dtb order
    bool disabled = false;       // status == "disabled"
};

struct Tree {
    std::vector<Node> nodes; // arena; nodes[0] is always the root
    std::string source;      // file path or "<stdin>"
    size_t total_props = 0;
};

} // namespace dtb