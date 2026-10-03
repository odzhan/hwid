#pragma once
#include "common/HardwareIdentifier.h"
#include <ostream>
namespace hwid {
void writeJson(std::ostream& out, const HardwareResults& results, bool redact);
void writeConsole(std::ostream& out, const HardwareResults& results, bool verbose, bool redact);
}
