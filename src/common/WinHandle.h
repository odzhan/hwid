#pragma once
#include <Windows.h>
namespace hwid {
class WinHandle {
    HANDLE value_;
public:
    explicit WinHandle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~WinHandle() { if (valid()) CloseHandle(value_); }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return value_; }
};
}
