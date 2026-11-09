cmake_minimum_required(VERSION 3.21)

option(FORMAT_VALIDATION_ONLY "Configure only clang-format targets without native dependencies" OFF)
if(FORMAT_VALIDATION_ONLY)
    project(FormatValidation LANGUAGES NONE)
    include(cmake/FormatValidation.cmake)
    return()
endif()

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(IMGUIExtension LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "IMGUIExtension requires Windows MSVC x86. Configure with -A Win32.")
endif()

get_property(imguiextension_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(imguiextension_multi_config)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported configurations" FORCE)
elseif(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Select Debug or Release.")
endif()

# Standalone checkouts keep every artifact inside this directory (build/x86/<config>
# and install/x86/<config>) instead of the generator's default Program Files prefix.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    get_filename_component(_imguiextension_build_leaf "${CMAKE_BINARY_DIR}" NAME)
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND _imguiextension_build_leaf MATCHES "^(Debug|Release)$")
        set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/install/x86/${_imguiextension_build_leaf}"
            CACHE PATH "Install prefix for this plugin" FORCE)
    endif()
    unset(_imguiextension_build_leaf)
endif()

include(cmake/Dependencies.cmake)
imguiextension_prepare_dependencies()
include(cmake/SDK.cmake)
include(cmake/Sources.cmake)
include(cmake/SourceGroups.cmake)

# The plugin version is stamped at configure time. It is generated into the build
# tree so that building never leaves a modified source file behind.
set(IMGUIEXTENSION_VERSION "" CACHE STRING "Plugin version string; empty stamps the configure time")
if(NOT IMGUIEXTENSION_VERSION)
    string(TIMESTAMP _imguiextension_stamp "%y%m%d.%H%M")
    set(IMGUIEXTENSION_VERSION "1.0.0-dev.${_imguiextension_stamp}")
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in"
    "${PROJECT_BINARY_DIR}/generated/version.h" @ONLY)
message(STATUS "IMGUIExtension version: ${IMGUIEXTENSION_VERSION}")

add_library(IMGUIExtension SHARED ${IMGUIEXTENSION_SOURCES} ${IMGUIEXTENSION_IMGUI_SOURCES})
imguiextension_assign_source_groups(IMGUIExtension)
imguiextension_use_sdk(IMGUIExtension)

target_compile_options(IMGUIExtension PRIVATE
    /MP
    /Zi
    /W3
    /wd4091
    /wd4291
    /wd4311
    /wd4312
    /wd4819
    /wd4996)

# OpenGL2 backend for the engine's fixed-function GL rendering context.
target_link_libraries(IMGUIExtension PRIVATE opengl32)

target_link_options(IMGUIExtension PRIVATE
    /DEBUG
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>)

set_target_properties(IMGUIExtension PROPERTIES
    DEBUG_POSTFIX ""
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

install(TARGETS IMGUIExtension RUNTIME DESTINATION svencoop/metahook/plugins)
install(FILES "$<TARGET_PDB_FILE:IMGUIExtension>" DESTINATION svencoop/metahook/plugins OPTIONAL)

# Dispatcher tests compile Dear ImGui without a backend because SafeInvoke inspects
# the current context, and keep assertions enabled in Release (/UNDEBUG).
option(IMGUIEXTENSION_BUILD_TESTS "Build the dispatcher regression tests" ON)
if(IMGUIEXTENSION_BUILD_TESTS)
    enable_testing()
    add_executable(IMGUIExtensionDispatcherTests
        tests/dispatcher_tests.cpp
        "${IMGUI_SOURCE_PATH}/imgui.cpp"
        "${IMGUI_SOURCE_PATH}/imgui_draw.cpp"
        "${IMGUI_SOURCE_PATH}/imgui_tables.cpp"
        "${IMGUI_SOURCE_PATH}/imgui_widgets.cpp")
    imguiextension_use_sdk(IMGUIExtensionDispatcherTests)
    target_compile_definitions(IMGUIExtensionDispatcherTests PRIVATE _CONSOLE)
    target_compile_options(IMGUIExtensionDispatcherTests PRIVATE /UNDEBUG /W3)
    add_test(NAME IMGUIExtensionDispatcherTests COMMAND IMGUIExtensionDispatcherTests)

    # Range combination is header-only; the missing-font case needs the ImGui
    # library because it verifies the ImFontFlags_NoLoadError path.
    add_executable(IMGUIExtensionFontTests
        tests/font_glyph_ranges_tests.cpp
        "${IMGUI_SOURCE_PATH}/imgui.cpp"
        "${IMGUI_SOURCE_PATH}/imgui_draw.cpp"
        "${IMGUI_SOURCE_PATH}/imgui_tables.cpp"
        "${IMGUI_SOURCE_PATH}/imgui_widgets.cpp")
    imguiextension_use_sdk(IMGUIExtensionFontTests)
    target_compile_definitions(IMGUIExtensionFontTests PRIVATE _CONSOLE)
    target_compile_options(IMGUIExtensionFontTests PRIVATE /UNDEBUG /W3)
    add_test(NAME IMGUIExtensionFontTests COMMAND IMGUIExtensionFontTests)
endif()

# The aggregate solution owns its single LaunchGame target.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    option(METAHOOKSV_ENABLE_LAUNCH_GAME "Add the Visual Studio build/deploy/debug target" OFF)
    if(METAHOOKSV_ENABLE_LAUNCH_GAME)
        include(cmake/LaunchGame.cmake)
        metahooksv_add_launch_game()
    endif()
endif()

# Shared formatting targets are explicit; ordinary native builds do not run them.
include(cmake/FormatValidation.cmake)
