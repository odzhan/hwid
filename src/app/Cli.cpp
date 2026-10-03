#include "Cli.h"
#include "common/HardwareIdentifier.h"
#include <algorithm>
#include <stdexcept>
namespace hwid {
Options parseCli(std::span<const std::string_view> args) {
    Options o;
    auto select = [&](std::string_view name) {
        if (!o.component.empty()) throw std::invalid_argument("Specify only one component filter");
        o.component = name == "sd-emmc" ? "sdEmmc" : name;
        if (std::find(components.begin(), components.end(), o.component) == components.end())
            throw std::invalid_argument("Unknown component: " + o.component);
    };
    for (size_t i = 0; i < args.size(); ++i) {
        auto a = args[i];
        if (a == "--help" || a == "-h") o.help = true;
        else if (a == "--json") o.json = true;
        else if (a == "--verbose") o.verbose = true;
        else if (a == "--redact") o.redact = true;
        else if (a == "--component" || a == "--tpm-info") {
            if (a == "--tpm-info") select("tpm");
            else {
                if (++i == args.size()) throw std::invalid_argument("--component requires a name");
                select(args[i]);
            }
        }
        else if (a.starts_with("--") && (a == "--sd-emmc" ||
            std::find(components.begin(), components.end(), a.substr(2)) != components.end())) select(a.substr(2));
        else throw std::invalid_argument("Unknown option: " + std::string(a));
    }
    return o;
}
std::string_view usage() {
    return "hwid - read-only Windows hardware inventory\n"
        "Usage: hwid [--json] [--verbose] [--redact] [--component NAME]\n"
        "Default output: console tables grouped by component and device.\n"
        "Components: system motherboard bios tpm storage network usb gpu sdEmmc cpu\n"
        "Select one collector (omit to collect all):\n"
        "  --system      System UUID, vendor and name\n"
        "  --motherboard Motherboard manufacturer, product and serial\n"
        "  --bios        BIOS manufacturer, version and reported serial\n"
        "  --tpm         TPM presence, state and endorsement fingerprints\n"
        "  --storage     Disk bus, model, revision and serial\n"
        "  --network     Network addresses and adapter metadata\n"
        "  --usb         USB device IDs and serial information\n"
        "  --gpu         GPU identifiers\n"
        "  --sd-emmc     SD/eMMC storage identifiers and CID availability\n"
        "  --cpu         CPU identification and features\n"
        "  --component NAME  Equivalent named component filter\n"
        "  --tpm-info    Alias for --component tpm\n"
        "  --json        Versioned UTF-8 JSON with raw and normalized values\n"
        "  --verbose     Add sources, confidence, raw values and diagnostics below tables\n"
        "  --redact      Mask all values, device names and diagnostic text\n"
        "  --help        Show this help\n"
        "No identifiers are transmitted or saved. Partial failures appear in results.\n"
        "Exit codes: 0 inventory completed (possibly partial), 1 fatal error, 2 usage error.\n";
}
}
