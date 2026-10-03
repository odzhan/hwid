#include "Collectors.h"
#include "windows/WmiClient.h"
#include "common/Strings.h"
namespace hwid {
namespace {
HardwareResults collect(std::string component, std::wstring klass, std::vector<std::wstring> fields, std::wstring identity, std::string type) {
    WmiClient wmi;
    std::wstring query = L"SELECT ";
    for (const auto& f : fields) { if (query != L"SELECT ") query += L", "; query += f; }
    auto rows = wmi.query(query + L" FROM " + klass, fields);
    HardwareResults results;
    size_t index = 0;
    for (const auto& row : rows) {
        auto device = component + " " + std::to_string(index++);
        for (const auto& field : fields) {
            const auto& v = row.at(field);
            auto source = utf8(klass) + "." + utf8(field);
            auto label = field == identity ? type : utf8(field);
            if (!v.error.empty()) { results.push_back(unavailable(component, device, label, source, v.error)); continue; }
            auto r = identifier(component, device, label, v.text, source,
                field == identity ? IdConfidence::FirmwareAssigned : IdConfidence::Informational, true, field == identity);
            if (field == L"UUID") {
                r.normalizedValue = normalizeUuid(r.rawValue);
                r.available = !r.normalizedValue.empty();
                r.status = r.available ? IdStatus::Available : IdStatus::Invalid;
                r.error = r.available ? "" : "Invalid, all-zero or all-FF SMBIOS UUID";
            }
            results.push_back(std::move(r));
        }
    }
    return results;
}
}
HardwareResults collectSystem() { return collect("system", L"Win32_ComputerSystemProduct", {L"UUID", L"Vendor", L"Name"}, L"UUID", "SMBIOS System UUID"); }
HardwareResults collectMotherboard() { return collect("motherboard", L"Win32_BaseBoard", {L"Manufacturer", L"Product", L"SerialNumber"}, L"SerialNumber", "Motherboard serial"); }
HardwareResults collectBios() { return collect("bios", L"Win32_BIOS", {L"Manufacturer", L"SMBIOSBIOSVersion", L"SerialNumber"}, L"SerialNumber", "BIOS-reported system serial"); }
}
