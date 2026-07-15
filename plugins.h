#pragma once

#include <metahook.h>
#include <string>
#include <in_buttons.h>
#include <usercmd.h>
#include "IImGuiExtension.h"

// Tracks the buttons bitmask from the last CL_CreateMove call.
// Used at capture ON to build the restore snapshot — fully bind-agnostic.
extern unsigned short g_LastButtons;

extern cl_exportfuncs_t gExportfuncs;
extern cl_enginefunc_t gEngfuncs;
extern metahook_api_t* g_pMetaHookAPI;
extern mh_interface_t* g_pInterface;
extern mh_enginesave_t* g_pMetaSave;
extern IFileSystem* g_pFileSystem;
extern IFileSystem_HL25* g_pFileSystem_HL25;

extern int glwidth, glheight;
extern int* g_iVisibleMouse;

extern float g_vRawDeltaX;
extern float g_vRawDeltaY;

// Forward declarations for exports
int HUD_Redraw(float time, int intermission);
void CL_CreateMove(float frametime, struct usercmd_s* cmd, int active);
void HUD_Shutdown(void);
void IN_MouseEvent(int mstate);
void IN_Accumulate(void);

// Hooked function pointers
extern int (*g_pfnHUD_Redraw)(float time, int intermission);
extern void (*g_pfnCL_CreateMove)(float frametime, struct usercmd_s* cmd, int active);
extern void (*g_pfnIN_MouseEvent)(int mstate);
extern void (*g_pfnIN_Accumulate)(void);

void PrivateFuncs_Init();
void ImGui_InitOnce();
void ImGui_Shutdown();
bool AnyWantsInputCapture();

class CImGuiExtension : public IImGuiExtension
{
public:
	void RegisterCallbacks(IImGuiExtensionCallbacks* cb) override;
	void UnregisterCallbacks(IImGuiExtensionCallbacks* cb) override;
	ImGuiContext* GetImGuiContext() override;
	void GetRawMouseDelta(float* mx, float* my) override;
};

extern CImGuiExtension g_ImGuiExtension;
