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
	std::uint64_t childSP;
	std::uint64_t returnAddr;
	std::uint64_t instructionAddr;
	std::size_t frameNumber;

public:
	bool from_dbg_string(const std::string& raw) {
		moduleName.clear();
		methodName.clear();
		methodOffset = 0;
		srcFileName.clear();
		srcLineOffset = 0;
		childSP = 0;
		returnAddr = 0;
		instructionAddr = 0;
		frameNumber = 0;
		static const std::regex matcher(R"(([0-9a-f]+) ([0-9a-f]+)`([0-9a-f]+) ([0-9a-f]+)`([0-9a-f]+) ([^!]+)!([^\+]+)\+0x([0-9a-f]+) \[([^\]]+) @ ([0-9]+)\])");
		//                                --^^^^^^^^^---^^^^^^^^^^^^^^^^^^^^^---^^^^^^^^^^^^^^^^^^^^^---^^^^^---^^^^^^------^^^^^^^^^-----^^^^^^-----^^^^^^^^----
		//                                  Frame Num   Calling PC              Return Address           Mod     Symbol      Symbol Off    Src Name   Src Line
		
		std::smatch matchResult;
		if (std::regex_match(raw, matchResult, matcher)) {
			if (matchResult.size() == 11) {
				frameNumber = std::stoull(matchResult[1].str(), nullptr, 16);
				childSP = (std::stoull(matchResult[2].str(), nullptr, 16) << 32) + std::stoull(matchResult[3].str(), nullptr, 16);
				returnAddr = (std::stoull(matchResult[4].str(), nullptr, 16) << 32) + std::stoull(matchResult[5].str(), nullptr, 16);
				moduleName = matchResult[6].str();
				methodName = matchResult[7].str();
				methodOffset = static_cast<size_t>(std::stoull(matchResult[8].str(), nullptr, 16));
				srcFileName = matchResult[9].str();
				srcLineOffset = static_cast<size_t>(std::stoull(matchResult[10].str(), nullptr, 10));
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
			if (item.from_dbg_string(line)) {
				item.instructionAddr = pFrames[item.frameNumber].InstructionOffset;
				ret.push_back(item);
			}
			// TODO: else?
		}
		clear();
		return ret;
	}

	void clear() {
		buffer.clear();
		buffer.str(std::string()); // TODO
	}

	bool setFrames(DEBUG_STACK_FRAME* ptr, std::size_t count) {
		pFrames = ptr;
		frameCount = count;
		return true;
	}

private:
	std::stringstream buffer;
	DEBUG_STACK_FRAME* pFrames=nullptr;
	std::size_t frameCount=0;
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
			auto symbolStoreEnvName = "_NT_SYMBOL_PATH";
			auto symbolStoreSize = GetEnvironmentVariableA(symbolStoreEnvName, nullptr, 0);
			if (symbolStoreSize != 0) {
				auto store = new char[symbolStoreSize]();
				if (GetEnvironmentVariableA(symbolStoreEnvName, store, symbolStoreSize) != 0) {
					status = symbols->AppendSymbolPath(store);
				}
				delete[] store;
			}
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
						callback.setFrames(frames, filled) &&
						control->OutputStackTrace(
							DEBUG_OUTCTL_ALL_CLIENTS, frames, filled,
							DEBUG_STACK_SOURCE_LINE | DEBUG_STACK_FRAME_ADDRESSES | DEBUG_STACK_FRAME_NUMBERS
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

