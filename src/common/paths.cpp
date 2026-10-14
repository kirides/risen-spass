#include "common/paths.h"

#include <string>

namespace spass::paths {
namespace {

std::filesystem::path g_bin;
std::filesystem::path g_data;
std::filesystem::path g_mods;

}  // namespace

void init(HMODULE self) {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(self, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            break;
        }
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    g_bin = std::filesystem::path(buf).parent_path();
    g_data = g_bin / L"spass";
    g_mods = g_data / L"mods";
}

const std::filesystem::path& binDir() { return g_bin; }
const std::filesystem::path& dataDir() { return g_data; }
const std::filesystem::path& modsDir() { return g_mods; }

}  // namespace spass::paths
