#include "Strings.h"
#include "HardwareIdentifier.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <limits>

namespace hwid {
std::string utf8(std::wstring_view v) {
    if (v.empty()) return {};
    if (v.size() > static_cast<size_t>(std::numeric_limits<int>::max())) throw std::runtime_error("UTF-16 input too large");
    const auto size = static_cast<int>(v.size());
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, v.data(), size, nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Invalid UTF-16 returned by Windows");
    std::string out(n, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, v.data(), size, out.data(), n, nullptr, nullptr))
        throw std::runtime_error("UTF-16 conversion failed");
    return out;
}
std::string trim(std::string_view v) {
    auto pad = [](unsigned char c) { return c == 0 || std::isspace(c); };
    while (!v.empty() && pad(static_cast<unsigned char>(v.front()))) v.remove_prefix(1);
    while (!v.empty() && pad(static_cast<unsigned char>(v.back()))) v.remove_suffix(1);
    return std::string(v);
}
std::string lower(std::string v) {
    for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
}
bool isPlaceholderHardwareId(std::string_view value) {
    const auto v = lower(trim(value));
    for (auto p : {"", "to be filled by o.e.m.", "to be filled by o.e.m", "default string", "none", "unknown", "not specified", "not applicable", "n/a"})
        if (v == p) return true;
    bool hasDigit = false;
    for (char c : v) {
        if (c == '0') hasDigit = true;
        else if (c != '-' && c != ':' && c != '{' && c != '}' && c != ' ') return false;
    }
    return hasDigit;
}
std::string normalizeUuid(std::string_view value) {
    auto v = lower(trim(value));
    if (v.size() == 38 && v.front() == '{' && v.back() == '}') v = v.substr(1, 36);
    if (v.size() != 36) return {};
    bool zero = true, ff = true;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (v[i] != '-') return {}; }
        else {
            if (!std::isxdigit(static_cast<unsigned char>(v[i]))) return {};
            zero &= v[i] == '0'; ff &= v[i] == 'f';
        }
    }
    return zero || ff ? std::string{} : v;
}
std::string hex(std::span<const unsigned char> bytes) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string out;
    for (auto b : bytes) { out += digits[b >> 4]; out += digits[b & 15]; }
    return out;
}
std::string mac(std::span<const unsigned char> bytes) {
    std::string out;
    for (auto b : bytes) { if (!out.empty()) out += ':'; out += hex({&b, 1}); }
    return out;
}
std::string hexNumber(std::uint64_t value, int width) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << value;
    return out.str();
}
std::string descriptorString(std::span<const unsigned char> bytes, std::uint32_t offset) {
    if (!offset) return {};
    if (offset >= bytes.size()) throw std::runtime_error("Storage descriptor string offset outside buffer");
    auto start = bytes.begin() + offset;
    auto end = std::find(start, bytes.end(), 0);
    if (end == bytes.end()) throw std::runtime_error("Unterminated storage descriptor string");
    // STORAGE_DEVICE_DESCRIPTOR strings are ASCII. Reject malformed driver data.
    if (std::any_of(start, end, [](auto c) { return c > 127; })) throw std::runtime_error("Non-ASCII storage descriptor string");
    return std::string(start, end);
}
std::string jsonQuote(std::string_view value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: if (c < 32) out += "\\u00" + hex({&c, 1}); else out += static_cast<char>(c);
        }
    }
    return out + '"';
}
std::string_view toString(IdConfidence v) {
    constexpr std::string_view names[] = {"HardwareBacked", "ManufacturerAssigned", "FirmwareAssigned", "InterfaceAssigned", "Informational", "Unknown"};
    return names[static_cast<size_t>(v)];
}
std::string_view toString(IdStatus v) {
    constexpr std::string_view names[] = {"available", "not supported", "not present", "access denied", "driver does not expose value", "invalid/placeholder", "query failed"};
    return names[static_cast<size_t>(v)];
}
HardwareIdentifier identifier(std::string c, std::string d, std::string t, std::string raw,
    std::string s, IdConfidence confidence, bool m, bool validate) {
    auto normalized = trim(raw);
    bool valid = !normalized.empty() && (!validate || !isPlaceholderHardwareId(normalized));
    return {std::move(c), std::move(d), std::move(t), std::move(raw), std::move(normalized), std::move(s),
        confidence, m, valid, valid ? "" : "Empty or placeholder value returned", valid ? IdStatus::Available : IdStatus::Invalid};
}
HardwareIdentifier unavailable(std::string c, std::string d, std::string t, std::string s, std::string e, IdStatus status) {
    return {std::move(c), std::move(d), std::move(t), {}, {}, std::move(s), IdConfidence::Unknown, true, false, std::move(e), status};
}
}
