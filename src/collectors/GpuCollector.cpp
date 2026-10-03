#include "Collectors.h"
#include "common/Error.h"
#include "common/Strings.h"
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <memory>
#include <cstring>
namespace hwid {
namespace {
class IGpuIdentityProvider {
public:
    virtual ~IGpuIdentityProvider() = default;
    virtual HardwareResults collect() = 0;
};
class DxgiGpuProvider final : public IGpuIdentityProvider {
public:
    HardwareResults collect() override {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        checkHr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        HardwareResults results;
        for (UINT i = 0;; ++i) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            auto hr = factory->EnumAdapters1(i, &adapter);
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            checkHr(hr, "IDXGIFactory1::EnumAdapters1");
            DXGI_ADAPTER_DESC1 desc{};
            hr = adapter->GetDesc1(&desc);
            if (FAILED(hr)) {
                results.push_back(unavailable("gpu", "DXGI adapter " + std::to_string(i), "Adapter description", "IDXGIAdapter1::GetDesc1", windowsError("GetDesc1", hr), errorStatus(hr)));
                continue;
            }
            auto name = "DXGI " + std::to_string(i) + " (" + utf8(desc.Description) + ")";
            auto add = [&](std::string type, std::string value, std::string field) {
                results.push_back(identifier("gpu", name, type, value, "DXGI_ADAPTER_DESC1." + field, IdConfidence::Informational, true, false));
            };
            add("Vendor ID", hexNumber(desc.VendorId, 4), "VendorId");
            add("Device ID", hexNumber(desc.DeviceId, 4), "DeviceId");
            add("Subsystem ID", hexNumber(desc.SubSysId, 8), "SubSysId");
            add("Revision", hexNumber(desc.Revision, 2), "Revision");
            add("Adapter LUID (local, transient)", hexNumber(static_cast<ULONG>(desc.AdapterLuid.HighPart), 8) + hexNumber(desc.AdapterLuid.LowPart, 8), "AdapterLuid");
            add("Software adapter", (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? "true" : "false", "Flags");
            results.push_back(unavailable("gpu", name, "Physical GPU UUID/serial", "DXGI",
                "DXGI does not expose a physical GPU serial/UUID. PCI IDs identify a model; LUID is only locally unique.", IdStatus::NotSupported));
        }
        return results;
    }
};
// Minimal stable NVML C ABI; no build-time dependency on NVIDIA headers or libraries.
class NvidiaGpuProvider final : public IGpuIdentityProvider {
    using Device = struct nvmlDevice_st*;
    using Init = int(__cdecl*)();
    using Shutdown = int(__cdecl*)();
    using Count = int(__cdecl*)(unsigned*);
    using GetDevice = int(__cdecl*)(unsigned, Device*);
    using GetString = int(__cdecl*)(Device, char*, unsigned);
    HMODULE module_ = nullptr;
    Shutdown shutdown_ = nullptr;
    bool initialized_ = false;
    template<class T> T symbol(const char* name) {
        auto address = GetProcAddress(module_, name);
        // GetProcAddress is Windows' documented runtime symbol resolution mechanism.
        T fn{};
        static_assert(sizeof(fn) == sizeof(address));
        std::memcpy(&fn, &address, sizeof(fn));
        return fn;
    }
public:
    ~NvidiaGpuProvider() override { if (initialized_ && shutdown_) shutdown_(); if (module_) FreeLibrary(module_); }
    HardwareResults collect() override {
        module_ = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_) return {unavailable("gpu", "NVIDIA provider", "NVML UUID", "NVML (optional)",
            windowsError("LoadLibraryExW(System32/nvml.dll)", GetLastError()), IdStatus::NotSupported)};
        auto init = symbol<Init>("nvmlInit_v2"); shutdown_ = symbol<Shutdown>("nvmlShutdown");
        auto count = symbol<Count>("nvmlDeviceGetCount_v2");
        auto device = symbol<GetDevice>("nvmlDeviceGetHandleByIndex_v2");
        auto uuid = symbol<GetString>("nvmlDeviceGetUUID");
        auto name = symbol<GetString>("nvmlDeviceGetName");
        auto serial = symbol<GetString>("nvmlDeviceGetSerial");
        if (!init || !shutdown_ || !count || !device || !uuid) return {unavailable("gpu", "NVIDIA provider", "NVML UUID", "NVML", "Required NVML entry points unavailable", IdStatus::NotSupported)};
        auto failure = [](std::string deviceName, std::string field, std::string api, int code) {
            return unavailable("gpu", deviceName, field, api, api + ": NVML return code " + std::to_string(code),
                code == 4 ? IdStatus::AccessDenied : code == 3 ? IdStatus::NotSupported : IdStatus::QueryFailed);
        };
        int code = init();
        if (code) return {failure("NVIDIA provider", "NVML initialization", "nvmlInit_v2", code)};
        initialized_ = true;
        unsigned n = 0;
        code = count(&n);
        if (code) return {failure("NVIDIA provider", "Device count", "nvmlDeviceGetCount_v2", code)};
        if (n > 4096) throw std::runtime_error("Invalid NVML device count");
        HardwareResults results;
        for (unsigned i = 0; i < n; ++i) {
            Device gpu = nullptr;
            auto label = "NVML device " + std::to_string(i);
            code = device(i, &gpu);
            if (code) { results.push_back(failure(label, "Device", "nvmlDeviceGetHandleByIndex_v2", code)); continue; }
            char description[256]{};
            if (name && name(gpu, description, sizeof(description)) == 0) { description[255] = 0; label += " (" + std::string(description) + ")"; }
            auto read = [&](GetString fn, std::string field, std::string api) {
                if (!fn) { results.push_back(unavailable("gpu", label, field, api, "NVML entry point unavailable", IdStatus::NotSupported)); return; }
                char value[256]{};
                auto rc = fn(gpu, value, sizeof(value));
                if (rc) results.push_back(failure(label, field, api, rc));
                else { value[255] = 0; results.push_back(identifier("gpu", label, field, value, api, IdConfidence::ManufacturerAssigned)); }
            };
            read(uuid, "NVIDIA GPU UUID", "nvmlDeviceGetUUID");
            read(serial, "NVIDIA board serial", "nvmlDeviceGetSerial");
        }
        return results;
    }
};
}
HardwareResults collectGpu() {
    DxgiGpuProvider dxgi;
    NvidiaGpuProvider nvidia;
    auto results = collectSafely("gpu", [&] { return dxgi.collect(); });
    auto vendor = collectSafely("gpu", [&] { return nvidia.collect(); });
    results.insert(results.end(), vendor.begin(), vendor.end());
    results.push_back(unavailable("gpu", "AMD/Intel providers", "Vendor UUID/serial", "Optional vendor provider",
        "AMD and Intel vendor identity backends are not implemented; DXGI metadata remains available.", IdStatus::NotSupported));
    return results;
}
}
