#include "WmiClient.h"
#include "common/Error.h"
#include "common/Strings.h"
#include <chrono>
namespace hwid {
namespace {
struct Bstr {
    BSTR value;
    explicit Bstr(std::wstring_view v) : value(SysAllocStringLen(v.data(), static_cast<UINT>(v.size()))) { if (!value) throw std::bad_alloc(); }
    ~Bstr() { SysFreeString(value); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
};
struct Variant {
    VARIANT value;
    Variant() { VariantInit(&value); }
    ~Variant() { VariantClear(&value); }
    Variant(const Variant&) = delete;
    Variant& operator=(const Variant&) = delete;
};
WmiValue read(IWbemClassObject* object, const std::wstring& name) {
    Variant v;
    HRESULT hr = object->Get(name.c_str(), 0, &v.value, nullptr, nullptr);
    if (FAILED(hr)) return {{}, windowsError("IWbemClassObject::Get", static_cast<DWORD>(hr))};
    switch (v.value.vt) {
    case VT_NULL: case VT_EMPTY: return {{}, "WMI property is null or empty"};
    case VT_BSTR: return {utf8({v.value.bstrVal, SysStringLen(v.value.bstrVal)}), {}};
    case VT_BOOL: return {v.value.boolVal == VARIANT_FALSE ? "false" : "true", {}};
    case VT_UI1: return {std::to_string(v.value.bVal), {}};
    case VT_I2: return {std::to_string(v.value.iVal), {}};
    case VT_UI2: return {std::to_string(v.value.uiVal), {}};
    case VT_I4: return {std::to_string(v.value.lVal), {}};
    case VT_UI4: return {std::to_string(v.value.ulVal), {}};
    case VT_I8: return {std::to_string(v.value.llVal), {}};
    case VT_UI8: return {std::to_string(v.value.ullVal), {}};
    default: return {{}, "Unsupported WMI VARIANT type " + std::to_string(v.value.vt)};
    }
}
}
ComApartment::ComApartment() {
    auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr != RPC_E_CHANGED_MODE) { checkHr(hr, "CoInitializeEx"); initialized_ = true; }
}
ComApartment::~ComApartment() { if (initialized_) CoUninitialize(); }
WmiClient::WmiClient(std::wstring_view nameSpace) {
    auto hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (hr != RPC_E_TOO_LATE) checkHr(hr, "CoInitializeSecurity");
    Microsoft::WRL::ComPtr<IWbemLocator> locator;
    checkHr(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)), "CoCreateInstance(WbemLocator)");
    Bstr ns(nameSpace);
    checkHr(locator->ConnectServer(ns.value, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &services_), "IWbemLocator::ConnectServer");
    checkHr(CoSetProxyBlanket(services_.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE), "CoSetProxyBlanket");
}
std::vector<WmiObject> WmiClient::query(std::wstring_view wql, const std::vector<std::wstring>& properties) {
    Bstr language(L"WQL"), statement(wql);
    Microsoft::WRL::ComPtr<IEnumWbemClassObject> enumerator;
    checkHr(services_->ExecQuery(language.value, statement.value, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &enumerator), "IWbemServices::ExecQuery");
    std::vector<WmiObject> result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) throw WindowsError("WMI query timeout", WAIT_TIMEOUT);
        ULONG count = 0;
        Microsoft::WRL::ComPtr<IWbemClassObject> object;
        auto hr = enumerator->Next(1000, 1, &object, &count);
        checkHr(hr, "IEnumWbemClassObject::Next");
        if (count) {
            WmiObject row;
            for (const auto& name : properties) row.emplace(name, read(object.Get(), name));
            result.push_back(std::move(row));
        }
        if (hr == WBEM_S_FALSE) break;
    }
    return result;
}
WmiObject WmiClient::readOnlyMethod(std::wstring_view objectPath, std::wstring_view method, const std::vector<std::wstring>& properties) {
    // Restrict this helper to the single passive TPM method used by inventory.
    if (method != L"IsReady") throw std::invalid_argument("Only the read-only IsReady method is allowed");
    Bstr path(objectPath), name(method);
    Microsoft::WRL::ComPtr<IWbemCallResult> call;
    checkHr(services_->ExecMethod(path.value, name.value, WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, nullptr, nullptr, &call), "IWbemServices::ExecMethod(IsReady)");
    Microsoft::WRL::ComPtr<IWbemClassObject> output;
    auto hr = call->GetResultObject(15000, &output);
    if (hr == WBEM_S_TIMEDOUT) throw WindowsError("Win32_Tpm.IsReady timeout", WAIT_TIMEOUT);
    checkHr(hr, "IWbemCallResult::GetResultObject");
    if (!output) throw std::runtime_error("WMI method returned no result object");
    WmiObject result;
    for (const auto& property : properties) result.emplace(property, read(output.Get(), property));
    return result;
}
}
