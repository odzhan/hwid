#include "Writers.h"
#include "common/Strings.h"
#include <algorithm>
#include <array>
namespace hwid {
namespace {
std::string safe(std::string_view s, bool redact) { return redact && !s.empty() ? "[redacted]" : std::string(s); }
// Escape control characters in device-provided strings, including terminal escape sequences.
std::string console(std::string_view s) {
    auto quoted = jsonQuote(s);
    return quoted.substr(1, quoted.size() - 2);
}
}
void writeJson(std::ostream& out, const HardwareResults& results, bool redact) {
    out << "{\n  \"schemaVersion\": 1,\n  \"redacted\": " << (redact ? "true" : "false");
    for (auto component : components) {
        out << ",\n  " << jsonQuote(component) << ": [";
        bool first = true;
        for (const auto& r : results) {
            if (r.component != component) continue;
            if (!first) out << ',';
            first = false;
            out << "\n    {";
            auto field = [&](std::string_view k, std::string_view v) { out << jsonQuote(k) << ": " << jsonQuote(v) << ", "; };
            field("component", r.component); field("deviceName", safe(r.deviceName, redact));
            field("identifierType", r.identifierType); field("rawValue", safe(r.rawValue, redact));
            field("normalizedValue", safe(r.normalizedValue, redact)); field("source", r.source);
            field("confidence", toString(r.confidence)); field("status", toString(r.status));
            out << "\"potentiallyMutable\": " << (r.potentiallyMutable ? "true" : "false")
                << ", \"available\": " << (r.available ? "true" : "false") << ", \"error\": " << jsonQuote(safe(r.error, redact)) << '}';
        }
        out << (first ? "]" : "\n  ]");
    }
    out << "\n}\n";
}
void writeConsole(std::ostream& out, const HardwareResults& results, bool verbose, bool redact) {
    std::string previous;
    for (size_t begin = 0; begin < results.size();) {
        const auto& first = results[begin];
        size_t end = begin + 1;
        while (end < results.size() && results[end].component == first.component && results[end].deviceName == first.deviceName) ++end;
        if (first.component != previous) { out << '\n' << first.component << '\n'; previous = first.component; }
        if (!first.deviceName.empty()) out << "  " << console(safe(first.deviceName, redact)) << '\n';

        using Row = std::array<std::string, 3>;
        std::vector<Row> rows{{"Identifier", "Value", "Status"}};
        for (size_t i = begin; i < end; ++i) {
            const auto& r = results[i];
            rows.push_back({console(r.identifierType), r.available ? console(safe(r.normalizedValue, redact)) : "-", std::string(toString(r.status))});
        }
        // Count UTF-8 characters rather than bytes when padding. Values are never truncated.
        auto width = [](std::string_view text) {
            return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](unsigned char c) { return (c & 0xC0) != 0x80; }));
        };
        std::array<size_t, 3> widths{};
        for (const auto& row : rows)
            for (size_t column = 0; column < widths.size(); ++column) widths[column] = std::max(widths[column], width(row[column]));
        auto border = [&] {
            out << "  +";
            for (auto size : widths) out << std::string(size + 2, '-') << '+';
            out << '\n';
        };
        border();
        for (size_t i = 0; i < rows.size(); ++i) {
            out << "  |";
            for (size_t column = 0; column < widths.size(); ++column)
                out << ' ' << rows[i][column] << std::string(widths[column] - width(rows[i][column]) + 1, ' ') << '|';
            out << '\n';
            if (i == 0) border();
        }
        border();
        if (verbose) {
            for (size_t i = begin; i < end; ++i) {
                const auto& r = results[i];
                out << "    " << console(r.identifierType) << ":\n"
                    << "      Source: " << console(r.source) << " | Confidence: " << toString(r.confidence)
                    << " | Mutable: " << (r.potentiallyMutable ? "possibly" : "no") << '\n'
                    << "      Raw: " << console(safe(r.rawValue, redact)) << '\n';
                if (!r.error.empty()) out << "      Note: " << console(safe(r.error, redact)) << '\n';
            }
        }
        begin = end;
    }
    if (!verbose && !results.empty()) out << "\nUse --verbose for sources, confidence, raw values and diagnostics.\n";
}
}
