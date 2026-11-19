#include "dtb/classify.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "dtb/model.hpp"

namespace dtb {
namespace {

struct Rule {
    std::string_view pattern;
    DeviceKind kind;
};

// `compatible` substrings.  Order matters: the first match wins, so the more
// specific patterns must come first (a video-in bus is also a "simple-bus").
constexpr Rule kCompatibleRules[] = {
    {"operating-points", DeviceKind::Structural},
    {"idle-state", DeviceKind::Structural},
    {"psci", DeviceKind::Firmware},
    {"optee", DeviceKind::Firmware},
    {"android,firmware", DeviceKind::Firmware},
    {"scpi", DeviceKind::Firmware},
    {"arm,gic", DeviceKind::Interrupt},
    {"wakeupgen", DeviceKind::Interrupt},
    {"sun8i-nmi", DeviceKind::Interrupt},
    {"interrupt-controller", DeviceKind::Interrupt},
    {"iommu", DeviceKind::Iommu},
    {"arm,cortex", DeviceKind::Cpu},
    {"armv8-pmuv3", DeviceKind::Cpu},
    {"dsufreq", DeviceKind::Cpu},
    {"allwinner,npu", DeviceKind::Cpu},
    {"cpucfg", DeviceKind::Cpu},
    {"allwinner,dram", DeviceKind::Memory},
    {"mbus", DeviceKind::Memory},
    {"mmio-sram", DeviceKind::Memory},
    {"shmem", DeviceKind::Memory},
    {"regulator", DeviceKind::Power},
    {"x-powers", DeviceKind::Power},
    {"power-controller", DeviceKind::Power},
    {"pwrseq", DeviceKind::Power},
    {"sun60iw2-pck", DeviceKind::Power},
    {"ccu", DeviceKind::Clock},
    {"fixed-clock", DeviceKind::Clock},
    {"clock-ddr", DeviceKind::Clock},
    {"clock_ddr", DeviceKind::Clock},
    {"pll", DeviceKind::Clock},
    {"pinctrl", DeviceKind::Pinctrl},
    {"sunxi-mmc", DeviceKind::Storage},
    {"sunxi-ufs", DeviceKind::Storage},
    {"sdhci", DeviceKind::Storage},
    {"nand", DeviceKind::Storage},
    {"uart", DeviceKind::Serial},
    {"sunxi-twi", DeviceKind::I2c},
    {"rsb", DeviceKind::I2c},
    {"i2c", DeviceKind::I2c},
    {"sunxi-spi", DeviceKind::Spi},
    {"spi-nor", DeviceKind::Spi},
    {"spidev", DeviceKind::Spi},
    {"sunxi-udc", DeviceKind::Usb},
    {"sunxi-ehci", DeviceKind::Usb},
    {"sunxi-ohci", DeviceKind::Usb},
    {"dwc3", DeviceKind::Usb},
    {"plat-phy", DeviceKind::Usb},
    {"otg", DeviceKind::Usb},
    {"usb", DeviceKind::Usb},
    {"gmac", DeviceKind::Network},
    {"dwmac", DeviceKind::Network},
    {"stmmac", DeviceKind::Network},
    {"sunxi-uio", DeviceKind::Network},
    {"rfkill", DeviceKind::Wireless},
    {"fmac", DeviceKind::Wireless},
    {"addr_mgt", DeviceKind::Wireless},
    {"btlpm", DeviceKind::Wireless},
    {"wlan", DeviceKind::Wireless},
    {"bluetooth", DeviceKind::Wireless},
    {"vin-media", DeviceKind::Camera},
    {"vin-core", DeviceKind::Camera},
    {"sunxi-csi", DeviceKind::Camera},
    {"sunxi-isp", DeviceKind::Camera},
    {"sunxi-scaler", DeviceKind::Camera},
    {"sunxi-mipi", DeviceKind::Camera},
    {"csi", DeviceKind::Camera},
    {"cedar", DeviceKind::Video},
    {"video-codec", DeviceKind::Video},
    {"video-engine", DeviceKind::Video},
    {"deinterlace", DeviceKind::Video},
    {"img,gpu", DeviceKind::Gpu},
    {"mali", DeviceKind::Gpu},
    {"g2d", DeviceKind::Gpu},
    {"snd-", DeviceKind::Audio},
    {"audio", DeviceKind::Audio},
    {"spdif", DeviceKind::Audio},
    {"codec", DeviceKind::Audio},
    {"es8388", DeviceKind::Audio},
    {"ac101", DeviceKind::Audio},
    {"i2s", DeviceKind::Audio},
    {"owa", DeviceKind::Audio},
    {"dmic", DeviceKind::Audio},
    {"tdm", DeviceKind::Audio},
    {"panel", DeviceKind::Panel},
    {"backlight", DeviceKind::Panel},
    {"display-engine", DeviceKind::Display},
    {"sunxi-drm", DeviceKind::Display},
    {"tcon", DeviceKind::Display},
    {"dsi", DeviceKind::Display},
    {"drm-edp", DeviceKind::Display},
    {"sunxi-hdmi", DeviceKind::Display},
    {"hdmi", DeviceKind::Display},
    {"de2-mixer", DeviceKind::Display},
    {"de2-rotate", DeviceKind::Display},
    {"allwinner,rgb", DeviceKind::Display},
    {"lvds", DeviceKind::Display},
    {"sunxi-pwm", DeviceKind::Pwm},
    {"gpadc", DeviceKind::Adc},
    {"lradc", DeviceKind::Adc},
    {"keyboard_1350mv", DeviceKind::Adc},
    {"-ir", DeviceKind::Adc},
    {"irrx", DeviceKind::Adc},
    {"irtx", DeviceKind::Adc},
    {"touchscreen", DeviceKind::Adc},
    {"goodix", DeviceKind::Adc},
    {"gpio-keys", DeviceKind::Adc},
    {"sunxi-leds", DeviceKind::Led},
    {"gpio-leds", DeviceKind::Led},
    {"ths", DeviceKind::Thermal},
    {"sun50i-timer", DeviceKind::Timer},
    {"armv8-timer", DeviceKind::Timer},
    {"rtc-v", DeviceKind::Timer},
    {"wdt", DeviceKind::Timer},
    {"watchdog", DeviceKind::Timer},
    {"dma-v", DeviceKind::Dma},
    {"msgbox", DeviceKind::Dma},
    {"hwspinlock", DeviceKind::Dma},
    {"rproc", DeviceKind::Dma},
    {"sram_ctrl", DeviceKind::Dma},
    {"nsi-v", DeviceKind::Dma},
    {"sunxi-ce", DeviceKind::Crypto},
    {"sunxi-sid", DeviceKind::Crypto},
    {"-sid", DeviceKind::Crypto},
    {"crypto", DeviceKind::Crypto},
    {"simple-bus", DeviceKind::Bus},
    {"pcie", DeviceKind::Bus},
    {"cci-400", DeviceKind::Bus},
};

// `device_type` values.  Allwinner BSP blobs use this property as a name, for
// example `sdc0` or `s_twi1`, so the match is on the prefix.
constexpr Rule kDeviceTypeRules[] = {
    {"cpu", DeviceKind::Cpu},          {"memory", DeviceKind::Memory},
    {"network", DeviceKind::Network},  {"display", DeviceKind::Display},
    {"uart", DeviceKind::Serial},      {"twi", DeviceKind::I2c},
    {"i2c", DeviceKind::I2c},          {"spi", DeviceKind::Spi},
    {"sdc", DeviceKind::Storage},      {"mmc", DeviceKind::Storage},
    {"usb", DeviceKind::Usb},          {"gpu", DeviceKind::Gpu},
    {"npu", DeviceKind::Cpu},          {"pci", DeviceKind::Bus},
    {"ufs", DeviceKind::Storage},      {"rtc", DeviceKind::Timer},
    {"timer", DeviceKind::Timer},      {"pinctrl", DeviceKind::Pinctrl},
    {"ethernet", DeviceKind::Network}, {"gmac", DeviceKind::Network},
};

// Node name prefixes (the part before '@'), lowercased.  Used only when
// `compatible` and `device_type` give no answer.
constexpr Rule kNameRules[] = {
    {"uart", DeviceKind::Serial},
    {"twi", DeviceKind::I2c},
    {"i2c", DeviceKind::I2c},
    {"spi", DeviceKind::Spi},
    {"sdmmc", DeviceKind::Storage},
    {"sdc", DeviceKind::Storage},
    {"mmc", DeviceKind::Storage},
    {"ufs", DeviceKind::Storage},
    {"usb", DeviceKind::Usb},
    {"ethernet", DeviceKind::Network},
    {"interrupt-controller", DeviceKind::Interrupt},
    {"iommu", DeviceKind::Iommu},
    {"pinctrl", DeviceKind::Pinctrl},
    {"pwm", DeviceKind::Pwm},
    {"s_pwm", DeviceKind::Pwm},
    {"rtc", DeviceKind::Timer},
    {"watchdog", DeviceKind::Timer},
    {"timer", DeviceKind::Timer},
    {"dma-controller", DeviceKind::Dma},
    {"msgbox", DeviceKind::Dma},
    {"hwspinlock", DeviceKind::Dma},
    {"gpu", DeviceKind::Gpu},
    {"g2d", DeviceKind::Gpu},
    {"npu", DeviceKind::Cpu},
    {"de", DeviceKind::Display},
    {"tcon", DeviceKind::Display},
    {"dsi", DeviceKind::Display},
    {"hdmi", DeviceKind::Display},
    {"edp", DeviceKind::Display},
    {"lvds", DeviceKind::Display},
    {"rgb", DeviceKind::Display},
    {"i2s", DeviceKind::Audio},
    {"owa", DeviceKind::Audio},
    {"dmic", DeviceKind::Audio},
    {"soundcard", DeviceKind::Audio},
    {"codec", DeviceKind::Audio},
    {"es8388", DeviceKind::Audio},
    {"ac101", DeviceKind::Audio},
    {"ths", DeviceKind::Thermal},
    {"gpadc", DeviceKind::Adc},
    {"lradc", DeviceKind::Adc},
    {"irrx", DeviceKind::Adc},
    {"touchscreen", DeviceKind::Adc},
    {"powerkey", DeviceKind::Adc},
    {"led", DeviceKind::Led},
    {"syscon", DeviceKind::Bus},
    {"bluetooth", DeviceKind::Wireless},
    {"wifi", DeviceKind::Wireless},
    {"camera", DeviceKind::Camera},
    {"dram", DeviceKind::Memory},
};

// Name substrings, used for the names that do not start with their type.
constexpr Rule kNameContainsRules[] = {
    {"clk", DeviceKind::Clock},
    {"clock", DeviceKind::Clock},
    {"freq", DeviceKind::Clock},
};

// Nodes that are data, not devices.  Matched against the base name.
constexpr std::string_view kStructuralNames[] = {
    "aliases",
    "chosen",
    "__symbols__",
    "reserved-memory",
    "firmware",
    "arisc_config",
    "vf_mapping_table",
    "opp_table",
    "auto_print",
    "dump_reg",
    "cpus",
    "cpu-map",
    "idle-states",
    "thermal-zones",
    "port",
    "ports",
    "endpoint",
    "display-timings",
    "trips",
    "trip-point",
    "cooling-maps",
    "thermal-idle",
    "stmmac-axi-config",
    "soc",
};

std::string lower(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(
            character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                 : character
        );
    }
    return lowered;
}

std::string base_name(std::string_view name) {
    const size_t at = name.find('@');
    return lower(name.substr(0, at));
}

// Value of a property as lowercased text; NUL separators become spaces.
std::string property_text(const Node& node, std::string_view name) {
    for (const Property& prop : node.props) {
        if (prop.name != name) continue;
        std::string text;
        text.reserve(prop.raw.size());
        for (const uint8_t byte : prop.raw) {
            text.push_back(byte == 0 ? ' ' : static_cast<char>(byte));
        }
        return lower(text);
    }
    return {};
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool has_property(const Node& node, std::string_view name) {
    for (const Property& prop : node.props) {
        if (prop.name == name) return true;
    }
    return false;
}

// Pin groups carry `pins`/`function` and no `compatible`.  Their names look like
// devices (`uart0@0`, `pwm0_1@0`), so they must be rejected before the name rules.
bool is_pin_group(const Node& node) {
    return has_property(node, "pins") || has_property(node, "allwinner,pins")
        || has_property(node, "function");
}

bool is_structural(const Node& node, std::string_view base, std::string_view compatible) {
    if (node.depth == 0) return true; // the root is a container
    for (const std::string_view name : kStructuralNames) {
        if (base == name) return true;
    }
    if (base.find("_test") != std::string_view::npos) return true; // pd_*_test, pinctrl_test
    if (base.size() > 4 && base.ends_with("_zone")) return true;   // thermal zones
    if (starts_with(base, "opp@") || starts_with(base, "opp-")) return true;
    if (compatible.find("operating-points") != std::string_view::npos) return true;
    return false;
}

template <size_t N>
std::optional<DeviceKind> match_rules(
    const Rule (&rules)[N], std::string_view text, bool prefix_only
) {
    if (text.empty()) return std::nullopt;
    for (const Rule& rule : rules) {
        const bool hit = prefix_only ? starts_with(text, rule.pattern)
                                     : text.find(rule.pattern) != std::string_view::npos;
        if (hit) return rule.kind;
    }
    return std::nullopt;
}

} // namespace

DeviceKind classify_node(const Node& node) {
    const std::string base = base_name(node.name);
    const std::string compatible = property_text(node, "compatible");
    const std::string device_type = property_text(node, "device_type");

    if (is_structural(node, base, compatible)) return DeviceKind::Structural;
    if (compatible.empty() && device_type.empty() && is_pin_group(node)) {
        return DeviceKind::Structural;
    }

    if (const auto kind = match_rules(kCompatibleRules, compatible, false)) return *kind;
    if (const auto kind = match_rules(kDeviceTypeRules, device_type, true)) return *kind;
    if (const auto kind = match_rules(kNameRules, base, true)) return *kind;
    if (const auto kind = match_rules(kNameContainsRules, base, false)) return *kind;

    // Anything that still advertises itself as bindable is a device we have no
    // rule for; everything else is structure.
    if (!compatible.empty() || !device_type.empty()) return DeviceKind::Other;
    return DeviceKind::Structural;
}

void classify(Tree& tree) {
    for (Node& node : tree.nodes)
        node.kind = classify_node(node);
}

std::string_view device_kind_name(DeviceKind kind) {
    switch (kind) {
    case DeviceKind::Structural: return "结构";
    case DeviceKind::Cpu: return "处理器";
    case DeviceKind::Bus: return "总线";
    case DeviceKind::Memory: return "内存";
    case DeviceKind::Interrupt: return "中断控制";
    case DeviceKind::Iommu: return "IOMMU";
    case DeviceKind::Clock: return "时钟";
    case DeviceKind::Power: return "电源";
    case DeviceKind::Pinctrl: return "管脚";
    case DeviceKind::Storage: return "存储";
    case DeviceKind::Serial: return "串口";
    case DeviceKind::I2c: return "I2C";
    case DeviceKind::Spi: return "SPI";
    case DeviceKind::Usb: return "USB";
    case DeviceKind::Network: return "以太网";
    case DeviceKind::Wireless: return "无线蓝牙";
    case DeviceKind::Display: return "显示";
    case DeviceKind::Panel: return "面板背光";
    case DeviceKind::Gpu: return "GPU";
    case DeviceKind::Video: return "视频编解码";
    case DeviceKind::Camera: return "摄像头";
    case DeviceKind::Audio: return "音频";
    case DeviceKind::Pwm: return "PWM";
    case DeviceKind::Adc: return "ADC 按键";
    case DeviceKind::Thermal: return "温度传感";
    case DeviceKind::Timer: return "定时 RTC";
    case DeviceKind::Dma: return "DMA 与 IPC";
    case DeviceKind::Crypto: return "加密标识";
    case DeviceKind::Led: return "LED";
    case DeviceKind::Firmware: return "固件";
    case DeviceKind::Other: return "其它";
    }
    return "其它";
}

} // namespace dtb