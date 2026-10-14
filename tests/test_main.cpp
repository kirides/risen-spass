// Unit tests that run without the game: string file parser, mod discovery/ordering and the Lua runtime (sandbox, events,
// require). Build target `spass_tests`; run with `ctest` or directly. A tiny self-contained runner, no framework.

#include <windows.h>
#include <dbghelp.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "game/loc_files.h"
#include "lua/mods.h"
#include "lua/runtime.h"

#pragma comment(lib, "dbghelp.lib")

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;
std::string g_current;

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        ++g_checks;                                                                                  \
        if (!(cond)) {                                                                               \
            ++g_failures;                                                                            \
            std::printf("  FAIL [%s] %s:%d: %s\n", g_current.c_str(), __FILE__, __LINE__, #cond);    \
        }                                                                                            \
    } while (0)

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("spass_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void write(const std::string& rel, const std::string& content) const {
        const fs::path p = path / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << content;
    }
};

std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}
struct Register {
    Register(const char* name, std::function<void()> fn) { registry().emplace_back(name, std::move(fn)); }
};
#define TEST(name)                           \
    static void name();                      \
    static Register reg_##name(#name, name); \
    static void name()

// ---- string files -----------------------------------------------------------------------------------------------------

TEST(csv_semicolon_and_comments) {
    const auto rows = spass::game::parseStringCsv("# comment\nFO_A;Alpha\n\nFO_B;Beta; with semicolon\n");
    CHECK(rows.size() == 2);
    CHECK(rows[0].id == "FO_A" && rows[0].text == "Alpha");
    CHECK(rows[1].id == "FO_B" && rows[1].text == "Beta");  // only the first two fields are used
}

TEST(csv_comma_quotes_and_multiline) {
    const auto rows = spass::game::parseStringCsv("id,text\r\nFO_A,\"Hello, \"\"world\"\"\"\r\nFO_B,\"two\nlines\"\r\n");
    CHECK(rows.size() == 2);  // the header row is skipped
    CHECK(rows.size() == 2 && rows[0].text == "Hello, \"world\"");
    CHECK(rows.size() == 2 && rows[1].text == "two\nlines");
}

TEST(csv_bom_and_utf8) {
    const auto rows = spass::game::parseStringCsv("\xEF\xBB\xBF" "FO_Bob;K\xC3\xA4se\n");
    CHECK(rows.size() == 1 && rows[0].id == "FO_Bob" && rows[0].text == "K\xC3\xA4se");
}

TEST(csv_ignores_incomplete_rows) {
    const auto rows = spass::game::parseStringCsv("lonely\n;no id\nGOOD;ok\n");
    CHECK(rows.size() == 1 && rows[0].id == "GOOD");
}

// ---- mods ---------------------------------------------------------------------------------------------------------------

TEST(mods_discovery_skips_invalid_and_disabled) {
    TempDir t;
    t.write("good/mod.toml", "id = \"good\"\nname = \"Good\"\nversion = \"1.2.3\"\n");
    t.write("bad_id/mod.toml", "id = \"Not Valid!\"\n");
    t.write("noid/mod.toml", "name = \"x\"\n");
    t.write("off.off/mod.toml", "id = \"off\"\n");
    t.write("_hidden/mod.toml", "id = \"hidden\"\n");
    t.write("plain/readme.txt", "no manifest");
    const auto mods = spass::lua::discoverMods(t.path);
    CHECK(mods.size() == 1);
    CHECK(!mods.empty() && mods[0].id == "good" && mods[0].version == "1.2.3" && mods[0].main == "main.lua");
}

TEST(mods_load_order_and_requirements) {
    TempDir t;
    t.write("a/mod.toml", "id = \"a\"\nload_after = [\"b\"]\n");
    t.write("b/mod.toml", "id = \"b\"\n");
    t.write("c/mod.toml", "id = \"c\"\nrequires = [\"missing\"]\n");
    t.write("d/mod.toml", "id = \"d\"\nrequires = [\"c\"]\n");
    const auto ordered = spass::lua::resolveLoadOrder(spass::lua::discoverMods(t.path));
    std::vector<std::string> ids;
    for (const auto& m : ordered) {
        ids.push_back(m.id);
    }
    CHECK(ids == (std::vector<std::string>{"b", "a"}));  // c is dropped (missing requirement), d with it
}

TEST(mods_cycle_is_dropped) {
    TempDir t;
    t.write("x/mod.toml", "id = \"x\"\nload_after = [\"y\"]\n");
    t.write("y/mod.toml", "id = \"y\"\nload_after = [\"x\"]\n");
    t.write("z/mod.toml", "id = \"z\"\n");
    const auto ordered = spass::lua::resolveLoadOrder(spass::lua::discoverMods(t.path));
    CHECK(ordered.size() == 1 && ordered[0].id == "z");
}

TEST(mods_data_overlay_flag) {
    TempDir t;
    t.write("plain/mod.toml", "id = \"plain\"\n");
    t.write("overlay/mod.toml", "id = \"overlay\"\ndata_overlay = true\n");
    const auto mods = spass::lua::discoverMods(t.path);
    CHECK(mods.size() == 2);
    for (const auto& m : mods) {
        CHECK(m.dataOverlay == (m.id == "overlay"));
    }
}

// ---- Lua runtime --------------------------------------------------------------------------------------------------------

TEST(runtime_sandbox_events_and_isolation) {
    TempDir t;
    // A mod registers its handler only after its own sanity checks passed, so "the handler exists" proves them.
    t.write("one/mod.toml", "id = \"one\"\n");
    t.write("one/main.lua",
            "leaked = 'from one'\n"
            "assert(io == nil and os.execute == nil and debug == nil and ffi == nil and package == nil, 'sandbox open')\n"
            "assert(loadfile == nil and dofile == nil, 'file loaders exposed')\n"
            "assert(type(os.time) == 'function' and type(string.format) == 'function' and type(bit.band) == 'function')\n"
            "local helper = require('lib.util')\n"
            "assert(helper.answer == 42 and require('lib.util') == helper, 'require')\n"
            "assert(mod.id == 'one' and type(risen.version) == 'string')\n"
            "assert(not pcall(require, '../escape'), 'require must reject path traversal')\n"
            "risen.on('test.ping', function(a, b) return a + b end)\n");
    t.write("one/lib/util.lua", "return { answer = 42 }\n");
    t.write("two/mod.toml", "id = \"two\"\nload_after = [\"one\"]\n");
    t.write("two/main.lua",
            "assert(leaked == nil, 'globals leak between mods')\n"
            "risen.on('test.ping', function(a, b) return nil end, 5)\n"
            "risen.on('test.ping', function(a, b) return a * b end, 10)\n");

    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, /*unsafeLua=*/false));
    CHECK(rt.running());
    // priority 10 (two) runs first; emitUntilHandled stops at the first non-nil result: 6*7, not 6+7
    {
        const sol::object r = rt.emitUntilHandled("test.ping", 6, 7);  // must not outlive the Lua state
        CHECK(r.valid() && r.is<double>() && r.as<double>() == 42.0);
    }
    CHECK(rt.hasHandlers("test.ping"));
    CHECK(!rt.hasHandlers("test.nobody"));
    rt.stop();
    CHECK(!rt.running());
}

TEST(runtime_unsafe_mode_opens_the_sandbox) {
    TempDir t;
    t.write("dev/mod.toml", "id = \"dev\"\n");
    t.write("dev/main.lua", "assert(io ~= nil and debug ~= nil, 'unsafe mode should expose io/debug')\nrisen.on('ok', function() return 1 end)\n");
    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, /*unsafeLua=*/true));
    CHECK(rt.hasHandlers("ok"));
    rt.stop();
}

TEST(runtime_survives_broken_mods) {
    TempDir t;
    t.write("syntax/mod.toml", "id = \"syntax\"\n");
    t.write("syntax/main.lua", "this is not lua (\n");
    t.write("runtime/mod.toml", "id = \"runtime\"\n");
    t.write("runtime/main.lua", "error('boom')\n");
    t.write("fine/mod.toml", "id = \"fine\"\n");
    t.write("fine/main.lua", "risen.on('t', function() error('handler fails') end)\nrisen.on('t', function() return 1 end, -1)\n");
    t.write("bytecode/mod.toml", "id = \"bytecode\"\n");
    t.write("bytecode/main.lua", std::string("\x1b") + "LJ junk");
    auto& rt = spass::lua::Runtime::instance();
    CHECK(rt.start(t.path, false));
    // a handler that raises is logged and skipped; the next one still runs
    {
        const sol::object r = rt.emitUntilHandled("t");
        CHECK(r.valid() && r.is<double>() && r.as<double>() == 1.0);
    }
    rt.stop();
}

}  // namespace

// Prints the faulting stack with symbol names when a test crashes (the PDB is next to the exe).
LONG WINAPI crashHandler(EXCEPTION_POINTERS* info) {
    std::printf("CRASH: exception 0x%08lx at %p\n", info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    void* frames[48];
    const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    for (USHORT i = 0; i < n; ++i) {
        DWORD64 disp = 0;
        if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), &disp, sym)) {
            std::printf("  #%u %s +0x%llx\n", i, sym->Name, static_cast<unsigned long long>(disp));
        } else {
            std::printf("  #%u %p\n", i, frames[i]);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int main() {
    SetUnhandledExceptionFilter(crashHandler);
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // keep the output when a test crashes
    spdlog::set_level(spdlog::level::off);
    for (auto& [name, fn] : registry()) {
        g_current = name;
        const int before = g_failures;
        fn();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", name.c_str());
    }
    std::printf("\n%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
