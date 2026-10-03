#include "Collectors.h"
#include "common/Error.h"
#include "common/Strings.h"
#include "windows/WmiClient.h"
#include <tbs.h>
#include <ncrypt.h>
#include <bcrypt.h>
namespace hwid {
namespace {
struct Provider {
    NCRYPT_PROV_HANDLE handle = 0;
    ~Provider() { if (handle) NCryptFreeObject(handle); }
};
HardwareIdentifier endorsement(Provider& provider, const wchar_t* property, std::string type) {
    const auto source = "NCryptGetProperty(MS_PLATFORM_CRYPTO_PROVIDER, " + utf8(property) + ") + SHA-256";
    DWORD size = 0;
    auto code = NCryptGetProperty(provider.handle, property, nullptr, 0, &size, NCRYPT_SILENT_FLAG);
    if (code) return unavailable("tpm", "Platform TPM", type, source, windowsError("NCryptGetProperty(size)", code), errorStatus(code));
    if (!size || size > 1024 * 1024) return unavailable("tpm", "Platform TPM", type, source, "Empty or oversized endorsement property", IdStatus::QueryFailed);
    std::vector<unsigned char> blob(size);
    code = NCryptGetProperty(provider.handle, property, blob.data(), size, &size, NCRYPT_SILENT_FLAG);
    if (code) return unavailable("tpm", "Platform TPM", type, source, windowsError("NCryptGetProperty", code), errorStatus(code));
    if (size > blob.size() || !size) return unavailable("tpm", "Platform TPM", type, source, "Invalid endorsement property length", IdStatus::QueryFailed);
    unsigned char digest[32]{};
    auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, blob.data(), size, digest, sizeof(digest));
    if (status < 0) return unavailable("tpm", "Platform TPM", type, source, "BCryptHash NTSTATUS 0x" + hexNumber(static_cast<ULONG>(status), 8), IdStatus::QueryFailed);
    auto result = identifier("tpm", "Platform TPM", type, hex(digest), source, IdConfidence::HardwareBacked);
    result.error = "SHA-256 of the exact provider-returned public blob; not private key material or proof of attestation. Certificate trust is not validated.";
    return result;
}
}
HardwareResults collectTpm() {
    HardwareResults results;
    TPM_DEVICE_INFO info{};
    auto code = Tbsi_GetDeviceInfo(sizeof(info), &info);
    if (code == TBS_SUCCESS) {
        results.push_back(identifier("tpm", "Platform TPM", "Present", "true", "Tbsi_GetDeviceInfo", IdConfidence::Informational, true, false));
        const auto version = info.tpmVersion == TPM_VERSION_20 ? "2.0" : info.tpmVersion == TPM_VERSION_12 ? "1.2" : "Unknown (" + std::to_string(info.tpmVersion) + ")";
        results.push_back(identifier("tpm", "Platform TPM", "Specification version", version, "TPM_DEVICE_INFO.tpmVersion", IdConfidence::Informational, true, false));
        results.push_back(identifier("tpm", "Platform TPM", "Interface type", std::to_string(info.tpmInterfaceType), "TPM_DEVICE_INFO.tpmInterfaceType", IdConfidence::Informational, true, false));
    } else results.push_back(unavailable("tpm", "Platform TPM", "Presence/version", "Tbsi_GetDeviceInfo", windowsError("Tbsi_GetDeviceInfo", code),
        code == TBS_E_TPM_NOT_FOUND ? IdStatus::NotPresent : errorStatus(code)));
    auto state = collectSafely("tpm", [] {
        WmiClient client(L"ROOT\\CIMV2\\Security\\MicrosoftTpm");
        const std::vector<std::wstring> fields = {L"SpecVersion", L"ManufacturerId", L"ManufacturerVersion", L"IsEnabled_InitialValue", L"IsActivated_InitialValue", L"IsOwned_InitialValue"};
        HardwareResults rows;
        for (const auto& row : client.query(L"SELECT SpecVersion, ManufacturerId, ManufacturerVersion, IsEnabled_InitialValue, IsActivated_InitialValue, IsOwned_InitialValue FROM Win32_Tpm", fields)) {
            for (const auto& f : fields) {
                const auto& v = row.at(f);
                auto source = "Win32_Tpm." + utf8(f);
                if (!v.error.empty()) rows.push_back(unavailable("tpm", "Platform TPM", utf8(f), source, v.error));
                else rows.push_back(identifier("tpm", "Platform TPM", utf8(f), v.text, source, IdConfidence::Informational, true, false));
            }
        }
        return rows;
    });
    results.insert(results.end(), state.begin(), state.end());
    auto readiness = collectSafely("tpm", [] {
        WmiClient client(L"ROOT\\CIMV2\\Security\\MicrosoftTpm");
        HardwareResults rows;
        for (const auto& instance : client.query(L"SELECT __PATH FROM Win32_Tpm", {L"__PATH"})) {
            const auto& path = instance.at(L"__PATH");
            if (!path.error.empty()) throw std::runtime_error(path.error);
            // WMI object paths for this local class contain ASCII namespace/class/key text.
            std::wstring objectPath(path.text.begin(), path.text.end());
            auto output = client.readOnlyMethod(objectPath, L"IsReady", {L"ReturnValue", L"IsReady"});
            const auto& returnValue = output.at(L"ReturnValue");
            const auto& ready = output.at(L"IsReady");
            if (!returnValue.error.empty()) throw std::runtime_error(returnValue.error);
            auto result = std::stoul(returnValue.text);
            if (result) throw WindowsError("Win32_Tpm.IsReady", result);
            if (!ready.error.empty()) throw std::runtime_error(ready.error);
            rows.push_back(identifier("tpm", "Platform TPM", "Ready", ready.text, "Win32_Tpm.IsReady", IdConfidence::Informational, true, false));
        }
        return rows;
    });
    results.insert(results.end(), readiness.begin(), readiness.end());
    Provider provider;
    auto status = NCryptOpenStorageProvider(&provider.handle, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (status) results.push_back(unavailable("tpm", "Platform TPM", "Public endorsement information", "Microsoft Platform Crypto Provider", windowsError("NCryptOpenStorageProvider", status), errorStatus(status)));
    else {
        results.push_back(endorsement(provider, NCRYPT_PCP_EKPUB_PROPERTY, "EK public blob SHA-256"));
        results.push_back(endorsement(provider, NCRYPT_PCP_EKCERT_PROPERTY, "EK certificate SHA-256"));
    }
    results.push_back(unavailable("tpm", "Platform TPM", "Application device identity", "TPM/CNG inventory boundary",
        "Passive inventory does not create keys or perform attestation. A TPM-backed application identity requires a separate enrollment operation.", IdStatus::NotSupported));
    return results;
}
}
