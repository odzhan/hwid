#include "Collectors.h"
#include "common/Error.h"
#include <iterator>
namespace hwid {
HardwareResults collectSafely(std::string_view component, const std::function<HardwareResults()>& collector) {
    try {
        auto results = collector();
        if (results.empty()) results.push_back(unavailable(std::string(component), "", "Inventory", "Windows enumeration", "No devices reported", IdStatus::NotPresent));
        return results;
    } catch (const WindowsError& e) {
        return {unavailable(std::string(component), "", "Inventory", "Windows API", e.what(), errorStatus(e.code))};
    } catch (const std::exception& e) {
        return {unavailable(std::string(component), "", "Inventory", "Collector", e.what(), IdStatus::QueryFailed)};
    }
}
HardwareResults collectAll(std::string_view component) {
    using Collector = HardwareResults(*)();
    constexpr Collector collectors[] = {collectSystem, collectMotherboard, collectBios, collectTpm, collectStorage,
        collectNetwork, collectUsb, collectGpu, collectSdEmmc, collectCpu};
    HardwareResults results;
    for (size_t i = 0; i < components.size(); ++i) {
        if (!component.empty() && component != components[i]) continue;
        auto rows = collectSafely(components[i], collectors[i]);
        results.insert(results.end(), std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
    }
    return results;
}
}
