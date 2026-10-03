#pragma once
#include <string>
#include <string_view>
#include <span>
#include <cstdint>
namespace hwid {
std::string utf8(std::wstring_view value);
std::string trim(std::string_view value);
std::string lower(std::string value);
bool isPlaceholderHardwareId(std::string_view value);
std::string normalizeUuid(std::string_view value);
std::string mac(std::span<const unsigned char> bytes);
std::string hex(std::span<const unsigned char> bytes);
std::string hexNumber(std::uint64_t value, int width = 0);
std::string descriptorString(std::span<const unsigned char> bytes, std::uint32_t offset);
std::string jsonQuote(std::string_view value);
}
