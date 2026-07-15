#include "plugins.h"
#include "privatefuncs.h"
#include "exportfuncs.h"
#include "InputCapture.h"
#include <interface.h>
#include "version.h"

cl_enginefunc_t  gEngfuncs = { 0 };
cl_exportfuncs_t gExportfuncs = { 0 };
metahook_api_t*  g_pMetaHookAPI = nullptr;
mh_interface_t*  g_pInterface   = nullptr;
mh_enginesave_t* g_pMetaSave    = nullptr;
IFileSystem*     g_pFileSystem  = nullptr;
IFileSystem_HL25* g_pFileSystem_HL25 = nullptr;

void (*g_pfnHUD_Init)(void) = nullptr;
int  (*g_pfnHUD_VidInit)(void) = nullptr;
int  (*g_pfnHUD_Redraw)(float time, int intermission) = nullptr;
void (*g_pfnCL_CreateMove)(float frametime, struct usercmd_s* cmd, int active) = nullptr;
void (*g_pfnIN_MouseEvent)(int mstate) = nullptr;
void (*g_pfnIN_Accumulate)(void) = nullptr;

int glwidth = 0;
int glheight = 0;
float g_vRawDeltaX = 0.0f;
float g_vRawDeltaY = 0.0f;
unsigned short g_LastButtons = 0;

void HUD_Init(void)
{
	g_pfnHUD_Init();
	g_Dispatcher.Start(&gEngfuncs, METAHOOK_API_VERSION);
}

// Called by the engine whenever video mode changes (resolution, fullscreen toggle).
// GoldSrc destroys and recreates the OpenGL context here — we must reinit GL resources
// and reinstall the WndProc subclass (engine may reset GWLP_WNDPROC on context recreation).
int HUD_VidInit(void)
{
	int result = g_pfnHUD_VidInit ? g_pfnHUD_VidInit() : 1;
	ImGui_VidReinit();
	return result;
}

void IPluginsV4::Init(metahook_api_t* pAPI, mh_interface_t* pIface, mh_enginesave_t* pSave)
{
	g_pMetaHookAPI = pAPI;
	g_pInterface   = pIface;
	g_pMetaSave    = pSave;

	// Initialize filesystem pointers — same pattern as BulletPhysics/plugins.cpp
	g_pFileSystem = pIface->FileSystem;
	if (!g_pFileSystem)
		g_pFileSystem_HL25 = pIface->FileSystem_HL25;
}

void IPluginsV4::Shutdown(void)
{
	g_Dispatcher.Shutdown();
	IMGUI_VGUI2Extension_Shutdown();
}

void IPluginsV4::LoadEngine(cl_enginefunc_t* pEngfuncs)
{
	memcpy(&gEngfuncs, pEngfuncs, sizeof(gEngfuncs));
}

void IPluginsV4::LoadClient(cl_exportfuncs_t* pExportFunc)
{
	if (!pExportFunc) return;
	memcpy(&gExportfuncs, pExportFunc, sizeof(gExportfuncs));

	g_pfnHUD_Init = pExportFunc->HUD_Init;
	pExportFunc->HUD_Init = HUD_Init;

	g_pfnHUD_VidInit = pExportFunc->HUD_VidInit;
	pExportFunc->HUD_VidInit = HUD_VidInit;

	g_pfnHUD_Redraw = pExportFunc->HUD_Redraw;
	pExportFunc->HUD_Redraw = HUD_Redraw;

	g_pfnIN_MouseEvent = pExportFunc->IN_MouseEvent;
	pExportFunc->IN_MouseEvent = IN_MouseEvent;

	g_pfnIN_Accumulate = pExportFunc->IN_Accumulate;
	pExportFunc->IN_Accumulate = IN_Accumulate;

	g_pfnCL_CreateMove = pExportFunc->CL_CreateMove;
	pExportFunc->CL_CreateMove = CL_CreateMove;

	PrivateFuncs_Init();
	IMGUI_VGUI2Extension_Init();

	g_Dispatcher.Initialize();
}

void IPluginsV4::ExitGame(int iResult)
{
	g_Dispatcher.Shutdown();
	IMGUI_VGUI2Extension_Shutdown();
	ImGui_Shutdown();
}

// CImGuiExtension implementation
void CImGuiExtension::RegisterCallbacks(IImGuiExtensionCallbacks* cb)
{
	g_Dispatcher.Register(cb);
}

void CImGuiExtension::UnregisterCallbacks(IImGuiExtensionCallbacks* cb)
{
	g_Dispatcher.Unregister(cb);
}

ImGuiContext* CImGuiExtension::GetImGuiContext()
{
	ImGui_InitOnce();
	return g_pImGuiContext;
}

void CImGuiExtension::GetRawMouseDelta(float* mx, float* my)
{
	if (mx) *mx = g_vRawDeltaX;
	if (my) *my = g_vRawDeltaY;
}

CImGuiExtension g_ImGuiExtension;

const char* IPluginsV4::GetVersion(void)
{
	return IMGUI_EXTENSION_VERSION;
}

EXPOSE_SINGLE_INTERFACE(IPluginsV4, IPluginsV4, METAHOOK_PLUGIN_API_VERSION_V4);
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CImGuiExtension, IImGuiExtension, IMGUI_EXTENSION_INTERFACE_VERSION, g_ImGuiExtension);
