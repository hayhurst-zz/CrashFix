#include "stdafx.h"
#include "StackFrame.h"
#include "Log.h"

#pragma comment(lib, "dbgeng.lib")

bool StackFrameItem::from_dbg_string(const std::string& raw)
{
	moduleName.clear();
	methodName.clear();
	methodOffset = 0;
	srcFileName.clear();
	srcLineOffset = 0;
	childSP = 0;
	returnAddr = 0;
	instructionAddr = 0;
	frameNumber = 0;

	// x64 frame
	static const std::regex matcher64(R"(([0-9a-f]+) ([0-9a-f]+)`([0-9a-f]+) ([0-9a-f]+)`([0-9a-f]+) ([^!]+)!([^\+]+)\+0x([0-9a-f]+) \[([^\]]+) @ ([0-9]+)\])");
	//                                  --^^^^^^^^^---^^^^^^^^^^^^^^^^^^^^^---^^^^^^^^^^^^^^^^^^^^^---^^^^^---^^^^^^------^^^^^^^^^-----^^^^^^-----^^^^^^^^--
	//                                    Frame Num   Calling PC              Return Address          Mod     Symbol      Symbol Off    Src Name   Src Line

	// x86 frame
	static const std::regex matcher32(R"(([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+) ([^!]+)!([^\+]+)\+0x([0-9a-f]+) \[([^\]]+) @ ([0-9]+)\])");
	//                                  --^^^^^^^^^---^^^^^^^^^---^^^^^^^^^---^^^^^---^^^^^^------^^^^^^^^^-----^^^^^^-----^^^^^^^^--
	//                                    Frame Num  Calling PC  Return Addr  Mod     Symbol      Symbol Off    Src Name   Src Line

	// x64 inline frame
	static const std::regex matcher64_inlne(R"(([0-9a-f]+) \(Inline Function\) --------`-------- ([^!]+)!([^\+]+)\+0x([0-9a-f]+) \[([^\]]+) @ ([0-9]+)\])");
	//                                        --^^^^^^^^^----^^^^^^^^^^^^^^^---^^^^^^^^^^^^^^^^^--^^^^^---^^^^^^------^^^^^^^^^-----^^^^^^-----^^^^^^^^--
	//                                          Frame Num    Calling PC        Return Address     Mod     Symbol      Symbol Off    Src Name   Src Line

	// x86 inline frame
	static const std::regex matcher32_inlne(R"(([0-9a-f]+) \(Inline\) -------- ([^!]+)!([^\+]+)\+0x([0-9a-f]+) \[([^\]]+) @ ([0-9]+)\])");
	//                                        --^^^^^^^^^----^^^^^^^---^^^^^^---^^^^^---^^^^^^------^^^^^^^^^-----^^^^^^-----^^^^^^^^--
	//                                          Frame Num  Calling PC  Ret Addr  Mod    Symbol      Symbol Off    Src Name   Src Line

	// x64 frame without src line
	static const std::regex matcher64_nosrcline(R"(([0-9a-f]+) ([0-9a-f]+)`([0-9a-f]+) ([0-9a-f]+)`([0-9a-f]+) ([^!]+)!([^\+]+)\+0x([0-9a-f]+))");
	//                                            --^^^^^^^^^---^^^^^^^^^^^^^^^^^^^^^---^^^^^^^^^^^^^^^^^^^^^---^^^^^---^^^^^^------^^^^^^^^^--
	//                                              Frame Num   Calling PC              Return Address          Mod     Symbol      Symbol Off

	// x86 frame without src line
	static const std::regex matcher32_nosrcline(R"(([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+) ([^!]+)!([^\+]+)\+0x([0-9a-f]+))");
	//                                            --^^^^^^^^^---^^^^^^^^^---^^^^^^^^^---^^^^^---^^^^^^------^^^^^^^^^--
	//                                              Frame Num  Calling PC  Return Address  Mod  Symbol      Symbol Off

	// x64 frame without symbols loaded
	static const std::regex matcher64_nosymbol(R"(([0-9a-f]+) ([0-9a-f]+)`([0-9a-f]+) ([0-9a-f]+)`([0-9a-f]+) ([^!]+)\+0x([0-9a-f]+))");
	//                                           --^^^^^^^^^---^^^^^^^^^^^^^^^^^^^^^---^^^^^^^^^^^^^^^^^^^^^---^^^^^------^^^^^^^^^--
	//                                             Frame Num   Calling PC              Return Address          Mod        Mod Off

	// x86 frame without symbols loaded
	static const std::regex matcher32_nosymbol(R"(([0-9a-f]+) ([0-9a-f]+) ([0-9a-f]+) ([^!]+)\+0x([0-9a-f]+))");
	//                                           --^^^^^^^^^---^^^^^^^^^---^^^^^^^^^---^^^^^------^^^^^^^^^--
	//                                             Frame Num  Calling PC  Return Address  Mod     Mod Off

	std::smatch matchResult;
	if (std::regex_match(raw, matchResult, matcher64)) {
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

	if (std::regex_match(raw, matchResult, matcher32)) {
		if (matchResult.size() == 9) {
			frameNumber = std::stoull(matchResult[1].str(), nullptr, 16);
			childSP = std::stoull(matchResult[2].str(), nullptr, 16);
			returnAddr = std::stoull(matchResult[3].str(), nullptr, 16);
			moduleName = matchResult[4].str();
			methodName = matchResult[5].str();
			methodOffset = static_cast<size_t>(std::stoull(matchResult[6].str(), nullptr, 16));
			srcFileName = matchResult[7].str();
			srcLineOffset = static_cast<size_t>(std::stoull(matchResult[8].str(), nullptr, 10));
			return true;
		}
	}

	if (std::regex_match(raw, matchResult, matcher64_inlne) || std::regex_match(raw, matchResult, matcher32_inlne)) {
		if (matchResult.size() == 7) {
			frameNumber = std::stoull(matchResult[1].str(), nullptr, 16);
			childSP = 0;
			returnAddr = 0;
			moduleName = matchResult[2].str();
			methodName = matchResult[3].str();
			methodOffset = static_cast<size_t>(std::stoull(matchResult[4].str(), nullptr, 16));
			srcFileName = matchResult[5].str();
			srcLineOffset = static_cast<size_t>(std::stoull(matchResult[6].str(), nullptr, 10));
			return true;
		}
	}

	if (std::regex_match(raw, matchResult, matcher64_nosrcline)) {
		if (matchResult.size() == 9) {
			frameNumber = std::stoull(matchResult[1].str(), nullptr, 16);
			childSP = (std::stoull(matchResult[2].str(), nullptr, 16) << 32) + std::stoull(matchResult[3].str(), nullptr, 16);
			returnAddr = (std::stoull(matchResult[4].str(), nullptr, 16) << 32) + std::stoull(matchResult[5].str(), nullptr, 16);
			moduleName = matchResult[6].str();
			methodName = matchResult[7].str();
			methodOffset = static_cast<size_t>(std::stoull(matchResult[8].str(), nullptr, 16));
			return true;
		}
	}

	if (std::regex_match(raw, matchResult, matcher32_nosrcline)) {
		if (matchResult.size() == 7) {
			frameNumber = std::stoull(matchResult[1].str(), nullptr, 16);
			childSP = std::stoull(matchResult[2].str(), nullptr, 16);
			returnAddr = std::stoull(matchResult[3].str(), nullptr, 16);
			moduleName = matchResult[4].str();
			methodName = matchResult[5].str();
			methodOffset = static_cast<size_t>(std::stoull(matchResult[6].str(), nullptr, 16));
			return true;
		}
	}

	if (std::regex_match(raw, matchResult, matcher64_nosymbol)) {
		if (matchResult.size() == 8) {
			frameNumber = std::stoull(matchResult[1].str(), nullptr, 16);
			childSP = (std::stoull(matchResult[2].str(), nullptr, 16) << 32) + std::stoull(matchResult[3].str(), nullptr, 16);
			returnAddr = (std::stoull(matchResult[4].str(), nullptr, 16) << 32) + std::stoull(matchResult[5].str(), nullptr, 16);
			moduleName = matchResult[6].str();
			methodOffset = static_cast<size_t>(std::stoull(matchResult[7].str(), nullptr, 16));
			return true;
		}
	}

	if (std::regex_match(raw, matchResult, matcher32_nosymbol)) {
		if (matchResult.size() == 6) {
			frameNumber = std::stoull(matchResult[1].str(), nullptr, 16);
			childSP = std::stoull(matchResult[2].str(), nullptr, 16);
			returnAddr = std::stoull(matchResult[3].str(), nullptr, 16);
			moduleName = matchResult[4].str();
			methodOffset = static_cast<size_t>(std::stoull(matchResult[5].str(), nullptr, 16));
			return true;
		}
	}

	return false;
}

///////////////////////////////////////////////////////////

STDMETHODIMP StackFrameDumperCallback::QueryInterface(THIS_ _In_ REFIID InterfaceId, _Out_ PVOID* Interface)
{
	*Interface = nullptr;
	if (IsEqualIID(InterfaceId, __uuidof(IUnknown)) ||
		IsEqualIID(InterfaceId, __uuidof(IDebugOutputCallbacks))
		) {
		*Interface = (IDebugOutputCallbacks *)this;
		AddRef();
		return S_OK;
	}
	else {
		return E_NOINTERFACE;
	}
}

STDMETHODIMP StackFrameDumperCallback::Output(THIS_ _In_ ULONG Mask, _In_ PCSTR Text)
{
	UNREFERENCED_PARAMETER(Mask); // TODO
	buffer << Text;
	// std::cout << Text;
	buffer.flush(); // TODO
	return S_OK;
}

STDMETHODIMP_(ULONG) StackFrameDumperCallback::Release(THIS) {
	return 0;
}

STDMETHODIMP_(ULONG) StackFrameDumperCallback::AddRef(THIS) {
	return 1;
}

std::vector<StackFrameItem> StackFrameDumperCallback::build(DWORD dwThreadId)
{
	std::string content = buffer.str();
	if(m_pLog)
		m_pLog->write(1, "Dump stack frames for thread 0x%x:\n%s\n", dwThreadId, content.c_str());
	
	std::string line;
	StackFrameItem item;
	std::vector<StackFrameItem> ret;
	while (std::getline(buffer, line)) {
		if (item.from_dbg_string(line)) {
			item.instructionAddr = pFrames[item.frameNumber].InstructionOffset;
			ret.push_back(item);
		}
		// TODO: else?
	}
	clear();
	return ret;
}

void StackFrameDumperCallback::clear()
{
	buffer.clear();
	buffer.str(std::string()); // TODO
}

bool StackFrameDumperCallback::setFrames(DEBUG_STACK_FRAME_EX* ptr, std::size_t count)
{
	pFrames = ptr;
	frameCount = count;
	return true;
}

void StackFrameDumperCallback::setLogger(CLog* pLog)
{
	m_pLog = pLog;
}

//////////////////////////////////////////////////////////////////////

StackFrameDumper::~StackFrameDumper()
{
	if (client) client->Release();
	if (control) control->Release();
	if (symbols) symbols->Release();
}

HRESULT StackFrameDumper::init(CLog* pLog)
{
	if (m_initilaized)
		return S_OK;

	HRESULT status = S_OK;
	status = DebugCreate(__uuidof(IDebugClient4), (void**)&client);
	if (status != S_OK) return status;

	status = client->QueryInterface(__uuidof(IDebugControl5), (void**)&control);
	if (status != S_OK) return status;

	status = client->QueryInterface(__uuidof(IDebugSymbols3), (void**)&symbols);
	if (status != S_OK) return status;

	status = symbols->SetSymbolOptions(0x30237); // flag used by WinDbg
	if (status != S_OK) return status;

	status = client->SetOutputCallbacks(&callback);
	if (status != S_OK) return status;

	m_initilaized = true;

	callback.setLogger(pLog);

	return status;
}

HRESULT StackFrameDumper::set_direcoties(const char* symbol_dirs, const char* image_dirs)
{
	HRESULT status = S_OK;
	if (symbol_dirs) 
	{
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

	if (image_dirs) 
	{
		status = symbols->SetImagePath(image_dirs);
		if (status != S_OK) return status;
	}

	// status = control->WaitForEvent(DEBUG_WAIT_DEFAULT, INFINITE);

	return status;
}

bool StackFrameDumper::open(const char* dumpFileName)
{
	if (FAILED(client->OpenDumpFile(dumpFileName)))
		return false;

	if (FAILED(control->WaitForEvent(DEBUG_WAIT_DEFAULT, INFINITE)))
		return false;

	if (FAILED(symbols->SetScopeFromStoredEvent()))
		return false;

	return true;
}

void StackFrameDumper::close()
{
	client->EndSession(DEBUG_END_ACTIVE_TERMINATE);
}

std::vector<StackFrameItem> StackFrameDumper::dumpFrame(DWORD dwThreadId, ULONG64 frameOffset /*= 0*/, ULONG64 stackOffset /*= 0*/, ULONG64 instructionOffset /*= 0 */)
{
	ULONG filled;
	static constexpr ULONG FrameBufSize = 64;
	DEBUG_STACK_FRAME_EX frames[FrameBufSize];

	do 
	{
		callback.clear();

		HRESULT hr = control->GetStackTraceEx(frameOffset, stackOffset, instructionOffset, frames, FrameBufSize, &filled);
		if (FAILED(hr))
			break;
		
		if (!callback.setFrames(frames, filled))
			break;

		hr = control->OutputStackTraceEx(
			DEBUG_OUTCTL_ALL_CLIENTS, frames, filled,
			DEBUG_STACK_SOURCE_LINE | DEBUG_STACK_FRAME_ADDRESSES | DEBUG_STACK_FRAME_NUMBERS
		);
		if (FAILED(hr))
			break;
		
		hr = client->FlushCallbacks();
		if (FAILED(hr))
			break;

		return callback.build(dwThreadId);
	} while (false);

	return{};
}

std::map<std::string, bool> StackFrameDumper::dumpModuleSymbolStatus()
{
	std::map<std::string, bool> result;
	DEBUG_MODULE_PARAMETERS* Module_Params = nullptr;

	do 
	{
		HRESULT hr = S_OK;
		
		ULONG Loaded = 0;
		ULONG Unloaded = 0;
		hr = symbols->GetNumberModules(&Loaded, &Unloaded);
		if (FAILED(hr))
			break;

		Module_Params = new DEBUG_MODULE_PARAMETERS[Loaded];
		hr = symbols->GetModuleParameters(Loaded, nullptr, 0, Module_Params);
		if (FAILED(hr))
			break;

		char buf[MAX_PATH] = { 0 };
		ULONG NameSize = 0;
		for (ULONG i = 0; i < Loaded; i++)
		{
			hr = symbols->GetModuleNameString(DEBUG_MODNAME_IMAGE, i, 0, buf, MAX_PATH, &NameSize);
			if (FAILED(hr))
				continue;

			std::string modname = buf;
			size_t pos = modname.rfind('\\');
			if (pos != modname.npos)
				modname = modname.substr(pos + 1);

			result[modname] = Module_Params[i].SymbolType == DEBUG_SYMTYPE_PDB;
		}

	} while (false);

	delete[] Module_Params;

	return result;
}
