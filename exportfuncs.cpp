#include <metahook.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <Interface/IVGUI2Extension.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "thirdparty/imgui/imgui.h"
#include "thirdparty/imgui/imgui_internal.h"
#include "thirdparty/imgui/backends/imgui_impl_win32.h"
#include "thirdparty/imgui/backends/imgui_impl_opengl2.h"

#include "plugins.h"
#include "privatefuncs.h"
#include "exportfuncs.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// VGUI2Extension pointer
static IVGUI2Extension* g_pVGUI2Extension = nullptr;

// ImGui Context state
static ImGuiContext* g_pImGuiContext = nullptr;
static bool g_bImGuiInitialized = false;
static bool g_bImGuiFrameReady = false;

#include "Dispatcher.h"
CImGuiDispatcher g_Dispatcher;

// Helper to check if game is in foreground window
static WNDPROC g_pfnOldWndProc = nullptr;

LRESULT CALLBACK ImGui_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (g_bImGuiInitialized)
	{
		ImGui::SetCurrentContext(g_pImGuiContext);

		// Pass to ImGui Win32 handler
		if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
		{
			if (g_Dispatcher.AnyWantsInputCapture())
			{
				return 1; // Supercede
			}
		}

		// Also block game inputs when ImGui wants capture
		if (g_Dispatcher.AnyWantsInputCapture())
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
			case WM_KEYDOWN: case WM_KEYUP:
			case WM_SYSKEYDOWN: case WM_SYSKEYUP:
				// Allow console tilde key to pass through to the game
				if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
				{
					if (wp == VK_OEM_3)
					{
						break; // Pass through
					}
				}
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

struct FindWindowData {
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

static HWND GetGameHWND()
{
	static HWND s_hGameWnd = NULL;
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

	return GetForegroundWindow();
}

// Static buffer for imgui.ini path — must outlive the ImGui context
static char s_szIniPath[MAX_PATH] = {};

void ImGui_InitOnce()
{
	if (g_bImGuiInitialized) return;

	HWND hWnd = GetGameHWND();
	if (!hWnd) return;

	g_pImGuiContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(g_pImGuiContext);

	// Route imgui.ini to the standard MetaHook plugin config location.
	// Pattern matches BulletPhysics/BasePhysicManager: use IFileSystem path tokens,
	// not pfnGetGameDirectory() + CWD-relative paths.
	//
	// "GAMEDOWNLOAD" resolves to svencoop_downloads/ by default, but the engine
	// will fall back to "GAME" (svencoop/) when writing if downloads is not writable.
	// We use "GAME" directly since this is persistent plugin config, not user content.
	//
	// FILESYSTEM_ANY_GETLOCALPATH returns an absolute path we can give to ImGui's fopen.
	FILESYSTEM_ANY_CREATEDIR("imguiextension", "GAME");
	if (FILESYSTEM_ANY_GETLOCALPATH("imguiextension/imgui.ini", s_szIniPath, sizeof(s_szIniPath)))
	{
		// COM_FixSlashes to normalize backslashes → forward slashes
		for (char* p = s_szIniPath; *p; ++p)
			if (*p == '\\') *p = '/';
		ImGui::GetIO().IniFilename = s_szIniPath;
	}
	else
	{
		ImGui::GetIO().IniFilename = nullptr; // Disable if FS can't resolve
	}

	// Load fonts BEFORE initializing backends to ensure the font atlas is ready
	ImGuiIO& io = ImGui::GetIO();
	static const ImWchar latin_extended_ranges[] = {
		0x0020, 0x00FF, // Basic Latin + Latin Supplement
		0x0400, 0x052F, // Unicode block [0x0400, 0x052F]
		0,
	};
	static const ImWchar symbol_ranges[] = {
		0x25A0, 0x25FF, // Geometric Shapes
		0x2600, 0x26FF, // Miscellaneous Symbols
		0x2700, 0x27BF, // Dingbats
		0,
	};
	char winDir[MAX_PATH];
	bool baseLoaded = false;
	bool symbolLoaded = false;
	std::string baseFontPath;
	std::string symbolFontPath;
	if (GetWindowsDirectoryA(winDir, sizeof(winDir)))
	{
		baseFontPath = std::string(winDir) + "\\Fonts\\segoeui.ttf";
		symbolFontPath = std::string(winDir) + "\\Fonts\\seguisym.ttf";

		if (io.Fonts->AddFontFromFileTTF(baseFontPath.c_str(), 18.0f, nullptr, latin_extended_ranges) != nullptr)
		{
			baseLoaded = true;
		}

		ImFontConfig config;
		config.MergeMode = true;
		if (io.Fonts->AddFontFromFileTTF(symbolFontPath.c_str(), 18.0f, &config, symbol_ranges) != nullptr)
		{
			symbolLoaded = true;
		}
	}

	if (baseLoaded && symbolLoaded)
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] Fonts loaded successfully (Base: %s, Symbols: %s)\n", baseFontPath.c_str(), symbolFontPath.c_str());
	}
	else
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] Font loading failed or partially failed (Base: %d, Symbols: %d). Falling back to default font.\n", baseLoaded, symbolLoaded);
		io.Fonts->AddFontDefault();
	}

	if (ImGui_ImplWin32_Init(hWnd))
	{
		ImGui_ImplOpenGL2_Init();
		g_bImGuiInitialized = true;

		if (!g_pfnOldWndProc)
		{
			g_pfnOldWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)ImGui_WndProc);
		}
	}
}

void ImGui_Shutdown()
{
	if (!g_bImGuiInitialized) return;

	HWND hWnd = GetGameHWND();
	if (hWnd && g_pfnOldWndProc)
	{
		SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)g_pfnOldWndProc);
		g_pfnOldWndProc = nullptr;
	}

	ImGui::SetCurrentContext(g_pImGuiContext);
	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext(g_pImGuiContext);
	g_pImGuiContext = nullptr;
	g_bImGuiInitialized = false;
}

// Called on HUD_VidInit (video mode change / resolution switch).
// GoldSrc destroys and recreates the OpenGL context on mode changes.
// We must:
//   1. Tear down GL resources (ImGui_ImplOpenGL2_*)
//   2. Tear down the Win32 backend (ImGui_ImplWin32_*)
//   3. Restore the old WndProc — the HWND may be the same, but the
//      engine or SDL2 may have reset GWLP_WNDPROC to its own proc.
//   4. Re-initialize everything on the (possibly new) HWND.
// The ImGui context and registered consumers are preserved.
void ImGui_VidReinit()
{
	if (!g_bImGuiInitialized) return;

	ImGui::SetCurrentContext(g_pImGuiContext);

	// Tear down GL resources — they reference the old GL context
	ImGui_ImplOpenGL2_Shutdown();

	// Restore old WndProc on the current HWND before we query the new one
	HWND hOldWnd = GetGameHWND();
	if (hOldWnd && g_pfnOldWndProc)
	{
		// Only restore if our proc is still installed (engine may have already reset it)
		WNDPROC current = (WNDPROC)GetWindowLongPtr(hOldWnd, GWLP_WNDPROC);
		if (current == ImGui_WndProc)
		{
			SetWindowLongPtr(hOldWnd, GWLP_WNDPROC, (LONG_PTR)g_pfnOldWndProc);
		}
		g_pfnOldWndProc = nullptr;
	}

	// Tear down Win32 backend (references the old HWND internally)
	ImGui_ImplWin32_Shutdown();
	g_bImGuiInitialized = false;

	// Re-initialize — GetGameHWND() will now return the fresh HWND/SDL_Window
	HWND hNewWnd = GetGameHWND();
	if (!hNewWnd) return;

	if (ImGui_ImplWin32_Init(hNewWnd))
	{
		ImGui_ImplOpenGL2_Init();
		g_bImGuiInitialized = true;

		// Reinstall WndProc subclass on the new/same HWND
		g_pfnOldWndProc = (WNDPROC)SetWindowLongPtr(hNewWnd, GWLP_WNDPROC, (LONG_PTR)ImGui_WndProc);
	}
}

bool AnyWantsInputCapture()
{
	return g_Dispatcher.AnyWantsInputCapture();
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

static bool s_bGameUIActive = false;
static bool s_bLastWantsCapture = false;
static std::chrono::steady_clock::time_point s_LastFrameTime = std::chrono::steady_clock::now();
static std::chrono::steady_clock::time_point s_LastCaptureTransition = std::chrono::steady_clock::now();
static int s_iCaptureTransitionCount = 0;

static void UpdateInputCaptureState()
{
	bool anyWantsCapture = g_Dispatcher.AnyWantsInputCapture();
	if (anyWantsCapture != s_bLastWantsCapture)
	{
		auto now = std::chrono::steady_clock::now();
		auto sinceLastMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_LastCaptureTransition).count();
		s_LastCaptureTransition = now;
		s_iCaptureTransitionCount++;

		POINT pt = {};
		GetCursorPos(&pt);

		s_bLastWantsCapture = anyWantsCapture;
		if (anyWantsCapture)
		{
			if (g_pfnIN_DeactivateMouse)
				g_pfnIN_DeactivateMouse();

			ClipCursor(nullptr);
			int guard = 0;
			while (ShowCursor(TRUE) < 0 && guard++ < 16) {}

			// Confirmed root cause: SDL2 relative mouse mode registers raw input
			// with RIDEV_NOLEGACY, which stops the OS from updating the cursor
			// position / generating legacy WM_MOUSEMOVE entirely. Neither
			// ClipCursor/ShowCursor nor IN_DeactivateMouse touch this — only
			// SDL_SetRelativeMouseMode(SDL_FALSE) properly unregisters it.
			if (g_pfnSDL_SetRelativeMouseMode)
				g_pfnSDL_SetRelativeMouseMode(SDL_FALSE);

			gEngfuncs.Con_Printf("[IMGUIExtension] capture ON  (+%lldms since last), cursorPos=(%ld,%ld)\n",
				(long long)sinceLastMs, pt.x, pt.y);
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
			}

			gEngfuncs.Con_Printf("[IMGUIExtension] capture OFF (+%lldms since last), cursorPos=(%ld,%ld), restored=%d\n",
				(long long)sinceLastMs, pt.x, pt.y, bShouldRestoreMouse);
		}
	}
}

// Callback classes bridged from VGUI2Extension
class CImGuiGameConsoleCallbacks : public IVGUI2Extension_GameConsoleCallbacks
{
public:
	int GetAltitude() const override { return 1000; }

	void Activate(VGUI2Extension_CallbackContext* CallbackContext) override { m_bConsoleVisible = true; }
	void Initialize(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void Hide(VGUI2Extension_CallbackContext* CallbackContext) override { m_bConsoleVisible = false; }
	void Clear(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void IsConsoleVisible(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void Printf(IVGUI2Extension_String* str, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void DPrintf(IVGUI2Extension_String* str, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetParent(vgui::VPANEL parent, VGUI2Extension_CallbackContext* CallbackContext) override {}

	bool m_bConsoleVisible = false;
};

static CImGuiGameConsoleCallbacks s_ConsoleCallbacks;

class CImGuiBaseUICallbacks : public IVGUI2Extension_BaseUICallbacks
{
public:
	int GetAltitude() const override { return 1000; } // Render on top of everything

	void Initialize(CreateInterfaceFn* factories, int count) override {}
	void Start(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion) override {}
	void Shutdown() override {}

	void Key_Event(int& down, int& keynum, const char*& pszCurrentBinding, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		if (g_Dispatcher.AnyWantsInputCapture())
		{
			// Check if any callback specifically allows this key to pass through
			if (g_Dispatcher.AnyAllowsKeyPassthrough(keynum))
			{
				return;
			}

			// Block key event in the engine if ImGui wants capture
			CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
		}
	}

	void CallEngineSurfaceAppProc(void*& pevent, void*& userData, VGUI2Extension_CallbackContext* CallbackContext) override {}

	void CallEngineSurfaceWndProc(void*& hwnd, unsigned int& msg, unsigned int& wparam, long& lparam, VGUI2Extension_CallbackContext* CallbackContext) override {}

	void Paint(int& x, int& y, int& right, int& bottom, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		ImGui_InitOnce();
		if (!g_bImGuiInitialized) return;

		ImGui::SetCurrentContext(g_pImGuiContext);

		if (!CallbackContext->IsPost)
		{
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - s_LastFrameTime).count();

			if (elapsed > 500)
			{
				s_LastFrameTime = now;

				HWND hWnd = GetGameHWND();
				RECT rc = { 0 };
				if (hWnd && GetClientRect(hWnd, &rc))
				{
					glwidth = rc.right - rc.left;
					glheight = rc.bottom - rc.top;
				}
				else
				{
					glwidth = right - x;
					glheight = bottom - y;
				}

				if (glwidth > 0 && glheight > 0)
				{
					auto& io = ImGui::GetIO();
					io.DisplaySize = ImVec2((float)glwidth, (float)glheight);

					static int s_lastLogWidth = 0;
					static int s_lastLogHeight = 0;
					if (glwidth != s_lastLogWidth || glheight != s_lastLogHeight)
					{
						s_lastLogWidth = glwidth;
						s_lastLogHeight = glheight;
						gEngfuncs.Con_Printf("[IMGUIExtension] Paint Resolution: %dx%d (VGUI: x=%d y=%d r=%d b=%d, hWnd=%p)\n",
							glwidth, glheight, x, y, right, bottom, hWnd);
					}

					UpdateInputCaptureState();

					ImGui_ImplWin32_NewFrame();
					ImGui_ImplOpenGL2_NewFrame();
					ImGui::NewFrame();

					io.MouseDrawCursor = false; // Always use hardware OS cursor

					g_Dispatcher.Dispatch([&](IImGuiExtensionCallbacks* cb) {
						ImGui::SetCurrentContext(g_pImGuiContext);
						cb->OnImGuiFrame();
					});

					g_bImGuiFrameReady = true;
				}
			}
		}
		else
		{
			if (g_bImGuiFrameReady)
			{
				ImGui::Render();
				ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
				g_bImGuiFrameReady = false;
			}
		}
	}

	void HideGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ActivateGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void HideConsole(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ShowConsole(VGUI2Extension_CallbackContext* CallbackContext) override {}
};

class CImGuiGameUICallbacks : public IVGUI2Extension_GameUICallbacks
{
public:
	int GetAltitude() const override { return 1000; }

	void Initialize(CreateInterfaceFn* factories, int count) override {}
	void PreStart(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion, void* system) override {}
	void Start(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion, void* system) override {}
	void Shutdown() override {}
	void PostShutdown() override {}

	void DisconnectFromServer(VGUI2Extension_CallbackContext* CallbackContext) override
	{
		s_bGameUIActive = true;
		g_Dispatcher.ForceReleaseInput();
		UpdateInputCaptureState();
	}
	void LoadingStarted(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		s_bGameUIActive = true;
		g_Dispatcher.ForceReleaseInput();
		UpdateInputCaptureState();
	}
	void HideGameUI(VGUI2Extension_CallbackContext* CallbackContext) override
	{
		s_bGameUIActive = false;
	}
	void ActivateGameUI(VGUI2Extension_CallbackContext* CallbackContext) override
	{
		s_bGameUIActive = true;
		g_Dispatcher.ForceReleaseInput();
		UpdateInputCaptureState();
	}
	void ActivateDemoUI(VGUI2Extension_CallbackContext* CallbackContext) override {}

	void HasExclusiveInput(VGUI2Extension_CallbackContext* CallbackContext) override
	{
		if (AnyWantsInputCapture())
		{
			static int state = 1;
			CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
			CallbackContext->pPluginReturnValue = &state;
		}
	}

	void IsGameUIActive(VGUI2Extension_CallbackContext* CallbackContext) override {}

	void RunFrame(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ConnectToServer(const char*& game, int& IP, int& port, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void LoadingFinished(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void StartProgressBar(const char*& progressType, int& progressSteps, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ContinueProgressBar(int& progressPoint, float& progressFraction, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void StopProgressBar(bool& bError, const char*& failureReason, const char*& extendedReason, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetProgressBarStatusText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetSecondaryProgressBar(float& progress, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetSecondaryProgressBarText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {}

	const char* GetControlModuleName() const override { return "ImGuiExtension"; }
};

static CImGuiBaseUICallbacks s_BaseUICallbacks;
static CImGuiGameUICallbacks s_GameUICallbacks;

void IMGUI_VGUI2Extension_Init(void)
	{
		auto hVGUI2Extension = g_pMetaHookAPI->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
		if (hVGUI2Extension)
		{
			auto factory = Sys_GetFactory(hVGUI2Extension);
			if (factory)
			{
				g_pVGUI2Extension = (IVGUI2Extension*)factory(VGUI2_EXTENSION_INTERFACE_VERSION, NULL);
			}
		}

		if (g_pVGUI2Extension)
		{
			g_pVGUI2Extension->RegisterBaseUICallbacks(&s_BaseUICallbacks);
			g_pVGUI2Extension->RegisterGameUICallbacks(&s_GameUICallbacks);
			g_pVGUI2Extension->RegisterGameConsoleCallbacks(&s_ConsoleCallbacks);
		}
	}

void IMGUI_VGUI2Extension_Shutdown(void)
{
	if (g_pVGUI2Extension)
	{
		g_pVGUI2Extension->UnregisterBaseUICallbacks(&s_BaseUICallbacks);
		g_pVGUI2Extension->UnregisterGameUICallbacks(&s_GameUICallbacks);
		g_pVGUI2Extension->UnregisterGameConsoleCallbacks(&s_ConsoleCallbacks);
		g_pVGUI2Extension = nullptr;
	}
}

// Hooked Client Export Functions
int HUD_Redraw(float time, int intermission)
{
	return g_pfnHUD_Redraw(time, intermission);
}

void IN_MouseEvent(int mstate)
{
	g_pfnIN_MouseEvent(mstate);
}

void IN_Accumulate(void)
{
	if (g_Dispatcher.AnyWantsInputCapture())
	{
		if (g_pfnSDL_GetRelativeMouseState)
		{
			int rx = 0, ry = 0;
			g_pfnSDL_GetRelativeMouseState(&rx, &ry);
			g_vRawDeltaX = (float)rx;
			g_vRawDeltaY = (float)ry;
		}

		// DIAGNOSTIC: deliberately NOT calling g_pfnIN_Accumulate() here.
		// Hypothesis: the engine's own IN_Accumulate performs SDL_WarpMouseInWindow
		// (or similar) to emulate relative mode every frame, which would explain
		// "cursor moves but click re-centers it" — the warp keeps happening
		// regardless of ClipCursor/ShowCursor state.
		// If this fixes centering, the root cause is confirmed here, not in
		// IN_ActivateMouse/IN_DeactivateMouse.
	}
	else
	{
		g_vRawDeltaX = 0.0f;
		g_vRawDeltaY = 0.0f;
		g_pfnIN_Accumulate();
	}
}

void CL_CreateMove(float frametime, struct usercmd_s* cmd, int active)
{
	g_pfnCL_CreateMove(frametime, cmd, active);
}
