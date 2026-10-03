#include "app/Cli.h"
#include "collectors/Collectors.h"
#include "output/Writers.h"
#include <Windows.h>
#include <iostream>
#include <vector>
int main(int argc, char** argv) {
    try {
        std::vector<std::string_view> args;
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        hwid::Options options;
        try { options = hwid::parseCli(args); }
        catch (const std::invalid_argument& e) { std::cerr << e.what() << '\n' << hwid::usage(); return 2; }
        if (options.help) { std::cout << hwid::usage(); return 0; }
        // Restore the console code page; redirected output is always UTF-8.
        struct ConsoleUtf8 {
            UINT old = GetConsoleOutputCP();
            ConsoleUtf8() { if (old) SetConsoleOutputCP(CP_UTF8); }
            ~ConsoleUtf8() { if (old) SetConsoleOutputCP(old); }
        } console;
        const auto results = hwid::collectAll(options.component);
        if (options.json) hwid::writeJson(std::cout, results, options.redact);
        else hwid::writeConsole(std::cout, results, options.verbose, options.redact);
        return std::cout ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << "hwid: " << e.what() << '\n'; return 1; }
}
