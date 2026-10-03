#pragma once
#include <Windows.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace hwid {
struct WmiValue { std::string text, error; };
using WmiObject = std::map<std::wstring, WmiValue>;
class ComApartment {
    bool initialized_ = false;
public:
    ComApartment();
    ~ComApartment();
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
};
class WmiClient {
    ComApartment apartment_;
    Microsoft::WRL::ComPtr<IWbemServices> services_;
public:
    explicit WmiClient(std::wstring_view nameSpace = L"ROOT\\CIMV2");
    std::vector<WmiObject> query(std::wstring_view wql, const std::vector<std::wstring>& properties);
    WmiObject readOnlyMethod(std::wstring_view objectPath, std::wstring_view method, const std::vector<std::wstring>& properties);
};
}
