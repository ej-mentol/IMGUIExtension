# IMGUIExtension

`IMGUIExtension` is a core arbitrator/host plugin designed to integrate Dear ImGui into Sven Co-op using the MetaHook framework. By managing the global ImGui context, Win32 window message interception, and OpenGL rendering state, it provides a stable environment for lightweight consumer plugins (such as custom menus or HUD overlays) without requiring them to bundle individual rendering backends.

---

## Technical Overview

The plugin solves several architecture-specific hurdles inherent to GoldSrc/Sven Co-op's rendering pipeline and VGUI2 focus management.

### Key Architectural Features

1. **Unified Context & Font Atlas**
   The global `ImGuiContext` is created lazily on the first rendering frame. Fonts are loaded and merged **before** backend compilation to ensure structural integrity of the font atlas.
   * **Extended Unicode Support**: Loads `segoeui.ttf` with raw glyph ranges `[0x0020, 0x00FF]` and `[0x0400, 0x052F]`.
   * **Unicode Symbol Merging**: Merges `seguisym.ttf` (Segoe UI Symbol) with `MergeMode = true` covering geometric shapes, miscellaneous symbols, and dingbats `[0x25A0, 0x27BF]`. This allows consumers to render vector-like icons from standard 16-bit Unicode symbol charts.

2. **Cached HWND Arbitrator**
   Sven Co-op's VGUI2 engine creates native Win32 child text boxes (e.g., when the chat box gains focus), which collapses the standard active window metrics. `IMGUIExtension` uses an `EnumWindows` callback to match the current process ID against class names `"SDL_app"` or `"ValveHalfLifeWindow"`, caching the true top-level game window. This ensures consistent viewport dimensions and prevents coordinate collapsing.

3. **SDL2 Relative Mouse Mode Sync**
   Properly intercepts and deactivates SDL2 relative mouse mode when consumer UIs request input capture. When capture is released, it restores the relative state to prevent the in-game camera from snapping.

4. **Resolution Switch Safeguards**
   GoldSrc destroys and re-creates the OpenGL device context upon video mode changes or resolution switches. `IMGUIExtension` catches this during `BaseUI::Paint` and `HUD_VidInit`, tearing down old backends and re-initializing GL2/Win32 context states dynamically.

---

## Project Build & Compilation

* **Build Method**: Open the MetaHook solution (`.sln`) in Visual Studio and build the `IMGUIExtension` project.
* **Output Directories**: Compiled binaries (`IMGUIExtension.dll`) are placed inside:
  * `Plugins/IMGUIExtension/Release`
  * `Plugins/IMGUIExtension/Release_AVX2` (optimized build leveraging AVX2 instructions)

## Concurrency & Safety

* **C++ Standard**: Compiled under C++20 (`stdcpp20`).
* **Thread Safety**: Global callback registration states should leverage atomic operations or lock-free queue models if processed across multiple threads. Consumers must avoid long-running or blocking code in `OnImGuiFrame` callbacks to maintain consistent rendering frame times.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
