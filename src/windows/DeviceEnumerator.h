#pragma once
#include <Windows.h>
#include <setupapi.h>
#include <string>
#include <vector>
namespace hwid {
class DeviceSet {
    HDEVINFO value_;
public:
    DeviceSet(const GUID* klass, const wchar_t* enumerator, DWORD flags);
    ~DeviceSet();
    DeviceSet(const DeviceSet&) = delete;
    DeviceSet& operator=(const DeviceSet&) = delete;
    HDEVINFO get() const { return value_; }
};
std::vector<std::wstring> deviceInterfaces(const GUID& klass);
std::wstring deviceInstanceId(HDEVINFO set, SP_DEVINFO_DATA& device);
std::wstring deviceProperty(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property);
}
