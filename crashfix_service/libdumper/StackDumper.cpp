/************************************************************************************* 
This file is a part of CrashRpt library.
Copyright (c) 2003-2013 The CrashRpt project authors. All Rights Reserved.

Use of this source code is governed by a BSD-style license
that can be found in the License.txt file in the root of the source
tree. All contributing project authors may
be found in the Authors.txt file in the root of the source tree.
***************************************************************************************/

#include "stdafx.h"
#include "StackDumper.h"
#include "dbghelp.h"
#include <assert.h>
#include "strconv.h"
#include "md5.h"
#include "StackWalker.h"

#pragma comment( lib, "dbghelp.lib" )

namespace StackDumper {

CMiniDumpReader* g_pMiniDumpReader = NULL;

// Callback function prototypes

class CMiniDumpReaderCallback
{
public:
static BOOL CALLBACK ReadProcessMemoryProc64(
                                      HANDLE hProcess,
                                      DWORD64 lpBaseAddress,
                                      PVOID lpBuffer,
                                      DWORD nSize,
                                      LPDWORD lpNumberOfBytesRead);

static PVOID CALLBACK FunctionTableAccessProc64(
    HANDLE hProcess,
    DWORD64 AddrBase);

static DWORD64 CALLBACK GetModuleBaseProc64(
                                     HANDLE hProcess,
                                     DWORD64 Address);

static BOOL CALLBACK SymRegisterCallbackProc64(
                                        HANDLE hProcess,
                                        ULONG ActionCode,
                                        ULONG64 CallbackData,
                                        ULONG64 UserContext
                                        );
};

CMiniDumpReader::CMiniDumpReader()
{
    m_bLoaded = FALSE;
    m_bReadSysInfoStream = FALSE;
    m_bReadExceptionStream = FALSE;
    m_bReadModuleListStream = FALSE;
    m_bReadMemoryListStream = FALSE;
    m_bReadThreadListStream = FALSE;
    m_hFileMiniDump = INVALID_HANDLE_VALUE;
    m_hFileMapping = NULL;
    m_pMiniDumpStartPtr = NULL;  
}

CMiniDumpReader::~CMiniDumpReader()
{
    Close();
}

bool CMiniDumpReader::DumpFrame(ULONG32 dwThreadId, ULONG64 frameOffset, ULONG64 stackOffset, ULONG64 instructionOffset, std::vector<CStackFrame>& stackFrames)
{
	stackFrames.clear();

	if (StackWalk(dwThreadId, frameOffset, stackOffset, instructionOffset) != 0)
		return false;
	int nThreadIndex = GetThreadRowIdByThreadId(dwThreadId);
	if (nThreadIndex < 0 || nThreadIndex >= m_DumpData.m_Threads.size())
		return false;
	for (const auto& frame : m_DumpData.m_Threads[nThreadIndex].m_StackTrace)
	{
		CStackFrame StackFrame;
		StackFrame.m_dwAddrPC = frame.m_dwAddrPCOffset;
		StackFrame.m_sSymbolName = CW2A(frame.m_sSymbolName);
		StackFrame.m_dwOffsInSymbol = frame.m_dw64OffsInSymbol;
		StackFrame.m_sSrcFileName = frame.m_sSrcFileName;
		StackFrame.m_nSrcLineNumber = frame.m_nSrcLineNumber;
		StackFrame.m_dwOffsInLine = frame.m_dwOffsInLine;

		if (frame.m_nModuleRowID >= 0 && frame.m_nModuleRowID < m_DumpData.m_Modules.size())
		{
			const auto& module = m_DumpData.m_Modules[frame.m_nModuleRowID];
			StackFrame.m_sModuleName = module.m_sModuleName;;
			StackFrame.m_dwOffsInModule = StackFrame.m_dwAddrPC - module.m_uBaseAddr;
		}
		stackFrames.push_back(StackFrame);
	}

	return true;
}

std::map<std::string, bool> CMiniDumpReader::DumpModuleSymbolStatus()
{
	std::map<std::string, bool> result;

	for (const auto& module : m_DumpData.m_Modules)
	{
		std::string modname = CW2A(module.m_sModuleName);
		result[modname] = !module.m_bNoSymbolInfo;
	}

	return result;
}

void CMiniDumpReader::SetDirecoties(CString sSymSearchPath)
{
	m_sSymSearchPath = sSymSearchPath;
}

int CMiniDumpReader::Open(CString sFileName)
{  
    static DWORD_PTR dwProcessID = 0;

    if(m_bLoaded)
    {
		// Already loaded
        return 0;
    }

    m_sFileName = sFileName;

    m_hFileMiniDump = CreateFile(
        sFileName, 
        FILE_GENERIC_READ, 
		FILE_SHARE_READ,
        NULL, 
        OPEN_EXISTING, 
        NULL, 
        NULL);

    if(m_hFileMiniDump==INVALID_HANDLE_VALUE)
    {
        Close();
        return 1;
    }

    m_hFileMapping = CreateFileMapping(
        m_hFileMiniDump, 
        NULL, 
        PAGE_READONLY, 
        0, 
        0, 
        0);

    if(m_hFileMapping==NULL)
    {
        Close();
        return 2;
    }

    m_pMiniDumpStartPtr = MapViewOfFile(
        m_hFileMapping, 
        FILE_MAP_READ, 
        0, 
        0, 
        0);

    if(m_pMiniDumpStartPtr==NULL)
    {    
        Close();
        return 3;
    }

    m_DumpData.m_hProcess = (HANDLE)(++dwProcessID);  

    DWORD dwOptions = 0;
    //dwOptions |= SYMOPT_DEFERRED_LOADS; // Symbols are not loaded until a reference is made requiring the symbols be loaded.
    dwOptions |= SYMOPT_EXACT_SYMBOLS; // Do not load an unmatched .pdb file. 
    dwOptions |= SYMOPT_FAIL_CRITICAL_ERRORS; // Do not display system dialog boxes when there is a media failure such as no media in a drive.
    //dwOptions |= SYMOPT_UNDNAME; // All symbols are presented in undecorated form.   
    SymSetOptions(dwOptions);

    BOOL bSymInit = SymInitializeW(
        m_DumpData.m_hProcess,
        CT2W(m_sSymSearchPath),
        FALSE);

    if(!bSymInit)
    {
        m_DumpData.m_hProcess = NULL;
        Close();
        return 5;
    }

    /*SymRegisterCallbackW64(
    m_DumpData.m_hProcess, 
    SymRegisterCallbackProc64,
    (ULONG64)this);*/

    m_bReadSysInfoStream = !ReadSysInfoStream();  
    m_bReadModuleListStream = !ReadModuleListStream();
    m_bReadThreadListStream = !ReadThreadListStream();
    m_bReadMemoryListStream = !ReadMemoryListStream();    
    m_bReadExceptionStream = !ReadExceptionStream();    

    m_bLoaded = true;
    return 0;
}

//BOOL CALLBACK CMiniDumpReaderCallback::SymRegisterCallbackProc64(
//  HANDLE hProcess,
//  ULONG ActionCode,
//  ULONG64 CallbackData,
//  ULONG64 UserContext
//)
//{
//  UNREFERENCED_PARAMETER(hProcess);
//  CMiniDumpReader* pMdmpReader = (CMiniDumpReader*)UserContext;
//
//  switch(ActionCode)
//  {
//  case CBA_DEBUG_INFO:
//    {
//      LPCTSTR szMessage = (LPCTSTR)CallbackData;
//      pMdmpReader->m_DumpData.m_LoadLog.push_back(szMessage);
//    } 
//    return TRUE;   
//  case CBA_DEFERRED_SYMBOL_LOAD_CANCEL:
//    {
//      // Ignore      
//    } 
//    return FALSE;    
//  case CBA_DEFERRED_SYMBOL_LOAD_COMPLETE:
//    {
//      IMAGEHLP_DEFERRED_SYMBOL_LOADW64* pLoadInfo = 
//        (IMAGEHLP_DEFERRED_SYMBOL_LOADW64*)CallbackData;
//      CString sMessage;
//      sMessage.Format(_T("Completed loading symbols for %s."), pLoadInfo->FileName);
//      //pMdmpReader->m_DumpData.m_LoadLog.push_back(sMessage);
//    }
//    return TRUE;
//  case CBA_DEFERRED_SYMBOL_LOAD_FAILURE:
//    {
//      IMAGEHLP_DEFERRED_SYMBOL_LOADW64* pLoadInfo = 
//        (IMAGEHLP_DEFERRED_SYMBOL_LOADW64*)CallbackData;
//      CString sMessage;
//      sMessage.Format(_T("Failed to loaded symbols for %s."), pLoadInfo->FileName);
//      pMdmpReader->m_DumpData.m_LoadLog.push_back(sMessage);
//    }
//    return TRUE;
//  case CBA_DEFERRED_SYMBOL_LOAD_PARTIAL:
//    {
//      IMAGEHLP_DEFERRED_SYMBOL_LOADW64* pLoadInfo = 
//        (IMAGEHLP_DEFERRED_SYMBOL_LOADW64*)CallbackData;
//      CString sMessage;
//      sMessage.Format(_T("Partially loaded symbols for %s."), pLoadInfo->FileName);
//      pMdmpReader->m_DumpData.m_LoadLog.push_back(sMessage);
//    }
//    return TRUE;
//  case CBA_DEFERRED_SYMBOL_LOAD_START:
//    {
//      IMAGEHLP_DEFERRED_SYMBOL_LOADW64* pLoadInfo = 
//        (IMAGEHLP_DEFERRED_SYMBOL_LOADW64*)CallbackData;
//      CString sMessage;
//      sMessage.Format(_T("Started loading symbols for %s."), pLoadInfo->FileName);
//      //pMdmpReader->m_DumpData.m_LoadLog.push_back(sMessage);
//    }
//    return TRUE;
//  }
//
//  return FALSE;
//}

void CMiniDumpReader::Close()
{
    UnmapViewOfFile(m_pMiniDumpStartPtr);

    if(m_hFileMapping!=NULL)
    {
        CloseHandle(m_hFileMapping);
    }

    if(m_hFileMiniDump!=INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hFileMiniDump);
    }

    if(m_DumpData.m_hProcess!=NULL)
    {
        SymCleanup(m_DumpData.m_hProcess);
    }

	m_bLoaded = false;
	m_bReadSysInfoStream = FALSE;
	m_bReadExceptionStream = FALSE;
	m_bReadModuleListStream = FALSE;
	m_bReadMemoryListStream = FALSE;
	m_bReadThreadListStream = FALSE;
	m_hFileMiniDump = INVALID_HANDLE_VALUE;
	m_hFileMapping = NULL;
	m_pMiniDumpStartPtr = NULL;

	m_DumpData = MdmpData();
}

BOOL CMiniDumpReader::CheckDbgHelpApiVersion()
{
    // Set valid dbghelp API version
    API_VERSION CompiledApiVer;
    CompiledApiVer.MajorVersion = 6;
    CompiledApiVer.MinorVersion = 1;
    CompiledApiVer.Revision = 11;    
    CompiledApiVer.Reserved = 0;
    
	LPAPI_VERSION pActualApiVer = ImagehlpApiVersionEx(&CompiledApiVer);    
    
	// Expected v6.11 or above
	if (CompiledApiVer.MajorVersion < pActualApiVer->MajorVersion)
		return true;

	if(CompiledApiVer.MajorVersion == pActualApiVer->MajorVersion &&
        CompiledApiVer.MinorVersion <= pActualApiVer->MinorVersion)
		return true;

	return false;
}

// Extracts a UNICODE string stored in minidump file by its relative address
CString CMiniDumpReader::GetMinidumpString(LPVOID start_addr, RVA rva)
{
    MINIDUMP_STRING* pms = (MINIDUMP_STRING*)((LPBYTE)start_addr+rva);
    //CString sModule = CString(pms->Buffer, pms->Length);
    CString sModule = pms->Buffer;  
    return sModule;
}

int CMiniDumpReader::ReadSysInfoStream()
{
    LPVOID pStreamStart = NULL;
    ULONG uStreamSize = 0;
    MINIDUMP_DIRECTORY* pmd = NULL;
    BOOL bRead = FALSE;

    bRead = MiniDumpReadDumpStream(m_pMiniDumpStartPtr, SystemInfoStream, 
        &pmd, &pStreamStart, &uStreamSize);

    if(bRead)
    {
        MINIDUMP_SYSTEM_INFO* pSysInfo = (MINIDUMP_SYSTEM_INFO*)pStreamStart;

        m_DumpData.m_uProcessorArchitecture = pSysInfo->ProcessorArchitecture;
        m_DumpData.m_uchNumberOfProcessors = pSysInfo->NumberOfProcessors;
        m_DumpData.m_uchProductType = pSysInfo->ProductType;
        m_DumpData.m_ulVerMajor = pSysInfo->MajorVersion;
        m_DumpData.m_ulVerMinor = pSysInfo->MinorVersion;
        m_DumpData.m_ulVerBuild = pSysInfo->BuildNumber;
        m_DumpData.m_sCSDVer = GetMinidumpString(m_pMiniDumpStartPtr, pSysInfo->CSDVersionRva);

        // Clean up
        pStreamStart = NULL;
        uStreamSize = 0;    
        pmd = NULL;
    }
    else 
    {
        return 1;    
    }

    return 0;
}

int CMiniDumpReader::ReadExceptionStream()
{
    LPVOID pStreamStart = NULL;
    ULONG uStreamSize = 0;
    MINIDUMP_DIRECTORY* pmd = NULL;
    BOOL bRead = FALSE;

    bRead = MiniDumpReadDumpStream(
        m_pMiniDumpStartPtr, 
        ExceptionStream, 
        &pmd, 
        &pStreamStart, 
        &uStreamSize);

    if(bRead)
    {
        MINIDUMP_EXCEPTION_STREAM* pExceptionStream = (MINIDUMP_EXCEPTION_STREAM*)pStreamStart;
        if(pExceptionStream!=NULL && 
            uStreamSize>=sizeof(MINIDUMP_EXCEPTION_STREAM))
        {
            m_DumpData.m_uExceptionThreadId = pExceptionStream->ThreadId;
            m_DumpData.m_uExceptionCode = pExceptionStream->ExceptionRecord.ExceptionCode;
            m_DumpData.m_uExceptionAddress = pExceptionStream->ExceptionRecord.ExceptionAddress;          
            m_DumpData.m_pExceptionThreadContext = (CONTEXT*)(((LPBYTE)m_pMiniDumpStartPtr)+pExceptionStream->ThreadContext.Rva);      
			m_DumpData.m_uExceptionThreadContextSize = pExceptionStream->ThreadContext.DataSize;

            CString sMsg;
            int nExcModuleRowID = GetModuleRowIdByAddress(m_DumpData.m_uExceptionAddress);
            if(nExcModuleRowID>=0)
            {
                sMsg.Format(_T("Unhandled exception at 0x%I64x in %s: 0x%x : %s"),
                    m_DumpData.m_uExceptionAddress,
                    (LPCTSTR)m_DumpData.m_Modules[nExcModuleRowID].m_sModuleName,
                    m_DumpData.m_uExceptionCode,
                    _T("Exception description.")
                    );
            }
            else
            {

            }
            m_DumpData.m_LoadLog.push_back(sMsg);
        }    
    }
    else
    {
        CString sMsg;
        sMsg = _T("No exception information found in minidump.");
        m_DumpData.m_LoadLog.push_back(sMsg);
        return 1;
    }

    return 0;
}

int CMiniDumpReader::ReadModuleListStream()
{
    LPVOID pStreamStart = NULL;
    ULONG uStreamSize = 0;
    MINIDUMP_DIRECTORY* pmd = NULL;
    BOOL bRead = FALSE;

    bRead = MiniDumpReadDumpStream(
        m_pMiniDumpStartPtr, 
        ModuleListStream, 
        &pmd, 
        &pStreamStart, 
        &uStreamSize);

    if(bRead)
    {
        MINIDUMP_MODULE_LIST* pModuleStream = (MINIDUMP_MODULE_LIST*)pStreamStart;
        if(pModuleStream!=NULL)
        {
            ULONG32 uNumberOfModules = pModuleStream->NumberOfModules;
            ULONG32 i;
            for(i=0; i<uNumberOfModules; i++)
            {
                MINIDUMP_MODULE* pModule = 
                    (MINIDUMP_MODULE*)((LPBYTE)pModuleStream->Modules+i*sizeof(MINIDUMP_MODULE));

                CString sModuleName = GetMinidumpString(m_pMiniDumpStartPtr, pModule->ModuleNameRva);               
				CStringW szModuleName = CT2W(sModuleName);
                DWORD64 dwBaseAddr = pModule->BaseOfImage;
                DWORD64 dwImageSize = pModule->SizeOfImage;

				CString sShortModuleName = sModuleName;
                int pos = -1;
                pos = sModuleName.ReverseFind('\\');
                if(pos>=0)
                    sShortModuleName = sShortModuleName.Mid(pos+1);          

                DWORD64 dwLoadResult = SymLoadModuleExW(
                    m_DumpData.m_hProcess,
                    NULL,
                    (LPCWSTR)szModuleName,
                    NULL,
                    dwBaseAddr,
                    (DWORD)dwImageSize,
                    NULL,
                    0);         

                IMAGEHLP_MODULE64 modinfo;
                memset(&modinfo, 0, sizeof(IMAGEHLP_MODULE64));
                modinfo.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
                BOOL bModuleInfo = SymGetModuleInfo64(m_DumpData.m_hProcess,
                    dwBaseAddr, 
                    &modinfo);
                MdmpModule m;
                if(!bModuleInfo)
                {          
                    m.m_bImageUnmatched = TRUE;
                    m.m_bNoSymbolInfo = TRUE;
                    m.m_bPdbUnmatched = TRUE;
                    m.m_pVersionInfo = NULL;
                    m.m_sImageName = sModuleName;
                    m.m_sModuleName = sShortModuleName;
                    m.m_uBaseAddr = dwBaseAddr;
                    m.m_uImageSize = dwImageSize;          
                }
                else
                {          
                    m.m_uBaseAddr = modinfo.BaseOfImage;
                    m.m_uImageSize = modinfo.ImageSize;        
                    m.m_sModuleName = sShortModuleName;
                    m.m_sImageName = modinfo.ImageName;
                    m.m_sLoadedImageName = modinfo.LoadedImageName;
                    m.m_sLoadedPdbName = modinfo.LoadedPdbName;
                    m.m_pVersionInfo = &pModule->VersionInfo;
                    m.m_bPdbUnmatched = modinfo.PdbUnmatched;          
                    BOOL bTimeStampMatched = pModule->TimeDateStamp == modinfo.TimeDateStamp;
                    m.m_bImageUnmatched = !bTimeStampMatched;
                    m.m_bNoSymbolInfo = modinfo.SymType != SymPdb;
                }        

                m_DumpData.m_Modules.push_back(m);
                m_DumpData.m_ModuleIndex[m.m_uBaseAddr] = m_DumpData.m_Modules.size()-1;          

                CString sMsg;
                if(m.m_bImageUnmatched)
                    sMsg.Format(_T("Loaded '*%s'"), (LPCTSTR)sModuleName);
                else
                    sMsg.Format(_T("Loaded '%s'"), (LPCTSTR)m.m_sLoadedImageName);

                if(m.m_bImageUnmatched)
                    sMsg += _T(", No matching binary found.");          
                else if(m.m_bPdbUnmatched)
                    sMsg += _T(", No matching PDB file found.");          
                else
                {
                    if(m.m_bNoSymbolInfo)            
                        sMsg += _T(", No symbols loaded.");          
                    else
                        sMsg += _T(", Symbols loaded.");          
                }
                m_DumpData.m_LoadLog.push_back(sMsg);
            }
        }
    }
    else
    {
        return 1;
    }

    return 0;
}

int CMiniDumpReader::GetModuleRowIdByBaseAddr(DWORD64 dwBaseAddr)
{
    std::map<DWORD64, size_t>::iterator it = m_DumpData.m_ModuleIndex.find(dwBaseAddr);
    if(it!=m_DumpData.m_ModuleIndex.end())
        return (int)it->second;
    return -1;
}

int CMiniDumpReader::GetModuleRowIdByAddress(DWORD64 dwAddress)
{
    UINT i;
    for(i=0;i<m_DumpData.m_Modules.size();i++)
    {
        if(m_DumpData.m_Modules[i].m_uBaseAddr<=dwAddress && 
            dwAddress<m_DumpData.m_Modules[i].m_uBaseAddr+m_DumpData.m_Modules[i].m_uImageSize)
            return i;
    }

    return -1;
}

int CMiniDumpReader::GetThreadRowIdByThreadId(ULONG32 dwThreadId)
{
    std::map<DWORD, ULONG32>::iterator it = m_DumpData.m_ThreadIndex.find(dwThreadId);
    if(it!=m_DumpData.m_ThreadIndex.end())
        return (int)it->second;
    return -1;
}

int CMiniDumpReader::ReadMemoryListStream()
{
    LPVOID pStreamStart = NULL;
    ULONG uStreamSize = 0;
    MINIDUMP_DIRECTORY* pmd = NULL;
    BOOL bRead = FALSE;

    bRead = MiniDumpReadDumpStream(
        m_pMiniDumpStartPtr, 
        MemoryListStream, 
        &pmd, 
        &pStreamStart, 
        &uStreamSize);

    if(bRead)
    {
        MINIDUMP_MEMORY_LIST* pMemStream = (MINIDUMP_MEMORY_LIST*)pStreamStart;
        if(pMemStream!=NULL)
        {
            ULONG32 uNumberOfMemRanges = pMemStream->NumberOfMemoryRanges;
            ULONG i;
            for(i=0; i<uNumberOfMemRanges; i++)
            {
                MINIDUMP_MEMORY_DESCRIPTOR* pMemDesc = (MINIDUMP_MEMORY_DESCRIPTOR*)(&pMemStream->MemoryRanges[i]);
                MdmpMemRange mr;
                mr.m_u64StartOfMemoryRange = pMemDesc->StartOfMemoryRange;
                mr.m_uDataSize = pMemDesc->Memory.DataSize;
                mr.m_pStartPtr = (LPBYTE)m_pMiniDumpStartPtr+pMemDesc->Memory.Rva;

                m_DumpData.m_MemRanges.push_back(mr);
            }
        }
    }
    else    
    {
        return 1;
    }

    return 0;
}

int CMiniDumpReader::ReadThreadListStream()
{
    LPVOID pStreamStart = NULL;
    ULONG uStreamSize = 0;
    MINIDUMP_DIRECTORY* pmd = NULL;
    BOOL bRead = FALSE;

    bRead = MiniDumpReadDumpStream(
        m_pMiniDumpStartPtr, 
        ThreadListStream, 
        &pmd, 
        &pStreamStart, 
        &uStreamSize);

    if(bRead)
    {
        MINIDUMP_THREAD_LIST* pThreadList = (MINIDUMP_THREAD_LIST*)pStreamStart;
        if(pThreadList!=NULL && 
            uStreamSize>=sizeof(MINIDUMP_THREAD_LIST))
        {
            ULONG32 uThreadCount = pThreadList->NumberOfThreads;

            ULONG32 i;
            for(i=0; i<uThreadCount; i++)
            {
                MINIDUMP_THREAD* pThread = (MINIDUMP_THREAD*)(&pThreadList->Threads[i]);

                MdmpThread mt;
                mt.m_dwThreadId = pThread->ThreadId;
                mt.m_pThreadContext = (CONTEXT*)(((LPBYTE)m_pMiniDumpStartPtr)+pThread->ThreadContext.Rva);
				mt.m_uThreadContextSize = pThread->ThreadContext.DataSize;

                m_DumpData.m_Threads.push_back(mt);
                m_DumpData.m_ThreadIndex[mt.m_dwThreadId] = m_DumpData.m_Threads.size()-1;        
            }
        }  
    }
    else
    {
        return 1;
    }

    return 0;
}

// comply with macro define for _WIN64 in DbgHelp.h
#ifdef StackWalk
	#undef StackWalk
#endif

int CMiniDumpReader::StackWalk(ULONG32 dwThreadId, ULONG64 frameOffset,	ULONG64 stackOffset, ULONG64 instructionOffset)
{ 
    int nThreadIndex = GetThreadRowIdByThreadId(dwThreadId);
    if(m_DumpData.m_Threads[nThreadIndex].m_bStackWalk == TRUE)
        return 0; // Already done

    CONTEXT* pThreadContext = NULL;
	ULONG32 uThreadContextSize = 0;

    if(m_DumpData.m_Threads[nThreadIndex].m_dwThreadId==m_DumpData.m_uExceptionThreadId)
	{
		pThreadContext = m_DumpData.m_pExceptionThreadContext;
		uThreadContextSize = m_DumpData.m_uExceptionThreadContextSize;
	}
    else
	{
		pThreadContext = m_DumpData.m_Threads[nThreadIndex].m_pThreadContext;
		uThreadContextSize = m_DumpData.m_Threads[nThreadIndex].m_uThreadContextSize;
	}

    if(pThreadContext==NULL)
        return 1;

    // Make modifiable context
	std::vector<byte> buf(uThreadContextSize, 0);
    PCONTEXT pContext = (PCONTEXT)buf.data();
	int s = sizeof(CONTEXT);
    memcpy(pContext, pThreadContext, uThreadContextSize);

	g_pMiniDumpReader = this;

	// Init stack frame with correct initial values
    // See this:
    // http://www.codeproject.com/KB/threads/StackWalker.aspx
    //
    // Given a current dbghelp, your code should:
    //  1. Always use StackWalk64
    //  2. Always set AddrPC to the current instruction pointer (Eip on x86, Rip on x64 and StIIP on IA64)
    //  3. Always set AddrStack to the current stack pointer (Esp on x86, Rsp on x64 and IntSp on IA64)
    //  4. Set AddrFrame to the current frame pointer when meaningful. On x86 this is Ebp, on x64 you 
    //     can use Rbp (but is not used by VC2005B2; instead it uses Rdi!) and on IA64 you can use RsBSP. 
    //     StackWalk64 will ignore the value when it isn't needed for unwinding.
    //  5. Set AddrBStore to RsBSP for IA64. 

    STACKFRAME64 sf;
    memset(&sf, 0, sizeof(STACKFRAME64));

    sf.AddrPC.Mode = AddrModeFlat;  
    sf.AddrFrame.Mode = AddrModeFlat;  
    sf.AddrStack.Mode = AddrModeFlat;  
    sf.AddrBStore.Mode = AddrModeFlat;  

    DWORD dwMachineType = 0;
    switch(m_DumpData.m_uProcessorArchitecture)
    {
  case PROCESSOR_ARCHITECTURE_INTEL: 
      dwMachineType = IMAGE_FILE_MACHINE_I386;
#ifdef _X86_
	  sf.AddrPC.Offset = pThreadContext->Eip;
      sf.AddrStack.Offset = pThreadContext->Esp;
      sf.AddrFrame.Offset = pThreadContext->Ebp;
#else
	  return 1; // x86 minidump unsupported by x64 dbghelp.dll
#endif
	  break;
  case PROCESSOR_ARCHITECTURE_AMD64:
      dwMachineType = IMAGE_FILE_MACHINE_AMD64;
#ifdef _AMD64_
	  sf.AddrPC.Offset = pThreadContext->Rip;
      sf.AddrStack.Offset = pThreadContext->Rsp;
      sf.AddrFrame.Offset = pThreadContext->Rbp;
#else
	  return 1; // x64 minidump unsupported by x86 dbghelp.dll
#endif
	  break;
  case PROCESSOR_ARCHITECTURE_IA64:
      dwMachineType = IMAGE_FILE_MACHINE_IA64;
#ifdef _IA64_
	  sf.AddrPC.Offset = pThreadContext->StIIP;
      sf.AddrStack.Offset = pThreadContext->IntSp;
      sf.AddrFrame.Offset = pThreadContext->RsBSP;    
      sf.AddrBStore.Offset = pThreadContext->RsBSP;
#else
	  return 1; // Unsupported architecture
#endif 
	  break;
  default:
      {
          assert(0);
          return 1; // Unsupported architecture
      }
    }

	if (frameOffset && stackOffset && instructionOffset)
	{
		sf.AddrPC.Offset = instructionOffset;
		sf.AddrStack.Offset = stackOffset;
		sf.AddrFrame.Offset = frameOffset;
	}

    for(;;)
    {    
        BOOL bWalk = ::StackWalk64(
            dwMachineType,               // machine type
            m_DumpData.m_hProcess,       // our process handle
            (HANDLE)dwThreadId,          // thread ID
            &sf,                         // stack frame
            dwMachineType==IMAGE_FILE_MACHINE_I386?NULL:(pContext), // used for non-I386 machines 
			&CMiniDumpReaderCallback::ReadProcessMemoryProc64,     // our routine
			&CMiniDumpReaderCallback::FunctionTableAccessProc64,   // our routine
			&CMiniDumpReaderCallback::GetModuleBaseProc64,         // our routine
            NULL                         // safe to be NULL
            );

        if(!bWalk)
            break;      

        MdmpStackFrame stack_frame;
        stack_frame.m_dwAddrPCOffset = sf.AddrPC.Offset;

        // Get module info
        IMAGEHLP_MODULE64 mi;
        memset(&mi, 0, sizeof(IMAGEHLP_MODULE64));
        mi.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
        BOOL bGetModuleInfo = SymGetModuleInfo64(m_DumpData.m_hProcess, sf.AddrPC.Offset, &mi);     
        if(bGetModuleInfo)
        {
            stack_frame.m_nModuleRowID = GetModuleRowIdByBaseAddr(mi.BaseOfImage);      
        }

        // Get symbol info
        DWORD64 dwDisp64;
        BYTE buffer[4096];
        SYMBOL_INFO* sym_info = (SYMBOL_INFO*)buffer;
        sym_info->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym_info->MaxNameLen = (4096 - sizeof(SYMBOL_INFO)) / sizeof(TCHAR) - 1;
        BOOL bGetSym = SymFromAddr(
            m_DumpData.m_hProcess, 
            sf.AddrPC.Offset, 
            &dwDisp64, 
            sym_info);

        if(bGetSym)
        {
            stack_frame.m_sSymbolName = CString(sym_info->Name, sym_info->NameLen);
            stack_frame.m_dw64OffsInSymbol = dwDisp64;
        }

        // Get source filename and line
        DWORD dwDisplacement;
        IMAGEHLP_LINE64 line;
        BOOL bGetLine = SymGetLineFromAddr64(
            m_DumpData.m_hProcess, 
            sf.AddrPC.Offset,
            &dwDisplacement,
            &line);

        if(bGetLine)
        {
            stack_frame.m_sSrcFileName = line.FileName;
            stack_frame.m_nSrcLineNumber = line.LineNumber;
			stack_frame.m_dwOffsInLine = dwDisplacement;
        }

        m_DumpData.m_Threads[nThreadIndex].m_StackTrace.push_back(stack_frame);
    }


    CString sStackTrace;
    UINT i;
    for(i=0; i<m_DumpData.m_Threads[nThreadIndex].m_StackTrace.size(); i++)
    {
        MdmpStackFrame& frame = m_DumpData.m_Threads[nThreadIndex].m_StackTrace[i];

        if(frame.m_sSymbolName.IsEmpty())
            continue;

        CString sModuleName;
        CString sAddrPCOffset;
        CString sSymbolName;            
        CString sOffsInSymbol;
        CString sSourceFile;
        CString sSourceLine;

        if(frame.m_nModuleRowID>=0)
        {
            sModuleName = m_DumpData.m_Modules[frame.m_nModuleRowID].m_sModuleName;
        }           

        sSymbolName = frame.m_sSymbolName;
        sAddrPCOffset.Format(_T("0x%I64x"), frame.m_dwAddrPCOffset);
        sSourceFile = frame.m_sSrcFileName;
        sSourceLine.Format(_T("%d"), frame.m_nSrcLineNumber);
        sOffsInSymbol.Format(_T("0x%I64x"), frame.m_dw64OffsInSymbol);

        CString str;
        str = sModuleName;
        if(!str.IsEmpty())
            str += _T("!");

        if(sSymbolName.IsEmpty())
            str += sAddrPCOffset;  
        else
        {
            str += sSymbolName;
            str += _T("+");
            str += sOffsInSymbol;
        }

        if(!sSourceFile.IsEmpty())
        {
            int pos = sSourceFile.ReverseFind('\\');
            if(pos>=0)
                sSourceFile = sSourceFile.Mid(pos+1);
            str += _T(" [ ");
            str += sSourceFile;
            str += _T(": ");
            str += sSourceLine;
            str += _T(" ] ");
        } 

        sStackTrace += str; 
        sStackTrace += _T("\n");
    }

    if(!sStackTrace.IsEmpty())
    {
        LPCSTR szStackTrace = CT2A(sStackTrace, CP_UTF8);
        MD5 md5;
        MD5_CTX md5_ctx;
        unsigned char md5_hash[16];
        md5.MD5Init(&md5_ctx);  
        md5.MD5Update(&md5_ctx, (unsigned char*)szStackTrace, (unsigned int)strlen(szStackTrace));  
        md5.MD5Final(md5_hash, &md5_ctx);

        for(i=0; i<16; i++)
        {
            CString number;
            number.Format(_T("%02x"), md5_hash[i]);
            m_DumpData.m_Threads[nThreadIndex].m_sStackTraceMD5 += number;
        }
    }

    m_DumpData.m_Threads[nThreadIndex].m_bStackWalk = TRUE;


    return 0;
}

// This callback function is used by StackWalk64. It provides access to 
// ranges of memory stored in minidump file
BOOL CALLBACK CMiniDumpReaderCallback::ReadProcessMemoryProc64(
                                      HANDLE hProcess,
                                      DWORD64 lpBaseAddress,
                                      PVOID lpBuffer,
                                      DWORD nSize,
                                      LPDWORD lpNumberOfBytesRead)
{
    *lpNumberOfBytesRead = 0;

    // Validate input parameters
    if(hProcess!=g_pMiniDumpReader->m_DumpData.m_hProcess ||
        lpBaseAddress==NULL ||
        lpBuffer==NULL ||
        nSize==0)
    {
        // Invalid parameter
        return FALSE;
    }

    ULONG i;
    for(i=0; i<g_pMiniDumpReader->m_DumpData.m_MemRanges.size(); i++)
    {
        MdmpMemRange& mr = g_pMiniDumpReader->m_DumpData.m_MemRanges[i];
        if(lpBaseAddress>=mr.m_u64StartOfMemoryRange &&
            lpBaseAddress<mr.m_u64StartOfMemoryRange+mr.m_uDataSize)
        {
            DWORD64 dwOffs = lpBaseAddress-mr.m_u64StartOfMemoryRange;

            LONG64 lBytesRead = 0;

            if(mr.m_uDataSize-dwOffs>nSize)
                lBytesRead = nSize;
            else
                lBytesRead = mr.m_uDataSize-dwOffs;

            if(lBytesRead<=0 || nSize<lBytesRead)
                return FALSE;

            *lpNumberOfBytesRead = (DWORD)lBytesRead;
            memcpy(lpBuffer, (LPBYTE)mr.m_pStartPtr+dwOffs, (size_t)lBytesRead);

            return TRUE;
        }
    }

    return FALSE;
}

// This callback function is used by StackWalk64. It provides access to 
// function table stored in minidump file
PVOID CALLBACK CMiniDumpReaderCallback::FunctionTableAccessProc64(
    HANDLE hProcess,
    DWORD64 AddrBase)
{   
    return SymFunctionTableAccess64(hProcess, AddrBase);
}

// This callback function is used by StackWalk64. It provides access to 
// module list stored in minidump file
DWORD64 CALLBACK CMiniDumpReaderCallback::GetModuleBaseProc64(
                                     HANDLE hProcess,
                                     DWORD64 Address)
{  
    return SymGetModuleBase64(hProcess, Address);
}

} // namespace StackDumper