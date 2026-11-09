# Explicit compile list. Dear ImGui is compiled into this DLL (consumers carry
# their own copy), and interface.cpp provides the HLSDK interface plumbing.
set(IMGUIEXTENSION_SOURCES
    src/plugins.cpp
    src/exportfuncs.cpp
    src/privatefuncs.cpp
    src/InputCapture.cpp
    src/VGUI2Callbacks.cpp
    src/FontAtlas.cpp
    "${METAHOOK_SOURCE_PATH}/include/HLSDK/common/interface.cpp")

set(IMGUIEXTENSION_IMGUI_SOURCES
    "${IMGUI_SOURCE_PATH}/imgui.cpp"
    "${IMGUI_SOURCE_PATH}/imgui_draw.cpp"
    "${IMGUI_SOURCE_PATH}/imgui_tables.cpp"
    "${IMGUI_SOURCE_PATH}/imgui_widgets.cpp"
    "${IMGUI_SOURCE_PATH}/backends/imgui_impl_win32.cpp"
    "${IMGUI_SOURCE_PATH}/backends/imgui_impl_opengl2.cpp")
