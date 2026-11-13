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
	std::cout << "=== All Tests Passed Successfully ===\n";
	return 0;
}
