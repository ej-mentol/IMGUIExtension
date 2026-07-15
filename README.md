# IMGUIExtension

`IMGUIExtension` is a core arbitrator/host plugin designed to integrate Dear ImGui into Sven Co-op using the MetaHook framework. By managing the global ImGui context, Win32 window message interception, and OpenGL rendering state, it provides a stable environment for lightweight consumer plugins (such as custom menus or HUD overlays) without requiring them to bundle individual rendering backends.

---

## Technical Overview

The plugin solves several architecture-specific hurdles inherent to GoldSrc/Sven Co-op's rendering pipeline and VGUI2 focus management.

### Key Architectural Features

1. **Unified Context & Font Atlas**
   The global `ImGuiContext` is created lazily on the first rendering frame. Fonts are loaded and merged dynamically based on the local configuration file `imguiextension/imguiextension.json` before backend compilation. By default, the extension:
   * **Configures Local Fonts**: Automatically lists and configures TrueType font files placed in the `imguiextension/fonts/` directory.
   * **Supports Merged Glyphs**: Allows merging multiple icon or symbol fonts (such as Font Awesome or Segoe UI Symbol) using ImGui's `MergeMode = true`, covering standard Latin, Cyrillic, and Private Use Area (PUA) symbol charts `[0xF000, 0xFFFF]`.

2. **Cached HWND Arbitrator**
   Sven Co-op's VGUI2 engine creates native Win32 child text boxes (e.g., when the chat box gains focus), which collapses the standard active window metrics. `IMGUIExtension` uses an `EnumWindows` callback to match the current process ID against class names `"SDL_app"` or `"ValveHalfLifeWindow"`, caching the true top-level game window. This ensures consistent viewport dimensions and prevents coordinate collapsing.

3. **SDL2 Relative Mouse Mode Sync**
   Properly intercepts and deactivates SDL2 relative mouse mode when consumer UIs request input capture. When capture is released, it restores the relative state to prevent the in-game camera from snapping.

4. **Resolution Switch Safeguards**
   GoldSrc destroys and re-creates the OpenGL device context upon video mode changes or resolution switches. `IMGUIExtension` catches this during `BaseUI::Paint` and `HUD_VidInit`, tearing down old backends and re-initializing GL2/Win32 context states dynamically.

5. **Robust Reentrant Dispatcher**
   To safely host multiple independent consumer plugins, the core utilizes a highly resilient dispatch system (`CImGuiDispatcher`). It prevents common modding crashes through:
   * **Zero-Iterator-Invalidation**: All callback iterations use safe index-based loops and a recursion depth counter instead of standard iterators.
   * **Deferred Mutation Queues**: If a consumer plugin registers or unregisters itself (or other components) in the middle of a dispatch loop or an input event, the changes are safely queued and resolved afterward.
   * **SEH Protection**: Consumer callback invocations and input checks are wrapped in Structured Exception Handling (`__try` / `__except`) blocks, ensuring a crash or null-pointer dereference in a client plugin does not crash the host extension or the game process.

---

## Project Build & Compilation

* **Build Method**: Open the MetaHook solution (`.sln`) in Visual Studio and build the `IMGUIExtension` project.
* **Output Directories**: Compiled binaries (`IMGUIExtension.dll`) are placed inside:
  * `Plugins/IMGUIExtension/Release`
  * `Plugins/IMGUIExtension/Release_AVX2` (optimized build leveraging AVX2 instructions)

## Concurrency & Safety

* **C++ Standard**: Compiled under C++20 (`stdcpp20`).
* **Thread Model**: Single-threaded by design. All dispatcher state (`CImGuiDispatcher`), registration, and callback iteration run exclusively on the engine's main thread inside GoldSrc's hook points (`HUD_Init`, `BaseUI::Paint`, `IN_*`). There is no internal locking - consumers must not call `RegisterCallbacks`/`UnregisterCallbacks` or any `IImGuiExtension` method from a worker thread.
* **Reentrancy & Mutation Safety**: The dispatcher explicitly supports *reentrant* calls and dynamic structural modifications. Registration/unregistration during active iteration at any depth is deferred to a queue and applied cleanly once the outermost execution loop unwinds.
* Consumers must avoid long-running or blocking code in `OnImGuiFrame` callbacks to maintain consistent rendering frame times.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.