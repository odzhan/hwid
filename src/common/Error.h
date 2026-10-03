#pragma once
#include "HardwareIdentifier.h"
#include <Windows.h>
#include <stdexcept>
namespace hwid {
std::string windowsError(std::string_view api, DWORD code);
IdStatus errorStatus(DWORD code);
class WindowsError : public std::runtime_error {
public:
    DWORD code;
    WindowsError(std::string_view api, DWORD value) : std::runtime_error(windowsError(api, value)), code(value) {}
};
inline void checkHr(HRESULT hr, std::string_view api) { if (FAILED(hr)) throw WindowsError(api, static_cast<DWORD>(hr)); }
}
