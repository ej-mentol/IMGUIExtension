#pragma once

#ifndef IMGUI_EXTENSION_INTERFACE_H
#define IMGUI_EXTENSION_INTERFACE_H

#include <interface.h>
#include <windows.h>

struct ImGuiContext;
struct cl_enginefuncs_s;
typedef struct cl_enginefuncs_s cl_enginefunc_t;

class IImGuiExtensionCallbacks : public IBaseInterface
{
public:
	virtual int GetAltitude() const = 0;
	virtual const char* GetModuleName() const = 0;

	// Lifecycle notifications called by the core extension
	virtual void Initialize() {}
	virtual void Start(cl_enginefunc_t* engineFuncs, int interfaceVersion) {}
	virtual void Shutdown() {}

	// Called inside the frame drawing loop when ImGui context is active.
	// NOTE: Each plugin DLL contains its own static ImGui library, which means
	// it has its own global `GImGui` variable. Therefore, the plugin MUST call
	// `ImGui::SetCurrentContext(g_pImGuiExtension->GetImGuiContext())` as the first
	// step in this callback before calling any other ImGui functions.
	virtual void OnImGuiFrame() = 0;

	// Return true if this module currently requires mouse/keyboard exclusive input.
	// The arbiter will automatically suppress game mouse look/keys if any plugin wants input capture.
	virtual bool WantsInputCapture() const = 0;

	// Return true if this module allows this specific key to pass through to the engine
	// even when WantsInputCapture() is true.
	//
	// keynum is the key number forwarded verbatim by VGUI2Extension's Key_Event. It is
	// NOT a Windows VK_* code (the engine key-number domain is documented in HLSDK
	// keydefs.h: printable keys are lowercased ASCII, special keys start at 0x80).
	// pszCurrentBinding is never null and is only valid for the current keydown; the
	// host does not invoke this callback for keyup events.
	//
	// Unbound keys are not captured at all, and the host already passes through any
	// binding that contains "toggleconsole", so returning false here cannot lock the
	// player out of the console. Keep this callback for UI-specific keys only.
	virtual bool AllowKeyPassthrough(int keynum, const char* pszCurrentBinding) const
	{
		(void)keynum;
		(void)pszCurrentBinding;
		return false;
	}

	// Called by the core to force release any input capture (e.g. during map loading or disconnect).
	// Consumer DLLs should reset their UI state (e.g. set visible/open flags to false) in this callback.
	virtual void OnForceRelease() {}
};

class IImGuiExtension : public IBaseInterface
{
public:
	virtual void RegisterCallbacks(IImGuiExtensionCallbacks* cb) = 0;
	virtual void UnregisterCallbacks(IImGuiExtensionCallbacks* cb) = 0;

	// The context is created lazily on the first rendered frame and lives until the
	// plugin is unloaded. A video mode change (vid_restart, windowed↔fullscreen)
	// rebuilds the backends but keeps the context, so the pointer stays valid for
	// the whole engine session. Never cache it across GetPluginModuleHandle-based
	// re-resolution, and always call ImGui::SetCurrentContext() from OnImGuiFrame.
	virtual ImGuiContext* GetImGuiContext() = 0;

	// Retrieve raw relative mouse movement accumulated during IN_Accumulate.
	// This avoids race conditions / duplicate polling when relative mode is active.
	virtual void GetRawMouseDelta(float* mx, float* my) = 0;
};

#define IMGUI_EXTENSION_INTERFACE_VERSION "ImGuiExtension001"

#endif // IMGUI_EXTENSION_INTERFACE_H
