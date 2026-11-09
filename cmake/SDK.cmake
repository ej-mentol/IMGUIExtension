# Shared compile interface: MetaHook SDK headers, HLSDK/SourceSDK headers,
# vendor headers and the C++20 language level used by every target here.
function(imguiextension_use_sdk target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE
        "${PROJECT_BINARY_DIR}/generated"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.."
        "${METAHOOK_SOURCE_PATH}/include"
        "${METAHOOK_SOURCE_PATH}/include/Interface"
        "${METAHOOK_SOURCE_PATH}/include/Interface/VGUI"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/common"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/engine"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/cl_dll"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/pm_shared"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/public"
        "${METAHOOK_SOURCE_PATH}/include/VGUI"
        "${METAHOOK_SOURCE_PATH}/include/vgui_controls"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier0"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier1"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier2"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier3"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/vstdlib"
        "${VGUI2EXTENSION_SOURCE_PATH}/include"
        "${VGUI2EXTENSION_SOURCE_PATH}/include/Interface"
        "${IMGUI_SOURCE_PATH}"
        "${IMGUI_SOURCE_PATH}/backends"
        "${RAPIDJSON_SOURCE_PATH}/include"
        ${IMGUIEXTENSION_SDL2_INCLUDE_DIRS})
    target_compile_definitions(${target} PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        _MBCS
        _CRT_SECURE_NO_WARNINGS
        NO_MALLOC_OVERRIDE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>)

    # This project does not use C++20 modules. On Visual Studio 18 the module pipeline otherwise
    # runs on every build (ComputeStdModulesCompileInputs / SetModuleDependencies), parses the
    # STL modules manifest and can fail with MSB4018 + OutOfMemoryException. CXX_SCAN_FOR_MODULES
    # (CMake 3.28+) disables per-source scanning; VS_GLOBAL_EnableCppModules disables the MSBuild
    # targets themselves for Visual Studio generators (ignored by other generators).
    set_target_properties(${target} PROPERTIES VS_GLOBAL_EnableCppModules "false")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    endif()
endfunction()
