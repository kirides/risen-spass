// risen-modkit: the mod editor of the Risen modding layer (docs/editor/REQUIREMENTS.md).
//   risen-modkit [project.rmk.json] [--data DIR] [--screenshot FILE [--frames N] [--panel TITLE] [--select NAME] [--size WxH]]
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <string>

#include "app/app.h"
#include "core/log.h"

int main(int argc, char** argv) {
    mk::AppOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs %s\n", a.c_str(), what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--data") opts.data_dir = next("a directory");
        else if (a == "--screenshot") opts.screenshot = next("a file name");
        else if (a == "--frames") opts.frames = std::max(2, std::atoi(next("a number").c_str()));
        else if (a == "--panel") opts.panel = next("a panel title");
        else if (a == "--select") opts.select = next("a name");
        else if (a == "--size") {
            const std::string s = next("WxH");
            if (std::sscanf(s.c_str(), "%dx%d", &opts.width, &opts.height) != 2) {
                std::fprintf(stderr, "--size wants WxH\n");
                return 2;
            }
        } else if (a == "--help" || a == "-h") {
            std::printf("risen-modkit [project.rmk.json] [--data DIR] [--screenshot FILE [--frames N] [--panel TITLE] [--select NAME] [--size WxH]]\n");
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        } else {
            opts.project = a;
        }
    }
    mk::log_info("risen-modkit " MK_VERSION);
    mk::App app(std::move(opts));
    return app.run();
}
