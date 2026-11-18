#include <argparse/argparse.hpp>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "dtb/decode.hpp"
#include "dtb/loader.hpp"
#include "dtb/model.hpp"
#include "ui/app.hpp"

namespace {

constexpr const char* kVersion = "0.2.0";
constexpr size_t kReadChunk = 8192;

std::vector<uint8_t> read_stream(std::istream& in, bool& ok) {
    std::vector<uint8_t> data;
    char buffer[kReadChunk];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const std::streamsize count = in.gcount();
        if (count <= 0) break;
        const auto* begin = reinterpret_cast<const uint8_t*>(buffer);
        data.insert(data.end(), begin, begin + count);
    }
    ok = !in.bad();
    return data;
}

void dump_property(const dtb::Property& prop, const std::string& indent, std::ostream& out) {
    out << indent << prop.name << " =";
    if (!prop.lines.empty()) out << ' ' << prop.lines.front();
    out << '\n';

    const std::string continuation(indent.size() + prop.name.size() + 3, ' ');
    for (size_t i = 1; i < prop.lines.size(); ++i)
        out << continuation << prop.lines[i] << '\n';

    out << indent << "  # " << dtb::kind_name(prop.kind) << ": " << prop.kind_reason << '\n';
}

void dump_tree(const dtb::Tree& tree, std::ostream& out) {
    for (const dtb::Node& node : tree.nodes) {
        const std::string indent(node.depth * 2, ' ');
        out << indent << node.path << " (depth " << node.depth << ")\n";

        const std::string prop_indent(node.depth * 2 + 2, ' ');
        for (const dtb::Property& prop : node.props)
            dump_property(prop, prop_indent, out);
    }
    out << "\n# nodes: " << tree.nodes.size() << ", properties: " << tree.total_props << '\n';
}

} // namespace

int main(int argc, char** argv) {
    argparse::ArgumentParser program("dtbtui", kVersion);
    program.add_description("Terminal viewer for device-tree blobs.");
    program.add_argument("--dump")
        .help("print the whole tree as plain text instead of opening the TUI")
        .default_value(false)
        .implicit_value(true);
    program.add_argument("path").help("path to a .dtb file, or - to read stdin");

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& error) {
        std::cerr << program;
        std::cerr << "dtbtui: " << error.what() << '\n';
        return 2;
    }

    const std::string path = program.get<std::string>("path");
    const bool dump = program.get<bool>("--dump");

    std::ifstream file;
    std::istream* input = &std::cin;
    if (path != "-") {
        file.open(path, std::ios::binary);
        if (!file) {
            std::cerr << "dtbtui: " << std::strerror(errno) << ": " << path << '\n';
            return 2;
        }
        input = &file;
    }

    bool read_ok = false;
    std::vector<uint8_t> blob = read_stream(*input, read_ok);
    const std::string source = path == "-" ? "<stdin>" : path;
    if (!read_ok) {
        std::cerr << "dtbtui: 读取失败: " << source << '\n';
        return 2;
    }

    dtb::LoadResult result = dtb::load(std::move(blob), source);
    if (!result.tree) {
        if (!dump) return ui::run_error(source, result.error);
        std::cerr << "dtbtui: " << source << ": " << result.error << '\n';
        return 1;
    }

    dtb::Tree tree = std::move(*result.tree);
    dtb::decode(tree);
    if (dump) {
        dump_tree(tree, std::cout);
        return 0;
    }
    return ui::run(tree);
}