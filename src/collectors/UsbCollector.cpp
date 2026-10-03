#include "Collectors.h"
#include "windows/DeviceEnumerator.h"
#include "common/Strings.h"
#include "common/Error.h"
#include <cfgmgr32.h>
namespace hwid {
HardwareResults collectUsb() {
    DeviceSet set(nullptr, L"USB", DIGCF_PRESENT | DIGCF_ALLCLASSES);
    HardwareResults results;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA device{}; device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(set.get(), index, &device)) {
            auto error = GetLastError();
            if (error == ERROR_NO_MORE_ITEMS) break;
            throw WindowsError("SetupDiEnumDeviceInfo", error);
        }
        std::string name = "USB device " + std::to_string(index);
        try {
            auto friendly = deviceProperty(set.get(), device, SPDRP_FRIENDLYNAME);
            if (friendly.empty()) friendly = deviceProperty(set.get(), device, SPDRP_DEVICEDESC);
            if (!friendly.empty()) name += " (" + utf8(friendly) + ")";
            const auto id = utf8(deviceInstanceId(set.get(), device));
            results.push_back(identifier("usb", name, "Windows device instance ID", id,
                "SetupDiGetDeviceInstanceIdW", IdConfidence::InterfaceAssigned));
            results.push_back(identifier("usb", name, "Manufacturer", utf8(deviceProperty(set.get(), device, SPDRP_MFG)),
                "SetupDiGetDeviceRegistryPropertyW.SPDRP_MFG", IdConfidence::Informational, true, false));
            for (const auto& token : {std::string("VID_"), std::string("PID_")}) {
                const auto pos = id.find(token);
                if (pos != std::string::npos && pos + 8 <= id.size()) results.push_back(identifier("usb", name, token.substr(0, 3), id.substr(pos + 4, 4),
                    "USB device instance ID (device type field)", IdConfidence::Informational, true, false));
            }
            DWORD capabilities = 0, type = 0, required = 0;
            if (!SetupDiGetDeviceRegistryPropertyW(set.get(), &device, SPDRP_CAPABILITIES, &type,
                reinterpret_cast<BYTE*>(&capabilities), sizeof(capabilities), &required)) throw WindowsError("SetupDiGetDeviceRegistryPropertyW(SPDRP_CAPABILITIES)", GetLastError());
            if (type != REG_DWORD || required != sizeof(capabilities)) throw std::runtime_error("Invalid device capabilities property");
            auto last = id.rfind('\\');
            // Only the physical USB devnode, with a bus-declared unique ID, is a serial candidate.
            // Composite interface suffixes and generated port identities remain instance IDs only.
            if ((capabilities & CM_DEVCAP_UNIQUEID) && id.starts_with("USB\\VID_") && id.find("&MI_") == std::string::npos && last != std::string::npos) {
                auto r = identifier("usb", name, "USB serial (bus-reported instance component)", id.substr(last + 1),
                    "SetupDiGetDeviceInstanceIdW + SPDRP_CAPABILITIES.CM_DEVCAP_UNIQUEID", IdConfidence::ManufacturerAssigned);
                r.error = "Inferred from USB bus unique-ID capability; not a direct read of the iSerialNumber descriptor";
                results.push_back(std::move(r));
            } else results.push_back(unavailable("usb", name, "USB serial", "USB devnode capabilities",
                "No unambiguous manufacturer serial exposed; Windows instance ID is not substituted"));
        } catch (const WindowsError& e) {
            results.push_back(unavailable("usb", name, "USB query", "SetupAPI", e.what(), errorStatus(e.code)));
        } catch (const std::exception& e) {
            results.push_back(unavailable("usb", name, "USB query", "SetupAPI", e.what(), IdStatus::QueryFailed));
        }
    }
    return results;
}
}
