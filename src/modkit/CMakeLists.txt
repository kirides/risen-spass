# risen-modkit: the mod editor ("Mod-Baukasten", docs/editor/REQUIREMENTS.md). Cross-platform: Windows, Linux, macOS.
#   modkit_core   the project model, game data, validation, export / import (no GUI dependency; unit-tested by modkit_tests)
#   risen-modkit  the GLFW + Dear ImGui application
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(unofficial-imgui-node-editor CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)

# The generic Lua part of every exported mod is embedded (like pack2mod's main.lua).
set(MK_EMBED_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/lua/runtime.lua|kMkRuntimeLua")
set(MK_EMBED_SRC "")
foreach(_entry IN LISTS MK_EMBED_FILES)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _file)
    list(GET _parts 1 _sym)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
    file(READ "${_file}" _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _arr "${_hex}")
    string(APPEND MK_EMBED_SRC "extern const unsigned char ${_sym}[] = { ${_arr} 0x00 };
extern const unsigned long long ${_sym}Size = sizeof(${_sym}) - 1;
")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/modkit_embedded.cpp.tmp" "${MK_EMBED_SRC}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/modkit_embedded.cpp.tmp" "${CMAKE_CURRENT_BINARY_DIR}/modkit_embedded.cpp")

add_library(modkit_core STATIC
    core/catalog.cpp
    core/export.cpp
    core/gamedata.cpp
    core/history.cpp
    core/log.cpp
    core/paths.cpp
    core/project.cpp
    core/lua_writer.cpp
    core/settings.cpp
    core/validate.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/modkit_embedded.cpp"
)
target_include_directories(modkit_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src")
target_compile_features(modkit_core PUBLIC cxx_std_20)
target_compile_definitions(modkit_core PUBLIC MK_VERSION="${PROJECT_VERSION}")
target_link_libraries(modkit_core PUBLIC pack2mod_core nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(modkit_core PUBLIC /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc /FS)
    target_compile_definitions(modkit_core PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(modkit_core PUBLIC -Wall -Wextra)
endif()

add_executable(risen-modkit
    app/main.cpp
    app/app.cpp
    app/fonts.cpp
    app/screenshot.cpp
    app/editor_util.cpp
    app/pickers.cpp
    app/prop_editor.cpp
    app/widgets.cpp
    app/panels/browser_panel.cpp
    app/panels/items_panel.cpp
    app/panels/problems_panel.cpp
    app/panels/recipes_panel.cpp
    app/panels/log_panel.cpp
    app/panels/npcs_panel.cpp
    app/panels/project_panel.cpp
    app/panels/settings_panel.cpp
)
target_include_directories(risen-modkit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" ${STB_INCLUDE_DIRS})
target_link_libraries(risen-modkit PRIVATE
    modkit_core glfw glad::glad imgui::imgui unofficial::imgui-node-editor::imgui-node-editor imguizmo::imguizmo nfd::nfd OpenGL::GL)
if(WIN32)
    # a GUI application without a console window; main() stays the entry point
    set_target_properties(risen-modkit PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_link_options(risen-modkit PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()

if(SPASS_BUILD_TESTS)
    add_executable(modkit_tests "${CMAKE_SOURCE_DIR}/tests/modkit/modkit_tests.cpp")
    target_link_libraries(modkit_tests PRIVATE modkit_core)
    target_compile_definitions(modkit_tests PRIVATE MODKIT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME modkit_tests COMMAND modkit_tests)
endif()
