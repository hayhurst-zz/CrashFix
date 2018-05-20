#pragma once
#include <string>
#include <sstream>
#include <regex>
#include <vector>
#include <windows.h>
#include <dbgeng.h>

class CLog;

struct StackFrameItem {
	std::string moduleName;
	std::string methodName;
	std::size_t methodOffset;
	std::string srcFileName;
	std::size_t srcLineOffset;
	std::uint64_t childSP;
	std::uint64_t returnAddr;
	std::uint64_t instructionAddr;
	std::size_t frameNumber;

public:
	bool from_dbg_string(const std::string& raw);
};

class StackFrameDumperCallback : public IDebugOutputCallbacks {
public:
	// IUnknown.
	STDMETHOD(QueryInterface)(THIS_ _In_ REFIID InterfaceId, _Out_ PVOID* Interface);
	STDMETHOD_(ULONG, AddRef)(THIS);
	STDMETHOD_(ULONG, Release)(THIS);

	// IDebugOutputCallbacks
	STDMETHOD(Output)(THIS_ _In_ ULONG Mask, _In_ PCSTR Text);

	std::vector<StackFrameItem> build(DWORD dwThreadId);

	void clear() {
		buffer.clear();
		buffer.str(std::string()); // TODO
	}

	bool setFrames(DEBUG_STACK_FRAME_EX* ptr, std::size_t count) {
		pFrames = ptr;
		frameCount = count;
		return true;
	}

	void setLogger(CLog* pLog)
	{
		m_pLog = pLog;
	}

private:
	std::stringstream buffer;
	DEBUG_STACK_FRAME_EX* pFrames=nullptr;
	std::size_t frameCount=0;
	CLog* m_pLog = nullptr;
};

class StackFrameDumper {
	IDebugClient4* client;
	IDebugControl5* control;
	IDebugSymbols3* symbols;
	StackFrameDumperCallback callback;

public:
	StackFrameDumper() :client(nullptr), control(nullptr), symbols(nullptr) {}
	StackFrameDumper(const StackFrameDumper&) = delete;

	~StackFrameDumper() 
	{
		if (client) client->Release();
		if (control) control->Release();
		if (symbols) symbols->Release();
	}

	HRESULT init(CLog* pLog);

	HRESULT set_direcoties(const char* symbol_dirs, const char* image_dirs = nullptr) 
	{
		HRESULT status = S_OK;
		if (symbol_dirs) {
			status = symbols->SetSymbolPath(symbol_dirs);
			//auto symbolStoreEnvName = "_NT_SYMBOL_PATH";
			//auto symbolStoreSize = GetEnvironmentVariableA(symbolStoreEnvName, nullptr, 0);
			//if (symbolStoreSize != 0) {
			//	auto store = new char[symbolStoreSize]();
			//	if (GetEnvironmentVariableA(symbolStoreEnvName, store, symbolStoreSize) != 0) {
			//		status = symbols->AppendSymbolPath(store);
			//	}
			//	delete[] store;
			//}
			if (status != S_OK) return status;
			status = symbols->SetImagePath(symbol_dirs);
			if (status != S_OK) return status;
		}

		if (image_dirs) {
			status = symbols->SetImagePath(image_dirs);
			if (status != S_OK) return status;
		}

		// status = control->WaitForEvent(DEBUG_WAIT_DEFAULT, INFINITE);
		
		return status;
	}

	std::vector<StackFrameItem> dumpFrame(DWORD dwThreadId,
		const char* dumpFileName, ULONG64 frameOffset = 0,
		ULONG64 stackOffset = 0, ULONG64 instructionOffset = 0
	);
};

