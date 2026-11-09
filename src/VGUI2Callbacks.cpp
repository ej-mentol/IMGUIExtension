#include <metahook.h>
#include <chrono>
#include <string>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_opengl2.h>

#include <Interface/IVGUI2Extension.h>

#include "plugins.h"
#include "exportfuncs.h"
#include "InputCapture.h"
#include "VGUI2Callbacks.h"

// The engine's console toggle command. Match by binding name because the key
// itself is user-rebindable and no key number is guaranteed here.
static constexpr const char kConsoleCommand[] = "toggleconsole";

// Callback classes bridged from VGUI2Extension
class CImGuiGameConsoleCallbacks : public IVGUI2Extension_GameConsoleCallbacks
{
public:
	int GetAltitude() const override { return 1000; }

	void Activate(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void Initialize(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void Hide(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void Clear(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void IsConsoleVisible(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void Printf(IVGUI2Extension_String* str, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void DPrintf(IVGUI2Extension_String* str, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetParent(vgui::VPANEL parent, VGUI2Extension_CallbackContext* CallbackContext) override {}
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
		if (!g_Dispatcher.AnyWantsInputCapture())
			return;

		// GoldSrc may pass a null or dangling binding pointer on keyup. Never
		// dereference it on release; the event must pass so the engine can clear
		// held commands.
		if (down == 0)
			return;

		// An unbound key has no command to suppress.
		if (!pszCurrentBinding)
			return;

		// Keep a stable copy for this event: the engine-owned binding string may be
		// invalidated by anything that rebinds while the consumer callbacks run, and
		// consumers only keep the pointer for the duration of the call.
		const std::string binding = pszCurrentBinding;

		// Console access must survive any capture, whichever key the user bound.
		if (binding.find(kConsoleCommand) != std::string::npos)
			return;

		// keynum is forwarded verbatim (engine key number, not a Windows VK code).
		if (g_Dispatcher.AnyAllowsKeyPassthrough(keynum, binding.c_str()))
			return;

		// Block the key in the engine while ImGui owns the keyboard.
		CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
	}

	void CallEngineSurfaceAppProc(void*& pevent, void*& userData, VGUI2Extension_CallbackContext* CallbackContext) override {}

	void CallEngineSurfaceWndProc(void*& hwnd, unsigned int& msg, unsigned int& wparam, long& lparam, VGUI2Extension_CallbackContext* CallbackContext) override {}

	void Paint(int& x, int& y, int& right, int& bottom, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		// Zero-initialized to epoch: first elapsed will be enormous, guaranteeing
		// the first Paint call always passes the 500μs throttle.
		static std::chrono::steady_clock::time_point s_LastFrameTime = {};

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

				// The Win32 backend owns io.DisplaySize (it reads the client rect in
				// NewFrame); these values are only used for the diagnostic log line.
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
					if (GetDeveloperLevel() > 1)
					{
						static int s_lastLogWidth = 0;
						static int s_lastLogHeight = 0;
						if (glwidth != s_lastLogWidth || glheight != s_lastLogHeight)
						{
							s_lastLogWidth = glwidth;
							s_lastLogHeight = glheight;
							gEngfuncs.Con_DPrintf("[IMGUIExtension] display %dx%d\n", glwidth, glheight);
						}
					}

					UpdateInputCaptureState();

					// A previous frame was begun but never rendered (the post-Paint call
					// was skipped). End it first: NewFrame asserts otherwise.
					if (g_bImGuiFrameReady)
					{
						ImGui::EndFrame();
						g_bImGuiFrameReady = false;
					}

					ImGui_ImplWin32_NewFrame();
					ImGui_ImplOpenGL2_NewFrame();
					ImGui::NewFrame();

					ImGui::GetIO().MouseDrawCursor = false; // Always use hardware OS cursor

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
				ImGui::SetCurrentContext(g_pImGuiContext);
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

static CImGuiBaseUICallbacks s_BaseUICallbacks;

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
	void LoadingFinished(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		s_bGameUIActive = false;
	}
	void StartProgressBar(const char*& progressType, int& progressSteps, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ContinueProgressBar(int& progressPoint, float& progressFraction, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void StopProgressBar(bool& bError, const char*& failureReason, const char*& extendedReason, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetProgressBarStatusText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetSecondaryProgressBar(float& progress, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetSecondaryProgressBarText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {}

	const char* GetControlModuleName() const override { return "ImGuiExtension"; }
};

static CImGuiGameUICallbacks s_GameUICallbacks;

void IMGUI_VGUI2Extension_Init(void)
{
	if (g_pVGUI2Extension)
		return;

	HINTERFACEMODULE hVGUI2Extension = nullptr;
	if (g_pMetaHookAPI)
		hVGUI2Extension = g_pMetaHookAPI->GetPluginModuleHandleByBaseFileName("VGUI2Extension");

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
	else
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] Failed to get VGUI2Extension interface!\n");
	}
}

void IMGUI_VGUI2Extension_Shutdown(void)
{
	if (!g_pVGUI2Extension)
		return;

	g_pVGUI2Extension->UnregisterBaseUICallbacks(&s_BaseUICallbacks);
	g_pVGUI2Extension->UnregisterGameUICallbacks(&s_GameUICallbacks);
	g_pVGUI2Extension->UnregisterGameConsoleCallbacks(&s_ConsoleCallbacks);
	// Both Shutdown and ExitGame call this; nulling the pointer makes it idempotent
	// and stops later callbacks from using a torn-down VGUI2Extension.
	g_pVGUI2Extension = nullptr;
}
