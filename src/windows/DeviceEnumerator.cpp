#include "DeviceEnumerator.h"
#include "common/Error.h"
#include <algorithm>
namespace hwid {
DeviceSet::DeviceSet(const GUID* klass, const wchar_t* enumerator, DWORD flags)
    : value_(SetupDiGetClassDevsW(klass, enumerator, nullptr, flags)) {
    if (value_ == INVALID_HANDLE_VALUE) throw WindowsError("SetupDiGetClassDevsW", GetLastError());
}
DeviceSet::~DeviceSet() { SetupDiDestroyDeviceInfoList(value_); }
std::vector<std::wstring> deviceInterfaces(const GUID& klass) {
    DeviceSet set(&klass, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    std::vector<std::wstring> paths;
    for (DWORD i = 0;; ++i) {
        SP_DEVICE_INTERFACE_DATA data{}; data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInterfaces(set.get(), nullptr, &klass, i, &data)) {
            auto error = GetLastError();
            if (error == ERROR_NO_MORE_ITEMS) break;
            throw WindowsError("SetupDiEnumDeviceInterfaces", error);
        }
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set.get(), &data, nullptr, 0, &size, nullptr);
        auto error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER) throw WindowsError("SetupDiGetDeviceInterfaceDetailW(size)", error);
        if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 1024 * 1024) throw std::runtime_error("Invalid device interface size");
        std::vector<unsigned char> buffer(size + sizeof(wchar_t), 0);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(set.get(), &data, detail, size, nullptr, nullptr))
            throw WindowsError("SetupDiGetDeviceInterfaceDetailW", GetLastError());
        paths.emplace_back(detail->DevicePath);
    }
    return paths;
}
std::wstring deviceInstanceId(HDEVINFO set, SP_DEVINFO_DATA& device) {
    DWORD size = 0;
    SetupDiGetDeviceInstanceIdW(set, &device, nullptr, 0, &size);
    auto error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER) throw WindowsError("SetupDiGetDeviceInstanceIdW(size)", error);
    if (!size || size > 32768) throw std::runtime_error("Invalid device instance ID size");
    std::vector<wchar_t> buffer(size + 1, 0);
    if (!SetupDiGetDeviceInstanceIdW(set, &device, buffer.data(), size, nullptr)) throw WindowsError("SetupDiGetDeviceInstanceIdW", GetLastError());
    return buffer.data();
}
std::wstring deviceProperty(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property) {
    DWORD size = 0, type = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type, nullptr, 0, &size);
    auto error = GetLastError();
    if (error == ERROR_INVALID_DATA) return {};
    if (error != ERROR_INSUFFICIENT_BUFFER) throw WindowsError("SetupDiGetDeviceRegistryPropertyW(size)", error);
    if (size > 1024 * 1024 || size % sizeof(wchar_t)) throw std::runtime_error("Invalid device property size");
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type, reinterpret_cast<BYTE*>(buffer.data()), size, nullptr))
        throw WindowsError("SetupDiGetDeviceRegistryPropertyW", GetLastError());
    if (type != REG_SZ && type != REG_EXPAND_SZ) throw std::runtime_error("Device property is not a string");
    return buffer.data();
}
}
