#include <metahook.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_opengl2.h>

#include "plugins.h"
#include "privatefuncs.h"
#include "exportfuncs.h"
#include "InputCapture.h"
#include "FontAtlas.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

ImGuiContext* g_pImGuiContext = nullptr;
bool g_bImGuiInitialized = false;
bool g_bImGuiFrameReady = false;
// Set to true because when the DLL loads, GoldSrc/Sven is starting up and the Main Menu (GameUI) is active.
// Also acts as a safety default during hot-reloads (vid_restart) to prevent capturing the mouse
// before callbacks (ActivateGameUI/LoadingStarted) sync the state.
bool s_bGameUIActive = true;

// True once the context has its ini path and font atlas; backends can come and go
// (video mode changes) without re-creating the context, so consumers keep the
// ImGuiContext pointer they obtained from IImGuiExtension::GetImGuiContext().
static bool s_bContextConfigured = false;
static bool s_bLastWantsCapture = false;
static WNDPROC g_pfnOldWndProc = nullptr;
static char s_szIniPath[MAX_PATH] = {};

int GetDeveloperLevel()
{
	if (gEngfuncs.pfnGetCvarFloat)
	{
		return (int)gEngfuncs.pfnGetCvarFloat("developer");
	}
	return 0;
}

// Transition diagnostics are only useful while debugging capture problems; keep
// them behind developer > 1 so a normal session produces no console noise.
// Con_DPrintf is also only valid after LoadEngine.
static void LogVerbose(const char* fmt, ...)
{
	if (GetDeveloperLevel() <= 1 || !gEngfuncs.Con_DPrintf)
		return;

	char msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	gEngfuncs.Con_DPrintf("%s", msg);
}

struct FindWindowData
{
	DWORD processId;
	HWND hWnd;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	FindWindowData* data = (FindWindowData*)lParam;
	DWORD processId = 0;
	GetWindowThreadProcessId(hwnd, &processId);
	if (processId == data->processId)
	{
		char className[256];
		if (GetClassNameA(hwnd, className, sizeof(className)))
		{
			if (strcmp(className, "SDL_app") == 0 || strcmp(className, "ValveHalfLifeWindow") == 0)
			{
				data->hWnd = hwnd;
				return FALSE; // Stop enumerating
			}
		}
	}
	return TRUE;
}

static HWND s_hGameWnd = NULL;

void ResetGameHWND()
{
	s_hGameWnd = NULL;
}

HWND GetGameHWND()
{
	if (s_hGameWnd && !IsWindow(s_hGameWnd))
		s_hGameWnd = NULL;
	if (s_hGameWnd)
		return s_hGameWnd;

	if (g_pfnSys_GetMainWindow && g_pfnSDL_GetWindowWMInfo)
	{
		SDL_Window* pSdlWindow = (SDL_Window*)g_pfnSys_GetMainWindow();
		if (pSdlWindow)
		{
			SDL_SysWMinfo info = {};
			SDL_VERSION(&info.version);
			if (g_pfnSDL_GetWindowWMInfo(pSdlWindow, &info))
			{
				s_hGameWnd = info.info.win.window;
				return s_hGameWnd;
			}
		}
	}

	FindWindowData data = { GetCurrentProcessId(), NULL };
	EnumWindows(EnumWindowsProc, (LPARAM)&data);
	if (data.hWnd)
	{
		s_hGameWnd = data.hWnd;
		return s_hGameWnd;
	}

	HWND h = FindWindowA("SDL_app", NULL);
	if (!h) h = FindWindowA("ValveHalfLifeWindow", NULL);
	if (h)
	{
		s_hGameWnd = h;
		return s_hGameWnd;
	}

	// Deliberately no GetForegroundWindow() fallback: subclassing whatever window
	// happens to be active (another process) would swallow that application's input.
	// Callers retry on the next frame instead.
	return nullptr;
}

LRESULT CALLBACK ImGui_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (g_bImGuiInitialized)
	{
		ImGui::SetCurrentContext(g_pImGuiContext);

		// Query once per message: the dispatcher walks every consumer callback.
		const bool bWantsInputCapture = g_Dispatcher.AnyWantsInputCapture();

		// Pass to ImGui Win32 handler
		if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
		{
			if (bWantsInputCapture)
			{
				return 1; // Supercede
			}
		}

		// Also block game inputs when ImGui wants capture
		if (bWantsInputCapture)
		{
			switch (msg)
			{
			case WM_MOUSEMOVE:
			case WM_LBUTTONDOWN: case WM_LBUTTONUP:
			case WM_RBUTTONDOWN: case WM_RBUTTONUP:
			case WM_MBUTTONDOWN: case WM_MBUTTONUP:
			case WM_XBUTTONDOWN: case WM_XBUTTONUP:
			case WM_MOUSEWHEEL:
			case WM_CHAR:
				return 0; // Block
			}
		}
	}

	if (g_pfnOldWndProc)
	{
		return CallWindowProc(g_pfnOldWndProc, hwnd, msg, wp, lp);
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

bool AnyWantsInputCapture()
{
	return g_Dispatcher.AnyWantsInputCapture();
}

struct ButtonToCmd
{
	unsigned short bit;
	const char* cmd;
};

static const ButtonToCmd s_ButtonToCmds[] = {
	{ IN_FORWARD, "+forward" },
	{ IN_BACK, "+back" },
	{ IN_MOVELEFT, "+moveleft" },
	{ IN_MOVERIGHT, "+moveright" },
	{ IN_JUMP, "+jump" },
	{ IN_DUCK, "+duck" },
	{ IN_USE, "+use" },
	{ IN_ATTACK, "+attack" },
	{ IN_ATTACK2, "+attack2" }
};

static std::vector<std::string> s_PreCaptureCommands;

void UpdateInputCaptureState()
{
	bool anyWantsCapture = g_Dispatcher.AnyWantsInputCapture();
	if (anyWantsCapture != s_bLastWantsCapture)
	{
		s_bLastWantsCapture = anyWantsCapture;
		if (anyWantsCapture)
		{
			// Build snapshot using the last buttons bitmask from CL_CreateMove.
			// This is fully bind-agnostic: we check which action flags the engine had active.
			s_PreCaptureCommands.clear();
			for (const auto& mapping : s_ButtonToCmds)
			{
				if (g_LastButtons & mapping.bit)
				{
					s_PreCaptureCommands.push_back(mapping.cmd);
				}
			}

			if (g_pfnIN_DeactivateMouse)
				g_pfnIN_DeactivateMouse();

			if (gExportfuncs.IN_ClearStates)
				gExportfuncs.IN_ClearStates();

			if (gEngfuncs.pfnClientCmd)
			{
				// Release every action the engine may still hold. The table above is
				// the canonical button -> command mapping, so this cannot drift from it.
				// Commands must end with '\n' so Cbuf_AddText does not concatenate them.
				for (const auto& mapping : s_ButtonToCmds)
				{
					std::string release = "-";
					release += mapping.cmd + 1; // mapping.cmd is "+name"
					release += "\n";
					gEngfuncs.pfnClientCmd(release.data());
				}
			}

			if (g_pfnSDL_SetRelativeMouseMode)
				g_pfnSDL_SetRelativeMouseMode(SDL_FALSE);

			ClipCursor(nullptr);
			int guard = 0;
			while (ShowCursor(TRUE) < 0 && guard++ < 16) {}

			LogVerbose("[IMGUIExtension] capture ON (held actions: %zu)\n", s_PreCaptureCommands.size());
		}
		else
		{
			bool bShouldRestoreMouse = true;
			if (s_bGameUIActive)
				bShouldRestoreMouse = false;
			if (gEngfuncs.pfnGetLevelName)
			{
				const char* level = gEngfuncs.pfnGetLevelName();
				if (!level || !level[0])
					bShouldRestoreMouse = false;
			}

			if (bShouldRestoreMouse)
			{
				if (g_pfnIN_ActivateMouse)
					g_pfnIN_ActivateMouse();

				if (g_pfnSDL_SetRelativeMouseMode)
					g_pfnSDL_SetRelativeMouseMode(SDL_TRUE);

				if (gEngfuncs.pfnClientCmd)
				{
					// Restore only from the snapshot taken at capture ON, not the live set.
					// The live set may contain keys pressed during menu navigation which are
					// already in the correct state once the menu closes.
					// Commands must end with '\n' so Cbuf_AddText executes them separately.
					for (const auto& cmd : s_PreCaptureCommands)
					{
						std::string restore = cmd + "\n";
						gEngfuncs.pfnClientCmd(restore.data());
					}
				}
			}

			s_PreCaptureCommands.clear();

			LogVerbose("[IMGUIExtension] capture OFF (restored=%d)\n", bShouldRestoreMouse ? 1 : 0);
		}
	}
}

static bool ConfigureImGuiContext()
{
	if (!g_pImGuiContext)
	{
		g_pImGuiContext = ImGui::CreateContext();
		if (!g_pImGuiContext)
			return false;
	}
	ImGui::SetCurrentContext(g_pImGuiContext);

	if (s_bContextConfigured)
		return true;

	if (g_pFileSystem || g_pFileSystem_HL25)
	{
		FILESYSTEM_ANY_CREATEDIR("imguiextension", "GAME");
		FILESYSTEM_ANY_CREATEDIR("imguiextension/fonts", "GAME");
		if (!FILESYSTEM_ANY_GETLOCALPATH("imguiextension/imgui.ini", s_szIniPath, sizeof(s_szIniPath)))
		{
			const char* pszGameDir = g_pMetaHookAPI ? g_pMetaHookAPI->GetGameDirectory() : nullptr;
			if (pszGameDir)
				snprintf(s_szIniPath, sizeof(s_szIniPath), "%s/imguiextension/imgui.ini", pszGameDir);
		}
	}

	for (char* p = s_szIniPath; *p; ++p)
		if (*p == '\\') *p = '/';
	if (s_szIniPath[0])
		ImGui::GetIO().IniFilename = s_szIniPath;

	LoadFontsFromConfig();
	s_bContextConfigured = true;
	return true;
}

void ImGui_InitOnce()
{
	if (g_bImGuiInitialized) return;

	HWND hWnd = GetGameHWND();
	if (!hWnd) return;

	if (!ConfigureImGuiContext())
		return;

	if (!ImGui_ImplWin32_Init(hWnd))
		return;

	if (!ImGui_ImplOpenGL2_Init())
	{
		ImGui_ImplWin32_Shutdown();
		return;
	}

	SetLastError(0);
	WNDPROC pfnOldWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)ImGui_WndProc);
	if (!pfnOldWndProc && GetLastError() != 0)
	{
		// Without the subclass the dispatcher would still build frames but no
		// consumer would receive input; roll back and retry on the next frame.
		ImGui_ImplOpenGL2_Shutdown();
		ImGui_ImplWin32_Shutdown();
		return;
	}
	if (pfnOldWndProc == ImGui_WndProc)
	{
		// Guard against recursive self-subclassing if already hooked.
		ImGui_ImplOpenGL2_Shutdown();
		ImGui_ImplWin32_Shutdown();
		return;
	}

	g_pfnOldWndProc = pfnOldWndProc;
	g_bImGuiInitialized = true;
}

void ImGui_Shutdown()
{
	if (g_pfnOldWndProc)
	{
		HWND hWnd = GetGameHWND();
		if (hWnd)
		{
			WNDPROC current = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_WNDPROC);
			if (current == ImGui_WndProc)
			{
				SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)g_pfnOldWndProc);
			}
		}
		g_pfnOldWndProc = nullptr;
	}
	ResetGameHWND();

	if (g_bImGuiInitialized)
	{
		ImGui_ImplOpenGL2_Shutdown();
		ImGui_ImplWin32_Shutdown();
	}
	g_bImGuiInitialized = false;
	g_bImGuiFrameReady = false;

	if (g_pImGuiContext)
	{
		ImGui::DestroyContext(g_pImGuiContext);
		g_pImGuiContext = nullptr;
	}

	s_bContextConfigured = false;
	s_szIniPath[0] = 0;
}

void ImGui_VidReinit()
{
	if (!g_bImGuiInitialized) return;

	// The engine may destroy and re-create both the window and the OpenGL context
	// on a video mode change, so restore the subclass on the known window first,
	// then reset the cached HWND and rebuild backends. The ImGui context itself
	// is kept: consumers cache the pointer returned by GetImGuiContext().
	if (g_pfnOldWndProc)
	{
		HWND hWnd = GetGameHWND();
		if (hWnd)
		{
			WNDPROC current = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_WNDPROC);
			if (current == ImGui_WndProc)
			{
				SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)g_pfnOldWndProc);
			}
		}
		g_pfnOldWndProc = nullptr;
	}
	ResetGameHWND();

	// A frame that was begun but not rendered references GL objects that the old
	// context owned; discard it instead of rendering stale draw data.
	g_bImGuiFrameReady = false;

	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplWin32_Shutdown();
	g_bImGuiInitialized = false;

	// Backends are rebuilt lazily by ImGui_InitOnce() during the next Paint call.
}
