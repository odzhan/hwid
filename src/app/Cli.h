#pragma once
#include <string>
#include <span>
#include <string_view>
namespace hwid {
struct Options { bool json = false, verbose = false, redact = false, help = false; std::string component; };
Options parseCli(std::span<const std::string_view> args);
std::string_view usage();
}
