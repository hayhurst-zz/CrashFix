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

	void clear();

	bool setFrames(DEBUG_STACK_FRAME_EX* ptr, std::size_t count);

	void setLogger(CLog* pLog);

private:
	std::stringstream buffer;
	DEBUG_STACK_FRAME_EX* pFrames=nullptr;
	std::size_t frameCount=0;
	CLog* m_pLog = nullptr;
};

class StackFrameDumper 
{
	bool m_initilaized = false;
	IDebugClient4* client;
	IDebugControl5* control;
	IDebugSymbols3* symbols;
	StackFrameDumperCallback callback;

public:
	StackFrameDumper() :client(nullptr), control(nullptr), symbols(nullptr) {}
	StackFrameDumper(const StackFrameDumper&) = delete;

	~StackFrameDumper();

	HRESULT init(CLog* pLog);

	HRESULT set_direcoties(const char* symbol_dirs, const char* image_dirs = nullptr);

	bool open(const char* dumpFileName);

	void close();
	
	std::vector<StackFrameItem> dumpFrame(DWORD dwThreadId, ULONG64 frameOffset = 0, ULONG64 stackOffset = 0, ULONG64 instructionOffset = 0);

	std::map<std::string, bool> dumpModuleSymbolStatus();
};

