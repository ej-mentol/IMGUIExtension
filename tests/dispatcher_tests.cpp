#include <iostream>
#include <vector>
#include <string>
#include <cassert>

#include <metahook.h>

#include "../src/Dispatcher.h"

// SafeInvoke reports consumer crashes through the host console. The test binary
// links without an engine, so provide the storage the header references.
cl_enginefunc_t gEngfuncs = {};

std::vector<std::string> g_ExecutionLog;

class MockCallbacks : public IImGuiExtensionCallbacks
{
public:
	MockCallbacks(const char* name, int altitude, bool wantsInput = false)
		: m_Name(name), m_Altitude(altitude), m_WantsInput(wantsInput)
		, m_pFrameDispatcher(nullptr), m_pFrameUnregister(nullptr)
		, m_pShutdownDispatcher(nullptr), m_pShutdownUnregister(nullptr)
		, m_bInitCalled(false), m_bStartCalled(false), m_bShutdownCalled(false)
		, m_iShutdownCalls(0), m_iPostUnregisterWrite(0)
	{}

	int GetAltitude() const override { return m_Altitude; }
	const char* GetModuleName() const override { return m_Name.c_str(); }

	void Initialize() override { m_bInitCalled = true; }
	void Start(cl_enginefunc_t* engineFuncs, int interfaceVersion) override { m_bStartCalled = true; }

	void Shutdown() override
	{
		m_bShutdownCalled = true;
		++m_iShutdownCalls;

		// A consumer may unregister other callbacks (or itself) from Shutdown();
		// the dispatcher must tolerate that without recursing or double-destroying.
		if (m_pShutdownDispatcher && m_pShutdownUnregister)
			m_pShutdownDispatcher->Unregister(m_pShutdownUnregister);
	}

	void OnImGuiFrame() override
	{
		assert(!m_bShutdownCalled);
		g_ExecutionLog.push_back(m_Name);

		if (m_pFrameDispatcher && m_pFrameUnregister)
		{
			m_pFrameDispatcher->Unregister(m_pFrameUnregister);
			if (m_pFrameUnregister == this)
			{
				// Contract: Unregister() ran Shutdown() synchronously, so the object
				// is already shut down yet still fully usable for its own cleanup.
				assert(m_bShutdownCalled);
				m_iPostUnregisterWrite = 42;
			}
		}
	}

	bool WantsInputCapture() const override { return m_WantsInput; }

	void SetFrameUnregister(CImGuiDispatcher* dispatcher, IImGuiExtensionCallbacks* target)
	{
		m_pFrameDispatcher = dispatcher;
		m_pFrameUnregister = target;
	}

	void SetShutdownUnregister(CImGuiDispatcher* dispatcher, IImGuiExtensionCallbacks* target)
	{
		m_pShutdownDispatcher = dispatcher;
		m_pShutdownUnregister = target;
	}

	bool m_bInitCalled;
	bool m_bStartCalled;
	bool m_bShutdownCalled;
	int m_iShutdownCalls;
	int m_iPostUnregisterWrite;

private:
	std::string m_Name;
	int m_Altitude;
	bool m_WantsInput;
	CImGuiDispatcher* m_pFrameDispatcher;
	IImGuiExtensionCallbacks* m_pFrameUnregister;
	CImGuiDispatcher* m_pShutdownDispatcher;
	IImGuiExtensionCallbacks* m_pShutdownUnregister;
};

void TestSorting()
{
	std::cout << "[Test] Running TestSorting...\n";
	g_ExecutionLog.clear();

	CImGuiDispatcher dispatcher;
	MockCallbacks cb1("Low", 10);
	MockCallbacks cb2("High", 100);
	MockCallbacks cb3("Medium", 50);

	dispatcher.Register(&cb1);
	dispatcher.Register(&cb2);
	dispatcher.Register(&cb3);

	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});

	assert(g_ExecutionLog.size() == 3);
	assert(g_ExecutionLog[0] == "High");
	assert(g_ExecutionLog[1] == "Medium");
	assert(g_ExecutionLog[2] == "Low");

	// Equal altitudes must keep their relative insertion order (contract requirement)
	CImGuiDispatcher dispatcherStable;
	MockCallbacks cbEq1("Eq1", 50);
	MockCallbacks cbEq2("Eq2", 50);
	MockCallbacks cbEq3("Eq3", 50);

	dispatcherStable.Register(&cbEq1);
	dispatcherStable.Register(&cbEq2);
	dispatcherStable.Register(&cbEq3);

	g_ExecutionLog.clear();
	dispatcherStable.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});

	assert(g_ExecutionLog.size() == 3);
	assert(g_ExecutionLog[0] == "Eq1");
	assert(g_ExecutionLog[1] == "Eq2");
	assert(g_ExecutionLog[2] == "Eq3");

	std::cout << "[Test] TestSorting passed.\n";
}

void TestWantsInputCapture()
{
	std::cout << "[Test] Running TestWantsInputCapture...\n";
	CImGuiDispatcher dispatcher;

	MockCallbacks cb1("NoInput1", 10, false);
	MockCallbacks cb2("WithInput", 50, true);
	MockCallbacks cb3("NoInput2", 100, false);

	dispatcher.Register(&cb1);
	assert(dispatcher.AnyWantsInputCapture() == false);

	dispatcher.Register(&cb2);
	assert(dispatcher.AnyWantsInputCapture() == true);

	dispatcher.Register(&cb3);
	assert(dispatcher.AnyWantsInputCapture() == true);

	dispatcher.Unregister(&cb2);
	assert(dispatcher.AnyWantsInputCapture() == false);
	std::cout << "[Test] TestWantsInputCapture passed.\n";
}

void TestSafeUnregisterDuringIteration()
{
	std::cout << "[Test] Running TestSafeUnregisterDuringIteration...\n";
	g_ExecutionLog.clear();

	CImGuiDispatcher dispatcher;
	MockCallbacks cb1("High", 100);
	MockCallbacks cb2("Medium", 50);
	MockCallbacks cb3("Low", 10);

	dispatcher.Register(&cb1);
	dispatcher.Register(&cb2);
	dispatcher.Register(&cb3);

	cb1.SetFrameUnregister(&dispatcher, &cb2);

	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});

	assert(g_ExecutionLog.size() == 2);
	assert(g_ExecutionLog[0] == "High");
	assert(g_ExecutionLog[1] == "Low");

	const auto callbacks = dispatcher.GetCallbacks();
	assert(callbacks.size() == 2);
	assert(callbacks[0] == &cb1);
	assert(callbacks[1] == &cb3);

	// Unregister() runs Shutdown() synchronously.
	assert(cb2.m_bShutdownCalled);
	assert(cb2.m_iShutdownCalls == 1);

	std::cout << "[Test] TestSafeUnregisterDuringIteration passed.\n";
}

void TestSelfUnregisterDuringIteration()
{
	std::cout << "[Test] Running TestSelfUnregisterDuringIteration...\n";
	g_ExecutionLog.clear();

	CImGuiDispatcher dispatcher;
	MockCallbacks cb1("High", 100);
	MockCallbacks cb2("Medium", 50);

	dispatcher.Register(&cb1);
	dispatcher.Register(&cb2);

	cb1.SetFrameUnregister(&dispatcher, &cb1);

	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});

	assert(g_ExecutionLog.size() == 2);
	assert(g_ExecutionLog[0] == "High");
	assert(g_ExecutionLog[1] == "Medium");

	const auto callbacks = dispatcher.GetCallbacks();
	assert(callbacks.size() == 1);
	assert(callbacks[0] == &cb2);

	// Shutdown ran before Unregister() returned, and the object stayed usable.
	assert(cb1.m_bShutdownCalled);
	assert(cb1.m_iShutdownCalls == 1);
	assert(cb1.m_iPostUnregisterWrite == 42);

	std::cout << "[Test] TestSelfUnregisterDuringIteration passed.\n";
}

void TestShutdownReentrancy()
{
	std::cout << "[Test] Running TestShutdownReentrancy...\n";
	CImGuiDispatcher dispatcher;

	MockCallbacks cb1("First", 100);
	MockCallbacks cb2("Second", 50);

	dispatcher.Register(&cb1);
	dispatcher.Register(&cb2);

	// Unregister another callback from inside Shutdown().
	cb1.SetShutdownUnregister(&dispatcher, &cb2);

	dispatcher.Shutdown();

	assert(cb1.m_iShutdownCalls == 1);
	assert(cb2.m_iShutdownCalls == 1);
	assert(dispatcher.GetCallbacks().empty());

	std::cout << "[Test] TestShutdownReentrancy passed.\n";
}

// A callback that registers a sibling while the dispatch loop is running.
class RegisteringCallbacks : public MockCallbacks
{
public:
	RegisteringCallbacks(const char* name, int altitude, CImGuiDispatcher* dispatcher, MockCallbacks* target)
		: MockCallbacks(name, altitude), m_Dispatcher(dispatcher), m_Target(target) {}

	void OnImGuiFrame() override
	{
		g_ExecutionLog.push_back(GetModuleName());
		m_Dispatcher->Register(m_Target);
	}

private:
	CImGuiDispatcher* m_Dispatcher;
	MockCallbacks* m_Target;
};

void TestRegisterDuringDispatch()
{
	std::cout << "[Test] Running TestRegisterDuringDispatch...\n";
	CImGuiDispatcher dispatcher;
	dispatcher.Initialize();
	dispatcher.Start(nullptr, 777);

	MockCallbacks cbLate("Late", 50);
	RegisteringCallbacks cbRegistrar("Registrar", 100, &dispatcher, &cbLate);
	dispatcher.Register(&cbRegistrar);
	assert(cbRegistrar.m_bInitCalled && cbRegistrar.m_bStartCalled);

	g_ExecutionLog.clear();
	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});

	// The addition is queued, so it must not run inside the current loop.
	assert(g_ExecutionLog.size() == 1);
	assert(g_ExecutionLog[0] == "Registrar");

	// Queued additions are applied after the loop: initialized, started and visible.
	const auto callbacks = dispatcher.GetCallbacks();
	assert(callbacks.size() == 2);
	assert(cbLate.m_bInitCalled);
	assert(cbLate.m_bStartCalled);

	g_ExecutionLog.clear();
	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});
	assert(g_ExecutionLog.size() == 2);
	assert(g_ExecutionLog[0] == "Registrar");
	assert(g_ExecutionLog[1] == "Late");

	std::cout << "[Test] TestRegisterDuringDispatch passed.\n";
}

void TestLifecycleCallbacks()
{
	std::cout << "[Test] Running TestLifecycleCallbacks...\n";
	CImGuiDispatcher dispatcher;
	MockCallbacks cb1("High", 100);
	MockCallbacks cb2("Low", 10);

	dispatcher.Register(&cb1);
	dispatcher.Register(&cb2);

	assert(!cb1.m_bInitCalled && !cb2.m_bInitCalled);
	dispatcher.Initialize();
	assert(cb1.m_bInitCalled && cb2.m_bInitCalled);

	assert(!cb1.m_bStartCalled && !cb2.m_bStartCalled);
	dispatcher.Start(nullptr, 1234);
	assert(cb1.m_bStartCalled && cb2.m_bStartCalled);

	assert(!cb1.m_bShutdownCalled && !cb2.m_bShutdownCalled);
	dispatcher.Shutdown();
	assert(cb1.m_bShutdownCalled && cb2.m_bShutdownCalled);

	std::cout << "[Test] TestLifecycleCallbacks passed.\n";
}

void TestLateRegistrationLifecycle()
{
	std::cout << "[Test] Running TestLateRegistrationLifecycle...\n";
	CImGuiDispatcher dispatcher;

	dispatcher.Initialize();
	dispatcher.Start(nullptr, 4321);

	MockCallbacks cb("LatePlayer", 50);
	assert(!cb.m_bInitCalled && !cb.m_bStartCalled);

	dispatcher.Register(&cb);
	// Late registration should instantly trigger Initialize and Start
	assert(cb.m_bInitCalled);
	assert(cb.m_bStartCalled);
	assert(!cb.m_bShutdownCalled);

	dispatcher.Unregister(&cb);
	assert(cb.m_bShutdownCalled);

	std::cout << "[Test] TestLateRegistrationLifecycle passed.\n";
}

#ifdef _WIN32
class CrashingCallbacks : public MockCallbacks
{
public:
	CrashingCallbacks(const char* name, int altitude, bool crashOnFrame = true)
		: MockCallbacks(name, altitude), m_bCrashOnFrame(crashOnFrame), m_iFrameCalls(0) {}

	void OnImGuiFrame() override
	{
		++m_iFrameCalls;
		if (m_bCrashOnFrame)
		{
			RaiseException(0xE0000001, 0, 0, nullptr);
		}
		g_ExecutionLog.push_back(GetModuleName());
	}

	bool m_bCrashOnFrame;
	int m_iFrameCalls;
};

void TestFaultedCallback()
{
	std::cout << "[Test] Running TestFaultedCallback...\n";
	CImGuiDispatcher dispatcher;
	CrashingCallbacks cbCrash("Crasher", 100, true);
	MockCallbacks cbHealthy("Healthy", 50, false);

	dispatcher.Register(&cbCrash);
	dispatcher.Register(&cbHealthy);

	g_ExecutionLog.clear();
	// First dispatch: cbCrash will crash and be marked faulted; cbHealthy will run
	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});

	assert(cbCrash.m_iFrameCalls == 1);
	assert(g_ExecutionLog.size() == 1);
	assert(g_ExecutionLog[0] == "Healthy");

	// Second dispatch: cbCrash is faulted, must be completely skipped
	g_ExecutionLog.clear();
	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});

	// Frame calls on cbCrash must still be 1 (never called again)
	assert(cbCrash.m_iFrameCalls == 1);
	assert(g_ExecutionLog.size() == 1);
	assert(g_ExecutionLog[0] == "Healthy");

	// Shutdown must still invoke Shutdown on faulted callbacks
	dispatcher.Shutdown();
	assert(cbCrash.m_bShutdownCalled);
	assert(cbHealthy.m_bShutdownCalled);

	std::cout << "[Test] TestFaultedCallback passed.\n";
}

class CrashingShutdownCallbacks : public MockCallbacks
{
public:
	CrashingShutdownCallbacks(const char* name, int altitude)
		: MockCallbacks(name, altitude) {}

	void Shutdown() override
	{
		MockCallbacks::Shutdown();
		RaiseException(0xE0000002, 0, 0, nullptr);
	}
};

void TestCrashingShutdown()
{
	std::cout << "[Test] Running TestCrashingShutdown...\n";
	CImGuiDispatcher dispatcher;
	CrashingShutdownCallbacks cbCrash("CrashOnShutdown", 100);
	MockCallbacks cbHealthy("Healthy", 50);

	dispatcher.Register(&cbCrash);
	dispatcher.Register(&cbHealthy);

	// Shutdown should catch the exception and safely finish shutting down all callbacks
	dispatcher.Shutdown();
	assert(cbCrash.m_bShutdownCalled);
	assert(cbHealthy.m_bShutdownCalled);

	// Also verify Unregister catches exception during Shutdown() without crashing
	dispatcher.Register(&cbCrash);
	dispatcher.Unregister(&cbCrash);

	std::cout << "[Test] TestCrashingShutdown passed.\n";
}

class CrashingStyleVarCallbacks : public MockCallbacks
{
public:
	CrashingStyleVarCallbacks(const char* name, int altitude)
		: MockCallbacks(name, altitude), m_iFrameCalls(0) {}

	void OnImGuiFrame() override
	{
		++m_iFrameCalls;
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
		RaiseException(0xE0000003, 0, 0, nullptr);
	}

	int m_iFrameCalls;
};

void TestImGuiStackRecoveryOnCrash()
{
	std::cout << "[Test] Running TestImGuiStackRecoveryOnCrash...\n";
	ImGuiContext* ctx = ImGui::CreateContext();
	ImGui::SetCurrentContext(ctx);
	ImGuiIO& io = ctx->IO;
	io.DisplaySize = ImVec2(800.0f, 600.0f);
	// Headless test without a real renderer backend: inform ImGui that textures are supported
	// so NewFrame builds the default font atlas without asserting.
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

	// Start a frame so WithinFrameScope and CurrentWindow are active
	ImGui::NewFrame();
	const int initialStyleVarStackSize = ctx->StyleVarStack.Size;

	CImGuiDispatcher dispatcher;
	CrashingStyleVarCallbacks cbCrash("StyleCrasher", 100);
	MockCallbacks cbHealthy("Healthy", 50);

	dispatcher.Register(&cbCrash);
	dispatcher.Register(&cbHealthy);

	g_ExecutionLog.clear();
	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});

	// Crash was caught; ImGui state recovery must have restored StyleVarStack
	assert(cbCrash.m_iFrameCalls == 1);
	assert(ctx->StyleVarStack.Size == initialStyleVarStackSize);
	assert(g_ExecutionLog.size() == 1);
	assert(g_ExecutionLog[0] == "Healthy");

	// Second dispatch: faulted consumer is skipped, StyleVarStack remains intact
	g_ExecutionLog.clear();
	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});
	assert(cbCrash.m_iFrameCalls == 1);
	assert(ctx->StyleVarStack.Size == initialStyleVarStackSize);
	assert(g_ExecutionLog.size() == 1);
	assert(g_ExecutionLog[0] == "Healthy");

	ImGui::EndFrame();
	ImGui::DestroyContext(ctx);
	std::cout << "[Test] TestImGuiStackRecoveryOnCrash passed.\n";
}

class CrashingCaptureCallbacks : public MockCallbacks
{
public:
	CrashingCaptureCallbacks(const char* name, int altitude)
		: MockCallbacks(name, altitude), m_iCaptureCalls(0), m_bShouldCrash(true) {}

	bool WantsInputCapture() const override
	{
		const_cast<CrashingCaptureCallbacks*>(this)->m_iCaptureCalls++;
		if (m_bShouldCrash)
		{
			RaiseException(0xE0000004, 0, 0, nullptr);
		}
		return true;
	}

	int m_iCaptureCalls;
	bool m_bShouldCrash;
};

void TestWantsInputCaptureFaultAndReRegister()
{
	std::cout << "[Test] Running TestWantsInputCaptureFaultAndReRegister...\n";
	CImGuiDispatcher dispatcher;
	CrashingCaptureCallbacks cbCrash("CaptureCrasher", 100);
	MockCallbacks cbHealthy("Healthy", 50, false);

	dispatcher.Register(&cbCrash);
	dispatcher.Register(&cbHealthy);

	// First query: cbCrash crashes, AnyWantsInputCapture must catch it, return false, and mark cbCrash faulted
	bool wants = dispatcher.AnyWantsInputCapture();
	assert(!wants);
	assert(cbCrash.m_iCaptureCalls == 1);

	// Second query: cbCrash is faulted and must be skipped without calling WantsInputCapture again
	wants = dispatcher.AnyWantsInputCapture();
	assert(!wants);
	assert(cbCrash.m_iCaptureCalls == 1);

	// Re-registering the same mock clears faulted state
	cbCrash.m_bShouldCrash = false;
	dispatcher.Register(&cbCrash);

	// Third query: cbCrash is no longer faulted and returns true
	wants = dispatcher.AnyWantsInputCapture();
	assert(wants);
	assert(cbCrash.m_iCaptureCalls == 2);

	std::cout << "[Test] TestWantsInputCaptureFaultAndReRegister passed.\n";
}
#endif

int main()
{
	std::cout << "=== Running Dispatcher Unit Tests ===\n";
	TestSorting();
	TestWantsInputCapture();
	TestSafeUnregisterDuringIteration();
	TestSelfUnregisterDuringIteration();
	TestShutdownReentrancy();
	TestRegisterDuringDispatch();
	TestLifecycleCallbacks();
	TestLateRegistrationLifecycle();
#ifdef _WIN32
	TestFaultedCallback();
	TestCrashingShutdown();
	TestImGuiStackRecoveryOnCrash();
	TestWantsInputCaptureFaultAndReRegister();
#endif
	std::cout << "=== All Tests Passed Successfully ===\n";
	return 0;
}
