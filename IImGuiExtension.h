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

	// Return true if this module allows this specific key to pass through to the engine even when WantsInputCapture() is true.
	// Defaults to allowing the console tilde key (VK_OEM_3 = 192) to prevent locking players out of the game console.
	virtual bool AllowKeyPassthrough(int keynum) const
	{
		if (keynum == VK_OEM_3) // '`~' tilde key — opens game console
			return true;
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
	virtual ImGuiContext* GetImGuiContext() = 0;

	// Retrieve raw relative mouse movement accumulated during IN_Accumulate.
	// This avoids race conditions / duplicate polling when relative mode is active.
	virtual void GetRawMouseDelta(float* mx, float* my) = 0;
};

#define IMGUI_EXTENSION_INTERFACE_VERSION "ImGuiExtension001"

#endif // IMGUI_EXTENSION_INTERFACE_H
