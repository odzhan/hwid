#include "common/Strings.h"
#include "common/Error.h"
#include "app/Cli.h"
#include "collectors/Collectors.h"
#include "output/Writers.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <array>
namespace {
int checks = 0;
void require(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F fn, const char* message) {
    bool threw = false; try { fn(); } catch (const std::exception&) { threw = true; }
    require(threw, message);
}
}
int main() {
    using namespace hwid;
    try {
        require(trim(std::string(" \t A  B \0\0", 10)) == "A  B", "serial padding/internal spaces");
        for (auto value : {"", " To Be Filled By O.E.M. ", "DEFAULT STRING", "None", "Unknown", "Not Specified", "0000", "00:00:00:00:00:00"})
            require(isPlaceholderHardwareId(value), "placeholder detection");
        require(!isPlaceholderHardwareId("AB00 123"), "legitimate serial");
        require(normalizeUuid(" {4C4C4544-0038-3710-8058-CAC04F123456} ") == "4c4c4544-0038-3710-8058-cac04f123456", "UUID case/braces");
        for (auto value : {"00000000-0000-0000-0000-000000000000", "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF", "xyz", "4C4C4544_0038-3710-8058-CAC04F123456"})
            require(normalizeUuid(value).empty(), "reject malformed UUID");
        unsigned char address[] = {0xaa, 0xbb, 0, 1, 0xee, 0xff};
        require(mac(address) == "AA:BB:00:01:EE:FF", "MAC canonical form");
        require(utf8(L"\u6d4b\u8bd5 \U0001f600") == "\xE6\xB5\x8B\xE8\xAF\x95 \xF0\x9F\x98\x80", "Unicode UTF-8");
        wchar_t invalid[] = {0xd800};
        rejects([&] { utf8({invalid, 1}); }, "reject invalid UTF-16");
        require(jsonQuote(std::string("\"\\\n\r\t\b\f\0", 8)) == "\"\\\"\\\\\\n\\r\\t\\b\\f\\u0000\"", "JSON control escaping");
        std::vector<unsigned char> descriptor = {0, 'A', ' ', 'B', 0};
        require(descriptorString(descriptor, 0).empty(), "absent descriptor offset");
        require(descriptorString(descriptor, 1) == "A B", "bounded descriptor string");
        rejects([&] { descriptorString(descriptor, 9); }, "out of bounds descriptor");
        descriptor.pop_back();
        rejects([&] { descriptorString(descriptor, 1); }, "unterminated descriptor");
        descriptor = {0, 255, 0};
        rejects([&] { descriptorString(descriptor, 1); }, "malformed ASCII descriptor");
        auto invalidId = identifier("bios", "board", "serial", " Default string ", "fixture", IdConfidence::FirmwareAssigned);
        require(!invalidId.available && invalidId.rawValue == " Default string " && invalidId.status == IdStatus::Invalid, "preserve invalid raw value");
        auto denied = collectSafely("storage", []() -> HardwareResults { throw WindowsError("fixture", ERROR_ACCESS_DENIED); });
        require(denied.size() == 1 && denied[0].status == IdStatus::AccessDenied, "access denied classification");
        auto failed = collectSafely("bios", []() -> HardwareResults { throw std::runtime_error("fixture"); });
        require(!failed[0].available && failed[0].status == IdStatus::QueryFailed, "partial failure handling");
        auto absent = collectSafely("usb", [] { return HardwareResults{}; });
        require(absent[0].status == IdStatus::NotPresent, "no-device result");
        HardwareResults fixtures = {
            identifier("storage", "secret-device", "Serial", "secret-one", "fixture", IdConfidence::InterfaceAssigned),
            identifier("storage", "second-device", "Serial", "secret-two", "fixture", IdConfidence::InterfaceAssigned),
            unavailable("tpm", "secret-device", "Ready", "fixture", "secret-error", IdStatus::AccessDenied)};
        std::ostringstream json;
        writeJson(json, fixtures, false);
        require(json.str().find("secret-one") != std::string::npos && json.str().find("secret-two") != std::string::npos, "multiple devices retained");
        require(json.str().find("\"schemaVersion\": 1") != std::string::npos, "schema version");
        std::ostringstream redacted, console;
        writeJson(redacted, fixtures, true); writeConsole(console, fixtures, true, true);
        require(redacted.str().find("secret") == std::string::npos && console.str().find("secret") == std::string::npos, "redact values names and errors");
        std::array<std::string_view, 4> args = {"--json", "--redact", "--component", "sd-emmc"};
        auto options = parseCli(args);
        require(options.json && options.redact && options.component == "sdEmmc", "CLI options");
        for (auto component : components) {
            const auto flag = "--" + std::string(component == "sdEmmc" ? "sd-emmc" : component);
            const std::array<std::string_view, 3> selection = {flag, "--json", "--redact"};
            const auto parsed = parseCli(selection);
            require(parsed.component == component && parsed.json && parsed.redact, "direct collector switch");
        }
        const std::array<std::string_view, 2> conflicting = {"--cpu", "--storage"};
        rejects([&] { parseCli(conflicting); }, "reject conflicting collector switches");
        const std::array<std::string_view, 3> mixedFilters = {"--component", "cpu", "--gpu"};
        rejects([&] { parseCli(mixedFilters); }, "reject mixed collector filters");
        std::array<std::string_view, 1> missing = {"--component"};
        rejects([&] { parseCli(missing); }, "CLI missing value");
        std::array<std::string_view, 2> unknown = {"--component", "nonsense"};
        rejects([&] { parseCli(unknown); }, "CLI unknown component");
        std::array<std::string_view, 1> mutation = {"--create-device-key"};
        rejects([&] { parseCli(mutation); }, "inventory rejects mutation command");
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "Test failed: " << e.what() << '\n'; return 1; }
}
