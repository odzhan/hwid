#pragma once
#include "common/HardwareIdentifier.h"
#include <functional>
namespace hwid {
HardwareResults collectSystem();
HardwareResults collectMotherboard();
HardwareResults collectBios();
HardwareResults collectTpm();
HardwareResults collectStorage();
HardwareResults collectNetwork();
HardwareResults collectUsb();
HardwareResults collectGpu();
HardwareResults collectSdEmmc();
HardwareResults collectCpu();
HardwareResults collectSafely(std::string_view component, const std::function<HardwareResults()>& collector);
HardwareResults collectAll(std::string_view component = {});
}
