#pragma once
#include <string>
#include <sstream>
#include <regex>
#include <vector>
#include <windows.h>
#include <dbgeng.h>

struct StackFrameItem {
	std::string moduleName;
	std::string methodName;
	std::size_t methodOffset;
	std::string srcFileName;
	std::size_t srcLineOffset;

public:
	bool from_dbg_string(const std::string& raw) {
		static const std::regex matcher(R"(([^!]+)!([^\+]+)\+0x([0-9a-f]+) \[([^\]]+) @ ([0-9]+)\])");
		std::smatch matchResult;
		if (std::regex_match(raw, matchResult, matcher)) {
			if (matchResult.size() == 6) {
				moduleName = matchResult[1].str();
				methodName = matchResult[2].str();
				methodOffset = static_cast<size_t>(std::stoull(matchResult[3].str(), nullptr, 16));
				srcFileName = matchResult[4].str();
				srcLineOffset = static_cast<size_t>(std::stoull(matchResult[5].str(), nullptr, 10));
				return true;
			}
		}
		return false;
	}
};

class StackFrameDumperCallback : public IDebugOutputCallbacks {
public:
	// IUnknown.
	STDMETHOD(QueryInterface)(THIS_ _In_ REFIID InterfaceId, _Out_ PVOID* Interface);
	STDMETHOD_(ULONG, AddRef)(THIS);
	STDMETHOD_(ULONG, Release)(THIS);

	// IDebugOutputCallbacks
	STDMETHOD(Output)(THIS_ _In_ ULONG Mask, _In_ PCSTR Text);

	std::vector<StackFrameItem> build() {
		std::string line;
		StackFrameItem item;
		std::vector<StackFrameItem> ret;
		while (std::getline(buffer,line)) {
			if (item.from_dbg_string(line)) ret.push_back(item);
			// TODO: else?
		}
		clear();
		return ret;
	}

	void clear() {
		buffer.clear();
		buffer.str(std::string()); // TODO
	}

private:
	std::stringstream buffer;
};

class StackFrameDumper {
	IDebugClient4* client;
	IDebugControl4* control;
	IDebugSymbols3* symbols;
	StackFrameDumperCallback callback;

public:
	StackFrameDumper() :client(nullptr), control(nullptr), symbols(nullptr) {}
	StackFrameDumper(const StackFrameDumper&) = delete;
	~StackFrameDumper() {
		if (client) client->Release();
		if (control) control->Release();
		if (symbols) symbols->Release();
	}
	HRESULT init() {
		HRESULT status = S_OK;
		status = DebugCreate(__uuidof(IDebugClient4), (void**)&client);
		if (status != S_OK) return status;
		status = client->QueryInterface(__uuidof(IDebugControl4), (void**)&control);
		if (status != S_OK) return status;
		status = client->QueryInterface(__uuidof(IDebugSymbols3), (void**)&symbols);
		if (status != S_OK) return status;
		status = symbols->SetSymbolOptions(0x30237); // flag used by WinDbg
		if (status != S_OK) return status;
		status = client->SetOutputCallbacks(&callback);
		if (status != S_OK) return status;
		return status;
	}

	HRESULT set_direcoties(const char* symbol_dirs, const char* image_dirs = nullptr) {
		HRESULT status = S_OK;
		if (symbol_dirs) {
			status = symbols->SetSymbolPath(symbol_dirs);
			if (status != S_OK) return status;
		}

		if (image_dirs) {
			status = symbols->SetImagePath(image_dirs);
			if (status != S_OK) return status;
		}

		// status = control->WaitForEvent(DEBUG_WAIT_DEFAULT, INFINITE);
		
		return status;
	}

	std::vector<StackFrameItem> dumpFrame(
		const char* dumpFileName, ULONG64 frameOffset = 0,
		ULONG64 stackOffset = 0, ULONG64 instructionOffset = 0
	) {
		if (client->OpenDumpFile(dumpFileName) == S_OK) {
			if (control->WaitForEvent(DEBUG_WAIT_DEFAULT, INFINITE) == S_OK) {
				if (symbols->SetScopeFromStoredEvent() == S_OK) {
					ULONG filled;
					static constexpr ULONG FrameBufSize = 32;
					DEBUG_STACK_FRAME frames[FrameBufSize];
					if (control->GetStackTrace(frameOffset, stackOffset, instructionOffset, frames, FrameBufSize, &filled) == S_OK &&
						control->OutputStackTrace(
							DEBUG_OUTCTL_ALL_CLIENTS, frames, filled,
							DEBUG_STACK_SOURCE_LINE | DEBUG_STACK_FUNCTION_INFO
						) == S_OK &&
						client->FlushCallbacks() == S_OK) {
						client->EndSession(DEBUG_END_ACTIVE_TERMINATE);
						return callback.build();
					}
				}
			}
		}
		client->EndSession(DEBUG_END_ACTIVE_TERMINATE);
		callback.clear();
		return{};
	}
};

