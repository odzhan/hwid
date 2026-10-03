#include "Collectors.h"
#include "windows/DeviceEnumerator.h"
#include "common/Error.h"
#include "common/Strings.h"
#include "common/WinHandle.h"
#include <winioctl.h>
#include <initguid.h>
#include <ntddstor.h>
#include <cstring>
namespace hwid {
namespace {
std::string busName(STORAGE_BUS_TYPE bus) {
    switch (bus) {
    case BusTypeNvme: return "NVMe"; case BusTypeSata: return "SATA"; case BusTypeAta: return "ATA";
    case BusTypeScsi: return "SCSI"; case BusTypeUsb: return "USB"; case BusTypeRAID: return "RAID";
    case BusTypeSd: return "SD"; case BusTypeMmc: return "MMC/eMMC"; case BusTypeSas: return "SAS";
    case BusTypeVirtual: return "Virtual"; case BusTypeFileBackedVirtual: return "File-backed virtual";
    case BusTypeSpaces: return "Storage Spaces"; default: return "Other (" + std::to_string(bus) + ")";
    }
}
HardwareResults storage(bool sdOnly) {
    const std::string component = sdOnly ? "sdEmmc" : "storage";
    HardwareResults results;
    const auto paths = deviceInterfaces(GUID_DEVINTERFACE_DISK);
    for (const auto& path : paths) {
        auto name = utf8(path);
        try {
            WinHandle handle(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
            if (!handle.valid()) throw WindowsError("CreateFileW(disk)", GetLastError());
            STORAGE_PROPERTY_QUERY query{}; query.PropertyId = StorageDeviceProperty; query.QueryType = PropertyStandardQuery;
            STORAGE_DESCRIPTOR_HEADER header{};
            DWORD returned = 0;
            if (!DeviceIoControl(handle.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &header, sizeof(header), &returned, nullptr))
                throw WindowsError("IOCTL_STORAGE_QUERY_PROPERTY(header)", GetLastError());
            if (returned < sizeof(header) || header.Size < sizeof(STORAGE_DEVICE_DESCRIPTOR) || header.Size > 1024 * 1024)
                throw std::runtime_error("Invalid storage descriptor size");
            std::vector<unsigned char> buffer(header.Size);
            if (!DeviceIoControl(handle.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr))
                throw WindowsError("IOCTL_STORAGE_QUERY_PROPERTY", GetLastError());
            if (returned < sizeof(STORAGE_DEVICE_DESCRIPTOR) || returned > buffer.size()) throw std::runtime_error("Truncated storage descriptor");
            STORAGE_DEVICE_DESCRIPTOR descriptor{};
            std::memcpy(&descriptor, buffer.data(), sizeof(descriptor));
            if (descriptor.Size > returned || descriptor.Size < sizeof(descriptor)) throw std::runtime_error("Inconsistent storage descriptor size");
            buffer.resize(descriptor.Size);
            const auto bus = descriptor.BusType;
            if (sdOnly && bus != BusTypeSd && bus != BusTypeMmc) continue;
            STORAGE_DEVICE_NUMBER number{};
            if (DeviceIoControl(handle.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number, sizeof(number), &returned, nullptr) && returned >= sizeof(number))
                name = "PhysicalDrive" + std::to_string(number.DeviceNumber);
            auto add = [&](std::string type, std::string value, std::string field, bool identity = false) {
                auto r = identifier(component, name, type, value, "IOCTL_STORAGE_QUERY_PROPERTY/STORAGE_DEVICE_DESCRIPTOR." + field,
                    identity ? IdConfidence::InterfaceAssigned : IdConfidence::Informational, true, identity);
                if (identity && r.available) r.error = "Storage-stack-reported serial; bridge, RAID or virtual controller may substitute it. Not an NVMe namespace identifier.";
                if (value.empty()) { r.status = IdStatus::NotExposed; r.error = "Storage driver does not expose this field"; }
                results.push_back(std::move(r));
            };
            add("Bus", busName(bus), "BusType");
            add("Vendor", descriptorString(buffer, descriptor.VendorIdOffset), "VendorIdOffset");
            add("Model", descriptorString(buffer, descriptor.ProductIdOffset), "ProductIdOffset");
            add("Revision", descriptorString(buffer, descriptor.ProductRevisionOffset), "ProductRevisionOffset");
            add("Storage serial", descriptorString(buffer, descriptor.SerialNumberOffset), "SerialNumberOffset", true);
            if (sdOnly) results.push_back(unavailable(component, name, "CID", "Windows storage property query",
                "This storage property does not expose CID. No supported generic CID query is implemented for this controller.", IdStatus::NotSupported));
        } catch (const WindowsError& e) {
            results.push_back(unavailable(component, name, "Storage query", "Windows storage API", e.what(), errorStatus(e.code)));
        } catch (const std::exception& e) {
            results.push_back(unavailable(component, name, "Storage query", "Storage descriptor parser", e.what(), IdStatus::QueryFailed));
        }
    }
    if (sdOnly && results.empty()) results.push_back(unavailable(component, "", "SD/eMMC inventory", "STORAGE_DEVICE_DESCRIPTOR.BusType",
        "No disks advertise SD/MMC bus type. USB readers can hide the underlying card type and CID.", IdStatus::NotPresent));
    return results;
}
}
HardwareResults collectStorage() { return storage(false); }
HardwareResults collectSdEmmc() { return storage(true); }
}
