#pragma once

#include <metahook.h>
#include <windows.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

typedef int (*pfnSDL_GetRelativeMouseState)(int* x, int* y);

extern pfnSDL_GetRelativeMouseState g_pfnSDL_GetRelativeMouseState;

// SDL_SetRelativeMouseMode(SDL_bool enabled)
typedef int (*pfnSDL_SetRelativeMouseMode)(SDL_bool enabled);
extern pfnSDL_SetRelativeMouseMode g_pfnSDL_SetRelativeMouseMode;

typedef int (*pfnSDL_GetWindowWMInfo)(SDL_Window* window, SDL_SysWMinfo* info);
extern pfnSDL_GetWindowWMInfo g_pfnSDL_GetWindowWMInfo;

// Returns the engine's main SDL_Window* (or HWND on non-SDL2 engines)
typedef void* (*pfnSys_GetMainWindow)(void);
extern pfnSys_GetMainWindow g_pfnSys_GetMainWindow;

typedef void (*pfnIN_ActivateMouse)(void);
typedef void (*pfnIN_DeactivateMouse)(void);

extern pfnIN_ActivateMouse g_pfnIN_ActivateMouse;
extern pfnIN_DeactivateMouse g_pfnIN_DeactivateMouse;

void PrivateFuncs_Init(void);
