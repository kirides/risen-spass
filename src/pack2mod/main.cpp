#include <windows.h>
#include <shellapi.h>

#include <iostream>
#include <string>
#include <vector>

#include "pack2mod/convert.h"

// wmain: Windows hands the command line as UTF-16, the tool works in UTF-8
int wmain(int argc, wchar_t** wargv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), n, nullptr, nullptr);
        args.push_back(std::move(s));
    }
    std::vector<const char*> argv;
    for (const std::string& s : args) argv.push_back(s.c_str());
    return p2m::pack2mod_main(static_cast<int>(argv.size()), argv.data(), std::cout, std::cerr);
}
