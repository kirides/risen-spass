cmake_minimum_required(VERSION 3.25)

project(risen_spass VERSION 0.1.0 LANGUAGES CXX)

# The layer (sentry.dll) targets 64-bit Windows only (Risen Enhanced is x64). The mod editor risen-modkit (src/modkit,
# docs/editor/REQUIREMENTS.md) and the offline readers it shares with risen-pack2mod build on Linux and macOS too.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SPASS_BUILD_LAYER_DEFAULT ON)
else()
    set(SPASS_BUILD_LAYER_DEFAULT OFF)
endif()
option(SPASS_BUILD_LAYER "Build the layer (sentry.dll), risen-pack2mod and their tests (64-bit Windows only)" ${SPASS_BUILD_LAYER_DEFAULT})
if(SPASS_BUILD_LAYER AND NOT (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8))
    message(FATAL_ERROR "The layer targets 64-bit Windows only (Risen Enhanced is x64). Configure with -DSPASS_BUILD_LAYER=OFF to build only risen-modkit.")
endif()
# risen-modkit needs the vcpkg manifest feature "modkit" (the presets enable it)
if("modkit" IN_LIST VCPKG_MANIFEST_FEATURES)
    set(SPASS_BUILD_MODKIT_DEFAULT ON)
else()
    set(SPASS_BUILD_MODKIT_DEFAULT OFF)
endif()
option(SPASS_BUILD_MODKIT "Build risen-modkit, the mod editor (needs the vcpkg feature modkit)" ${SPASS_BUILD_MODKIT_DEFAULT})
option(SPASS_BUILD_TESTS "Build the unit tests" ON)
if(SPASS_BUILD_TESTS)
    enable_testing()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
if(WIN32)
    # every <windows.h> in the project (and in the headers it includes) is the trimmed one, without min/max macros
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# Build speed. MSBuild compiles the files of one project one after another unless cl.exe gets /MP (Ninja runs one
# cl.exe per file anyway).
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    add_compile_options(/MP)
endif()
# Debug info goes into each object file (/Z7) instead of one shared PDB per target (/Zi): with /Zi every
# parallel cl.exe waits for mspdbsrv to write that PDB. The linker still writes the .pdb next to the binary (/DEBUG).
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
# Precompiled headers: sol2 (through the bindings' headers), spdlog/fmt, nlohmann/json (modkit) and the
# standard library are most of the compile time of every file.
option(SPASS_PCH "Precompile the common headers of each library (faster builds)" ON)

# ---------------------------------------------------------------------------------------------
# Dependencies (vcpkg manifest: vcpkg.json)
# ---------------------------------------------------------------------------------------------
# vcpkg only builds Debug and Release; without this CMake may pick the Debug import library
# (and the debug CRT) for RelWithDebInfo/MinSizeRel.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "Release")
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "Release")

if(SPASS_BUILD_LAYER)
    find_package(spdlog CONFIG REQUIRED)
    find_package(minhook CONFIG REQUIRED)
    find_package(tomlplusplus CONFIG REQUIRED)
endif()
find_package(sol2 CONFIG REQUIRED)

# The vcpkg luajit port ships headers in include/luajit and lua51.lib, but no CMake package.
find_path(LUAJIT_INCLUDE_DIR NAMES lua.hpp PATH_SUFFIXES luajit REQUIRED)
# Pin each lookup to its exact directory: vcpkg's find_library wrapper may otherwise return the
# debug library for the release variable.
set(_vcpkg_prefix "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
find_library(LUAJIT_LIB_REL NAMES lua51 luajit-5.1 PATHS "${_vcpkg_prefix}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(LUAJIT_LIB_DBG NAMES lua51 luajit-5.1 PATHS "${_vcpkg_prefix}/debug/lib" NO_DEFAULT_PATH)
if(NOT LUAJIT_LIB_DBG)
    set(LUAJIT_LIB_DBG "${LUAJIT_LIB_REL}")
endif()
set(LUAJIT_LIBRARY_RELEASE "${LUAJIT_LIB_REL}")
set(LUAJIT_LIBRARY_DEBUG "${LUAJIT_LIB_DBG}")

add_library(luajit::luajit STATIC IMPORTED)
set_target_properties(luajit::luajit PROPERTIES
    IMPORTED_LOCATION "${LUAJIT_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_RELEASE "${LUAJIT_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_DEBUG "${LUAJIT_LIBRARY_DEBUG}"
    INTERFACE_INCLUDE_DIRECTORIES "${LUAJIT_INCLUDE_DIR}")

if(SPASS_BUILD_LAYER)
# ---------------------------------------------------------------------------------------------
# Core library: everything except the proxy entry point, so tests/tools can link it.
# ---------------------------------------------------------------------------------------------
add_library(spass_core STATIC
    src/bootstrap.cpp
    src/config.cpp
    src/common/crash.cpp
    src/common/log.cpp
    src/common/paths.cpp
    src/common/test_run.cpp
    src/common/trace.cpp
    src/game/abi.cpp
    src/game/bindings.cpp
    src/game/call_trace.cpp
    src/game/engine_guards.cpp
    src/game/entity_events.cpp
    src/game/event_bus.cpp
    src/game/events.cpp
    src/game/file_trace.cpp
    src/game/folder_redirect.cpp
    src/game/hooks.cpp
    src/game/instance_mutex.cpp
    src/game/background_window.cpp
    src/game/inventory_events.cpp
    src/game/lua_ai.cpp
    src/game/lua_assets.cpp
    src/game/lua_balance.cpp
    src/game/loc_files.cpp
    src/game/loc_overlay.cpp
    src/game/screenshot.cpp
    src/game/lua_dialog.cpp
    src/game/lua_engine_info.cpp
    src/game/lua_entity.cpp
    src/game/lua_geometry.cpp
    src/game/lua_combat.cpp
    src/game/lua_config.cpp
    src/game/lua_options.cpp
    src/game/lua_hud.cpp
    src/game/lua_render.cpp
    src/game/lua_scene.cpp
    src/game/lua_gui.cpp
    src/game/lua_input.cpp
    src/game/lua_interact.cpp
    src/game/lua_items.cpp
    src/game/lua_mods.cpp
    src/game/lua_npc.cpp
    src/game/lua_npc_balance.cpp
    src/game/lua_persistent_npc.cpp
    src/game/lua_recipe.cpp
    src/game/lua_routine.cpp
    src/game/lua_sectors.cpp
    src/game/lua_spell.cpp
    src/game/lua_quest.cpp
    src/game/lua_save.cpp
    src/game/lua_template.cpp
    src/game/lua_test.cpp
    src/game/lua_vfs.cpp
    src/game/lua_voice.cpp
    src/game/properties.cpp
    src/game/script_bridge.cpp
    src/game/symbols.cpp
    src/game/vfs_mount.cpp
    src/hook/exports.cpp
    src/hook/hook_manager.cpp
    src/lua/mods.cpp
    src/lua/runtime.cpp
)
target_include_directories(spass_core PUBLIC src)
target_compile_features(spass_core PUBLIC cxx_std_20)
target_compile_definitions(spass_core PUBLIC
    UNICODE _UNICODE
    SOL_LUAJIT=1 SOL_ALL_SAFETIES_ON=1 SOL_EXCEPTIONS_SAFE_PROPAGATION=0
    SPASS_VERSION="${PROJECT_VERSION}")
target_link_libraries(spass_core PUBLIC
    spdlog::spdlog minhook::minhook sol2::sol2 tomlplusplus::tomlplusplus luajit::luajit)
if(MSVC)
    target_compile_options(spass_core PUBLIC /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc /FS)
endif()
if(SPASS_PCH)
    target_precompile_headers(spass_core PRIVATE
        <sol/sol.hpp> <spdlog/spdlog.h> <toml++/toml.hpp>
        <algorithm> <atomic> <cstdint> <cstring> <filesystem> <fstream> <functional> <map> <mutex> <optional> <set>
        <string> <string_view> <tuple> <unordered_map> <vector>)
endif()
endif()

# ---------------------------------------------------------------------------------------------
# risen-pack2mod: standalone converter from a Risen mod's .pNN / .pak archives to a mod for the layer (docs/PACK2MOD.md).
# Depends only on zlib; the generic Lua code of a converted mod (tools/extmod_templates/main.lua, config.lua) is embedded.
# ---------------------------------------------------------------------------------------------
find_package(ZLIB REQUIRED)
set(P2M_EMBED_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/extmod_templates/main.lua|kP2mMainLua"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/extmod_templates/config.lua|kP2mConfigLua")
set(P2M_EMBED_SRC "extern const unsigned char kP2mMainLua[];
extern const unsigned long long kP2mMainLuaSize;
extern const unsigned char kP2mConfigLua[];
extern const unsigned long long kP2mConfigLuaSize;
")
foreach(_entry IN LISTS P2M_EMBED_FILES)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _file)
    list(GET _parts 1 _sym)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
    file(READ "${_file}" _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _arr "${_hex}")
    string(APPEND P2M_EMBED_SRC "const unsigned char ${_sym}[] = { ${_arr} 0x00 };
const unsigned long long ${_sym}Size = sizeof(${_sym}) - 1;
")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pack2mod_embedded.cpp.tmp" "${P2M_EMBED_SRC}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/pack2mod_embedded.cpp.tmp" "${CMAKE_CURRENT_BINARY_DIR}/pack2mod_embedded.cpp")

add_library(pack2mod_core STATIC
    src/pack2mod/cli.cpp
    src/pack2mod/convert.cpp
    src/pack2mod/cs.cpp
    src/pack2mod/data.cpp
    src/pack2mod/dialog.cpp
    src/pack2mod/items.cpp
    src/pack2mod/json_in.cpp
    src/pack2mod/library.cpp
    src/pack2mod/lua_emit.cpp
    src/pack2mod/pak.cpp
    src/pack2mod/strings_stage.cpp
    src/pack2mod/tab.cpp
    src/pack2mod/text.cpp
    src/pack2mod/tple.cpp
    src/pack2mod/util.cpp
    src/pack2mod/world.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/pack2mod_embedded.cpp"
)
target_include_directories(pack2mod_core PUBLIC src)
target_compile_features(pack2mod_core PUBLIC cxx_std_20)
target_compile_definitions(pack2mod_core PUBLIC UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING P2M_VERSION="${PROJECT_VERSION}")
target_link_libraries(pack2mod_core PUBLIC ZLIB::ZLIB)
if(MSVC)
    target_compile_options(pack2mod_core PUBLIC /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc /FS)
endif()
if(SPASS_PCH)
    target_precompile_headers(pack2mod_core PRIVATE
        <algorithm> <cstdint> <filesystem> <map> <optional> <set> <string> <unordered_map> <vector>)
    # the generated file with the embedded Lua is plain C arrays
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/pack2mod_embedded.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()
if(SPASS_BUILD_LAYER)
    add_executable(risen-pack2mod src/pack2mod/main.cpp)
    target_link_libraries(risen-pack2mod PRIVATE pack2mod_core)
endif()

if(SPASS_BUILD_LAYER)
# ---------------------------------------------------------------------------------------------
# The proxy DLL. Risen.exe statically imports sentry.dll (crash reporter), so a DLL named
# sentry.dll that forwards every export to the renamed original (sentry_orig.dll) is loaded
# before main() without touching any game binary. See docs/RE.md and scripts/install.ps1.
# ---------------------------------------------------------------------------------------------
# MSVC only accepts `sentry_init = sentry_orig.sentry_init` forwarders if it can see an import
# library for sentry_orig, so generate one from the original's export list.
set(SENTRY_ORIG_LIB "${CMAKE_CURRENT_BINARY_DIR}/sentry_orig.lib")
add_custom_command(
    OUTPUT "${SENTRY_ORIG_LIB}"
    COMMAND "${CMAKE_AR}" /nologo /MACHINE:X64 "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/sentry_orig.def"
            "/OUT:${SENTRY_ORIG_LIB}"
    DEPENDS src/proxy/sentry_orig.def
    COMMENT "Generating import library for sentry_orig.dll"
    VERBATIM)
add_custom_target(sentry_orig_lib DEPENDS "${SENTRY_ORIG_LIB}")

add_library(spass_proxy SHARED
    src/proxy/dllmain.cpp
    src/proxy/sentry_proxy.def
)
add_dependencies(spass_proxy sentry_orig_lib)
target_link_libraries(spass_proxy PRIVATE spass_core "${SENTRY_ORIG_LIB}")
set_target_properties(spass_proxy PROPERTIES OUTPUT_NAME sentry)

install(TARGETS spass_proxy RUNTIME DESTINATION bin)
install(DIRECTORY mods/ DESTINATION spass/mods)

# ---------------------------------------------------------------------------------------------
# Unit tests that run without the game (string files, mod discovery, the Lua runtime): `ctest -C RelWithDebInfo`
# ---------------------------------------------------------------------------------------------
if(SPASS_BUILD_TESTS)
    add_executable(spass_tests tests/test_main.cpp)
    target_link_libraries(spass_tests PRIVATE spass_core pack2mod_core)
    target_compile_definitions(spass_tests PRIVATE SPASS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME spass_tests COMMAND spass_tests)
endif()
endif()

# ---------------------------------------------------------------------------------------------
# risen-modkit: the mod editor (src/modkit, docs/editor/REQUIREMENTS.md)
# ---------------------------------------------------------------------------------------------
if(SPASS_BUILD_MODKIT)
    add_subdirectory(src/modkit)
    # risen-pack2mod --project (docs/PACK2MOD.md "Editor project") maps its results onto the editor's project model: that code is part of modkit_core,
    # which links pack2mod_core, so the executable (not the library) links modkit_core. Without the modkit feature the option reports that it is missing.
    if(SPASS_BUILD_LAYER)
        target_link_libraries(risen-pack2mod PRIVATE modkit_core)
        target_compile_definitions(risen-pack2mod PRIVATE P2M_WITH_MODKIT)
    endif()
endif()
