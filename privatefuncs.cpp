#include <metahook.h>
#include "plugins.h"
#include "privatefuncs.h"

pfnSDL_GetRelativeMouseState g_pfnSDL_GetRelativeMouseState = nullptr;
pfnSDL_SetRelativeMouseMode g_pfnSDL_SetRelativeMouseMode = nullptr;
pfnSDL_GetWindowWMInfo g_pfnSDL_GetWindowWMInfo = nullptr;
pfnSys_GetMainWindow g_pfnSys_GetMainWindow = nullptr;
pfnIN_ActivateMouse g_pfnIN_ActivateMouse = nullptr;
pfnIN_DeactivateMouse g_pfnIN_DeactivateMouse = nullptr;

void PrivateFuncs_Init(void)
{
	g_pfnIN_ActivateMouse = gExportfuncs.IN_ActivateMouse;
	g_pfnIN_DeactivateMouse = gExportfuncs.IN_DeactivateMouse;

	HMODULE hSDL2 = GetModuleHandleA("SDL2.dll");
	if (hSDL2)
	{
		g_pfnSDL_GetRelativeMouseState = (pfnSDL_GetRelativeMouseState)GetProcAddress(hSDL2, "SDL_GetRelativeMouseState");
		g_pfnSDL_GetWindowWMInfo = (pfnSDL_GetWindowWMInfo)GetProcAddress(hSDL2, "SDL_GetWindowWMInfo");
		g_pfnSDL_SetRelativeMouseMode = (pfnSDL_SetRelativeMouseMode)GetProcAddress(hSDL2, "SDL_SetRelativeMouseMode");
	}

	// Sys_GetMainWindow is an engine export — returns SDL_Window* on SDL2, or HWND on legacy
	HMODULE hEngine = GetModuleHandleA("hw.dll");
	if (!hEngine) hEngine = GetModuleHandleA("sw.dll");
	if (!hEngine) hEngine = GetModuleHandleA("engine.dll"); // SvEngine / HL25
	if (hEngine)
	{
		g_pfnSys_GetMainWindow = (pfnSys_GetMainWindow)GetProcAddress(hEngine, "Sys_GetMainWindow");
	}
}
