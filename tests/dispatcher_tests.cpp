#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include "../Dispatcher.h"

std::vector<std::string> g_ExecutionLog;

class MockCallbacks : public IImGuiExtensionCallbacks
{
public:
	MockCallbacks(const char* name, int altitude, bool wantsInput = false)
		: m_Name(name), m_Altitude(altitude), m_WantsInput(wantsInput), m_Dispatcher(nullptr), m_UnregisterTarget(nullptr)
		, m_bInitCalled(false), m_bStartCalled(false), m_bShutdownCalled(false), m_iPostUnregisterWrite(0)
	{}

	int GetAltitude() const override { return m_Altitude; }
	const char* GetModuleName() const override { return m_Name.c_str(); }

	void Initialize() override { m_bInitCalled = true; }
	void Start(cl_enginefunc_t* engineFuncs, int interfaceVersion) override { m_bStartCalled = true; }
	void Shutdown() override { m_bShutdownCalled = true; }

	void OnImGuiFrame() override
	{
		assert(!m_bShutdownCalled);
		g_ExecutionLog.push_back(m_Name);
		if (m_Dispatcher && m_UnregisterTarget)
		{
			m_Dispatcher->Unregister(m_UnregisterTarget);
			if (m_UnregisterTarget == this)
			{
				// Verify that Shutdown was deferred and NOT called synchronously
				assert(!m_bShutdownCalled);

				// Verify that we can safely write to a member variable without UAF
				m_iPostUnregisterWrite = 42;
			}
		}
	}

	bool WantsInputCapture() const override { return m_WantsInput; }

	void SetUnregisterBehavior(CImGuiDispatcher* dispatcher, IImGuiExtensionCallbacks* target)
	{
		m_Dispatcher = dispatcher;
		m_UnregisterTarget = target;
	}

	bool m_bInitCalled;
	bool m_bStartCalled;
	bool m_bShutdownCalled;
	int m_iPostUnregisterWrite;

private:
	std::string m_Name;
	int m_Altitude;
	bool m_WantsInput;
	CImGuiDispatcher* m_Dispatcher;
	IImGuiExtensionCallbacks* m_UnregisterTarget;
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

	cb1.SetUnregisterBehavior(&dispatcher, &cb2);

	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});

	assert(g_ExecutionLog.size() == 2);
	assert(g_ExecutionLog[0] == "High");
	assert(g_ExecutionLog[1] == "Low");

	const auto& callbacks = dispatcher.GetCallbacks();
	assert(callbacks.size() == 2);
	assert(callbacks[0] == &cb1);
	assert(callbacks[1] == &cb3);

	// Since cb2 was unregistered mid-iteration, its Shutdown should be called after iteration
	assert(cb2.m_bShutdownCalled);

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

	cb1.SetUnregisterBehavior(&dispatcher, &cb1);

	dispatcher.Dispatch([](IImGuiExtensionCallbacks* cb) {
		cb->OnImGuiFrame();
	});

	assert(g_ExecutionLog.size() == 2);
	assert(g_ExecutionLog[0] == "High");
	assert(g_ExecutionLog[1] == "Medium");

	const auto& callbacks = dispatcher.GetCallbacks();
	assert(callbacks.size() == 1);
	assert(callbacks[0] == &cb2);

	// Verify deferred shutdown called AFTER iteration finished
	assert(cb1.m_bShutdownCalled);
	assert(cb1.m_iPostUnregisterWrite == 42);

	std::cout << "[Test] TestSelfUnregisterDuringIteration passed.\n";
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
	TestLifecycleCallbacks();
	TestLateRegistrationLifecycle();
	std::cout << "=== All Tests Passed Successfully ===\n";
	return 0;
}
