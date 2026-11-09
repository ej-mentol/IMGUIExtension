set(METAHOOK_SOURCE_PATH "$ENV{METAHOOK_SOURCE_PATH}" CACHE PATH
    "MetaHook source tree; empty fetches the SDK main branch")
set(VGUI2EXTENSION_SOURCE_PATH "$ENV{VGUI2EXTENSION_SOURCE_PATH}" CACHE PATH
    "VGUI2Extension source tree providing include/Interface/IVGUI2Extension.h; empty uses a sibling checkout or fetches it")
set(IMGUI_SOURCE_PATH "${PROJECT_SOURCE_DIR}/thirdparty/imgui" CACHE PATH
    "Dear ImGui source tree; it is a public dependency of consumer plugins, so it stays inside this repository")
set(RAPIDJSON_SOURCE_PATH "$ENV{RAPIDJSON_SOURCE_PATH}" CACHE PATH
    "RapidJSON source tree; empty uses thirdparty/rapidjson, then fetches the pinned commit")
set(SDL2_INCLUDE_DIRS "$ENV{SDL2_INCLUDE_DIRS}" CACHE STRING
    "Directories containing SDL2/SDL.h (headers only; SDL is neither built nor linked)")

function(imguiextension_validate_source variable source)
    foreach(required IN LISTS ARGN)
        if(NOT EXISTS "${source}/${required}" OR IS_DIRECTORY "${source}/${required}")
            message(FATAL_ERROR "${variable} is missing ${required}: ${source}")
        endif()
    endforeach()
endfunction()

function(imguiextension_fetch_source name url tag out_var)
    include(FetchContent)
    FetchContent_Populate(${name}
        GIT_REPOSITORY "${url}"
        GIT_TAG "${tag}"
        GIT_SUBMODULES ""
        GIT_SUBMODULES_RECURSE FALSE
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/${name}-src")
    string(TOLOWER "${name}" name_lower)
    set(${out_var} "${${name_lower}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(imguiextension_prepare_dependencies)
    # Validate every explicit path before downloading anything. External sources
    # are read-only inputs; this repository never writes into them.
    if(METAHOOK_SOURCE_PATH)
        get_filename_component(metahook_source "${METAHOOK_SOURCE_PATH}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
        imguiextension_validate_source(METAHOOK_SOURCE_PATH "${metahook_source}"
            include/metahook.h include/HLSDK/common/interface.cpp include/HLSDK/common/in_buttons.h
            include/HLSDK/common/usercmd.h include/HLSDK/common/cvardef.h include/Interface/IPlugins.h)
    endif()
    if(VGUI2EXTENSION_SOURCE_PATH)
        get_filename_component(vgui2extension_source "${VGUI2EXTENSION_SOURCE_PATH}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
        imguiextension_validate_source(VGUI2EXTENSION_SOURCE_PATH "${vgui2extension_source}"
            include/Interface/IVGUI2Extension.h)
    endif()
    if(NOT METAHOOK_SOURCE_PATH)
        include(FetchContent)
        FetchContent_Declare(imguiextension_metahook
            GIT_REPOSITORY https://github.com/MetaHookSv/MetaHook
            # MetaHook is tracked as a branch: always fetch the latest main.
            GIT_TAG origin/main
            GIT_SUBMODULES ""
            GIT_SUBMODULES_RECURSE FALSE
            SOURCE_SUBDIR include)
        FetchContent_MakeAvailable(imguiextension_metahook)
        set(metahook_source "${imguiextension_metahook_SOURCE_DIR}")
    endif()
    imguiextension_validate_source(METAHOOK_SOURCE_PATH "${metahook_source}"
        include/metahook.h include/HLSDK/common/interface.cpp include/HLSDK/common/in_buttons.h
        include/HLSDK/common/usercmd.h include/HLSDK/common/cvardef.h include/Interface/IPlugins.h)
    message(STATUS "METAHOOK_SOURCE_PATH: ${metahook_source}")

    if(NOT VGUI2EXTENSION_SOURCE_PATH)
        # A standalone checkout usually sits next to the aggregator's plugin tree;
        # accept that sibling before reaching for the network.
        if(EXISTS "${PROJECT_SOURCE_DIR}/../VGUI2Extension/include/Interface/IVGUI2Extension.h")
            get_filename_component(vgui2extension_source "${PROJECT_SOURCE_DIR}/../VGUI2Extension" ABSOLUTE)
        else()
            imguiextension_fetch_source(imguiextension_vgui2extension
                "https://github.com/MetaHookSv/VGUI2Extension"
                "cd7ef6e3b7fb51d3c98e6d7dadec02dd1fa08c4f" vgui2extension_source)
        endif()
    endif()
    imguiextension_validate_source(VGUI2EXTENSION_SOURCE_PATH "${vgui2extension_source}"
        include/Interface/IVGUI2Extension.h)
    message(STATUS "VGUI2EXTENSION_SOURCE_PATH: ${vgui2extension_source}")

    # Dear ImGui is bundled: consumer plugins compile their own copy and include
    # these headers through this repository's stable thirdparty/imgui path.
    get_filename_component(imgui_source "${IMGUI_SOURCE_PATH}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    imguiextension_validate_source(IMGUI_SOURCE_PATH "${imgui_source}"
        imgui.h imgui_internal.h imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
        backends/imgui_impl_win32.cpp backends/imgui_impl_win32.h
        backends/imgui_impl_opengl2.cpp backends/imgui_impl_opengl2.h)
    message(STATUS "IMGUI_SOURCE_PATH: ${imgui_source}")

    # RapidJSON is part of MetaHook's shared dependency set, so this repository no
    # longer carries a second copy. Prefer the injected tree, then the sibling
    # MetaHook checkout, then the pinned upstream commit used during development.
    if(RAPIDJSON_SOURCE_PATH)
        get_filename_component(rapidjson_source "${RAPIDJSON_SOURCE_PATH}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    elseif(EXISTS "${metahook_source}/thirdparty/rapidjson/include/rapidjson/document.h")
        get_filename_component(rapidjson_source "${metahook_source}/thirdparty/rapidjson" ABSOLUTE)
    else()
        imguiextension_fetch_source(imguiextension_rapidjson
            "https://github.com/Tencent/rapidjson"
            # v1.1.0-763; the commit this plugin was developed and verified against.
            "24b5e7a8b27f42fa16b96fc70aade9106cf7102f" rapidjson_source)
    endif()
    imguiextension_validate_source(RAPIDJSON_SOURCE_PATH "${rapidjson_source}" include/rapidjson/document.h)
    message(STATUS "RAPIDJSON_SOURCE_PATH: ${rapidjson_source}")

    # SDL2 headers only: the plugin resolves SDL2.dll exports at runtime and
    # never links or rebuilds SDL. Fall back to the SDK tree shipped with MetaHook.
    if(SDL2_INCLUDE_DIRS)
        set(sdl2_includes)
        set(sdl2_found FALSE)
        foreach(directory IN LISTS SDL2_INCLUDE_DIRS)
            get_filename_component(directory "${directory}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
            if(NOT IS_DIRECTORY "${directory}")
                message(FATAL_ERROR "SDL2_INCLUDE_DIRS directory does not exist: ${directory}")
            endif()
            if(EXISTS "${directory}/SDL2/SDL.h" AND EXISTS "${directory}/SDL2/SDL_syswm.h")
                set(sdl2_found TRUE)
            endif()
            list(APPEND sdl2_includes "${directory}")
        endforeach()
        if(NOT sdl2_found)
            message(FATAL_ERROR "SDL2_INCLUDE_DIRS must provide SDL2/SDL.h and SDL2/SDL_syswm.h")
        endif()
    else()
        set(sdl2_fallback "${metahook_source}/thirdparty/sdl2-compat/include")
        if(NOT EXISTS "${sdl2_fallback}/SDL2/SDL_syswm.h")
            message(FATAL_ERROR "Set SDL2_INCLUDE_DIRS to a directory providing SDL2/SDL_syswm.h. "
                "IMGUIExtension consumes SDL2 headers only and does not fetch or build SDL.")
        endif()
        set(sdl2_includes "${sdl2_fallback}")
    endif()
    message(STATUS "SDL2 include directories: ${sdl2_includes}")

    set(METAHOOK_SOURCE_PATH "${metahook_source}" PARENT_SCOPE)
    set(VGUI2EXTENSION_SOURCE_PATH "${vgui2extension_source}" PARENT_SCOPE)
    set(IMGUI_SOURCE_PATH "${imgui_source}" PARENT_SCOPE)
    set(RAPIDJSON_SOURCE_PATH "${rapidjson_source}" PARENT_SCOPE)
    set(IMGUIEXTENSION_SDL2_INCLUDE_DIRS "${sdl2_includes}" PARENT_SCOPE)
endfunction()
