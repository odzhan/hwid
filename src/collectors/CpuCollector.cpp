#include "Collectors.h"
#include "common/Strings.h"
#include "windows/WmiClient.h"
#include <intrin.h>
#include <cstring>
namespace hwid {
HardwareResults collectCpu() {
    HardwareResults results;
    auto add = [&](std::string type, std::string value, std::string source) {
        results.push_back(identifier("cpu", "Current logical processor", type, value, source, IdConfidence::Informational, true, false));
    };
#if defined(_M_X64) || defined(_M_IX86)
    int regs[4]{};
    __cpuid(regs, 0);
    const int maxLeaf = regs[0];
    char vendor[13]{};
    std::memcpy(vendor, &regs[1], 4); std::memcpy(vendor + 4, &regs[3], 4); std::memcpy(vendor + 8, &regs[2], 4);
    add("Vendor", vendor, "CPUID leaf 0");
    if (maxLeaf >= 1) {
        __cpuidex(regs, 1, 0);
        auto eax = static_cast<unsigned>(regs[0]);
        auto family = (eax >> 8) & 15;
        auto model = (eax >> 4) & 15;
        if (family == 6 || family == 15) model |= ((eax >> 16) & 15) << 4;
        if (family == 15) family += (eax >> 20) & 255;
        add("Family", std::to_string(family), "CPUID.1.EAX");
        add("Model", std::to_string(model), "CPUID.1.EAX");
        add("Stepping", std::to_string(eax & 15), "CPUID.1.EAX");
        add("Feature bits ECX", hexNumber(static_cast<unsigned>(regs[2]), 8), "CPUID.1.ECX");
        add("Feature bits EDX", hexNumber(static_cast<unsigned>(regs[3]), 8), "CPUID.1.EDX");
    }
    __cpuid(regs, static_cast<int>(0x80000000u));
    if (static_cast<unsigned>(regs[0]) >= 0x80000004u) {
        char brand[49]{};
        for (unsigned i = 0; i < 3; ++i) { __cpuid(regs, static_cast<int>(0x80000002u + i)); std::memcpy(brand + i * 16, regs, 16); }
        add("Brand", std::string(brand, 48), "CPUID extended brand leaves");
    }
#else
    results.push_back(unavailable("cpu", "", "CPUID", "Compiler architecture", "CPUID is only implemented on x86/x64", IdStatus::NotSupported));
#endif
    auto wmi = collectSafely("cpu", [] {
        WmiClient client;
        HardwareResults rows;
        for (const auto& r : client.query(L"SELECT DeviceID, Name, ProcessorId FROM Win32_Processor", {L"DeviceID", L"Name", L"ProcessorId"})) {
            auto device = r.at(L"DeviceID").text;
            for (auto property : {L"Name", L"ProcessorId"}) {
                const auto& value = r.at(property);
                if (!value.error.empty()) rows.push_back(unavailable("cpu", device, utf8(property), "Win32_Processor." + utf8(property), value.error));
                else rows.push_back(identifier("cpu", device, utf8(property), value.text, "Win32_Processor." + utf8(property), IdConfidence::Informational, true, false));
            }
        }
        return rows;
    });
    results.insert(results.end(), wmi.begin(), wmi.end());
    results.push_back(unavailable("cpu", "", "Unique CPU serial", "x86/x64 CPU identification semantics",
        "A generally usable unique CPU serial is not exposed on modern x86/x64 PCs. CPUID and ProcessorId are identification data, not unique serials.", IdStatus::NotSupported));
    return results;
}
}
