#include "Error.h"
#include "Strings.h"
#include <wbemcli.h>
namespace hwid {
std::string windowsError(std::string_view api, DWORD code) {
    wchar_t text[2048]{};
    auto count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, text, 2048, nullptr);
    return std::string(api) + ": " + std::to_string(code) + " (0x" + hexNumber(code, 8) + ")" +
        (count ? ": " + trim(utf8({text, count})) : ": No system message available");
}
IdStatus errorStatus(DWORD code) {
    if (code == ERROR_ACCESS_DENIED || code == static_cast<DWORD>(E_ACCESSDENIED) || code == static_cast<DWORD>(WBEM_E_ACCESS_DENIED) || code == static_cast<DWORD>(NTE_PERM)) return IdStatus::AccessDenied;
    if (code == ERROR_NOT_SUPPORTED || code == ERROR_INVALID_FUNCTION || code == static_cast<DWORD>(NTE_NOT_SUPPORTED)) return IdStatus::NotSupported;
    if (code == ERROR_NOT_FOUND || code == ERROR_FILE_NOT_FOUND || code == ERROR_DEVICE_NOT_CONNECTED) return IdStatus::NotPresent;
    return IdStatus::QueryFailed;
}
}
