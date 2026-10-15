cmake_minimum_required(VERSION 3.25)

project(risen_spass VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "risen-spass targets 64-bit Windows only (Risen Enhanced is x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---------------------------------------------------------------------------------------------
# Dependencies (vcpkg manifest: vcpkg.json)
# ---------------------------------------------------------------------------------------------
# vcpkg only builds Debug and Release; without this CMake may pick the Debug import library
# (and the debug CRT) for RelWithDebInfo/MinSizeRel.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "Release")
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "Release")

find_package(spdlog CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

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

# ---------------------------------------------------------------------------------------------
# Core library: everything except the proxy entry point, so tests/tools can link it.
# ---------------------------------------------------------------------------------------------
add_library(spass_core STATIC
    src/bootstrap.cpp
    src/config.cpp
    src/common/log.cpp
    src/common/paths.cpp
    src/common/test_run.cpp
    src/common/trace.cpp
    src/game/abi.cpp
    src/game/bindings.cpp
    src/game/events.cpp
    src/game/file_trace.cpp
    src/game/folder_redirect.cpp
    src/game/hooks.cpp
    src/game/lua_balance.cpp
    src/game/loc_files.cpp
    src/game/loc_overlay.cpp
    src/game/lua_dialog.cpp
    src/game/lua_engine_info.cpp
    src/game/lua_entity.cpp
    src/game/lua_items.cpp
    src/game/lua_npc.cpp
    src/game/lua_npc_balance.cpp
    src/game/lua_quest.cpp
    src/game/lua_save.cpp
    src/game/lua_template.cpp
    src/game/lua_test.cpp
    src/game/lua_vfs.cpp
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
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    SOL_LUAJIT=1 SOL_ALL_SAFETIES_ON=1
    SPASS_VERSION="${PROJECT_VERSION}")
target_link_libraries(spass_core PUBLIC
    spdlog::spdlog minhook::minhook sol2::sol2 tomlplusplus::tomlplusplus luajit::luajit)
if(MSVC)
    target_compile_options(spass_core PUBLIC /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc /FS)
endif()

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
option(SPASS_BUILD_TESTS "Build the unit tests" ON)
if(SPASS_BUILD_TESTS)
    enable_testing()
    add_executable(spass_tests tests/test_main.cpp)
    target_link_libraries(spass_tests PRIVATE spass_core)
    add_test(NAME spass_tests COMMAND spass_tests)
endif()
