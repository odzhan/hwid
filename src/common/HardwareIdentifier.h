#pragma once
#include <string>
#include <vector>
#include <array>
#include <string_view>

namespace hwid {
enum class IdConfidence { HardwareBacked, ManufacturerAssigned, FirmwareAssigned, InterfaceAssigned, Informational, Unknown };
enum class IdStatus { Available, NotSupported, NotPresent, AccessDenied, NotExposed, Invalid, QueryFailed };
struct HardwareIdentifier {
    std::string component, deviceName, identifierType, rawValue, normalizedValue, source;
    IdConfidence confidence = IdConfidence::Unknown;
    bool potentiallyMutable = true;
    bool available = false;
    std::string error;
    IdStatus status = IdStatus::NotExposed;
};
using HardwareResults = std::vector<HardwareIdentifier>;
inline constexpr std::array<std::string_view, 10> components = {
    "system", "motherboard", "bios", "tpm", "storage", "network", "usb", "gpu", "sdEmmc", "cpu"};
std::string_view toString(IdConfidence value);
std::string_view toString(IdStatus value);
HardwareIdentifier identifier(std::string component, std::string device, std::string type,
    std::string raw, std::string source, IdConfidence confidence, bool mutableValue = true,
    bool validateIdentity = true);
HardwareIdentifier unavailable(std::string component, std::string device, std::string type,
    std::string source, std::string error, IdStatus status = IdStatus::NotExposed);
}
