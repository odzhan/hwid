#include "Collectors.h"
#include "common/Error.h"
#include "common/Strings.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
namespace hwid {
HardwareResults collectNetwork() {
    ULONG size = 16384;
    std::vector<unsigned char> buffer;
    ULONG code = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 4 && code == ERROR_BUFFER_OVERFLOW; ++attempt) {
        if (size > 16 * 1024 * 1024) throw std::runtime_error("Adapter enumeration buffer too large");
        buffer.resize(size);
        code = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_ALL_INTERFACES | GAA_FLAG_SKIP_ANYCAST |
            GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (code == ERROR_NO_DATA) return {};
    if (code != NO_ERROR) throw WindowsError("GetAdaptersAddresses", code);
    HardwareResults results;
    for (auto adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next) {
        const auto name = adapter->FriendlyName ? utf8(adapter->FriendlyName) : "Unnamed interface";
        auto add = [&](std::string type, std::string value, std::string source, IdConfidence confidence, bool validate = false) {
            results.push_back(identifier("network", name, type, value, source, confidence, true, validate));
        };
        if (adapter->PhysicalAddressLength > sizeof(adapter->PhysicalAddress)) {
            results.push_back(unavailable("network", name, "Current MAC", "GetAdaptersAddresses.PhysicalAddress", "Invalid physical address length", IdStatus::QueryFailed));
        } else {
            add("Current MAC", mac({adapter->PhysicalAddress, adapter->PhysicalAddressLength}), "GetAdaptersAddresses.PhysicalAddress", IdConfidence::InterfaceAssigned, true);
        }
        add("Interface type", std::to_string(adapter->IfType), "GetAdaptersAddresses.IfType (IANA ifType)", IdConfidence::Informational);
        add("Operational status", std::to_string(adapter->OperStatus), "GetAdaptersAddresses.OperStatus", IdConfidence::Informational);
        MIB_IF_ROW2 row{}; row.InterfaceLuid = adapter->Luid;
        code = GetIfEntry2(&row);
        if (code != NO_ERROR) {
            results.push_back(unavailable("network", name, "Permanent MAC", "GetIfEntry2.PermanentPhysicalAddress", windowsError("GetIfEntry2", code), errorStatus(code)));
            continue;
        }
        add("Hardware interface", row.InterfaceAndOperStatusFlags.HardwareInterface ? "true" : "false",
            "GetIfEntry2.InterfaceAndOperStatusFlags.HardwareInterface", IdConfidence::Informational);
        if (row.PhysicalAddressLength > sizeof(row.PermanentPhysicalAddress)) {
            results.push_back(unavailable("network", name, "Permanent MAC", "GetIfEntry2.PermanentPhysicalAddress", "Invalid physical address length", IdStatus::QueryFailed));
            continue;
        }
        auto r = identifier("network", name, "Permanent MAC", mac({row.PermanentPhysicalAddress, row.PhysicalAddressLength}),
            "GetIfEntry2.PermanentPhysicalAddress", IdConfidence::InterfaceAssigned);
        if (!r.available) { r.status = IdStatus::NotExposed; r.error = "Driver does not expose a permanent address"; }
        else r.error = "Driver-reported permanent address; virtual interfaces and firmware can affect its meaning";
        results.push_back(std::move(r));
    }
    return results;
}
}
