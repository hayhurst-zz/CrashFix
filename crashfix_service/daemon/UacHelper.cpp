#include "stdafx.h"

#include "UacHelper.h"

#include <taskschd.h>
#include <shellapi.h>
#include <comdef.h>

#pragma comment(lib, "taskschd.lib")

#define SAFERELEASE(p) {if (p) {(p)->Release(); p = NULL;}}

BOOL UacHelper::CommonShellExec(
				LPCTSTR pszVerb, 
				LPCTSTR pszPath, 
				LPCTSTR pszParameters,
				LPCTSTR pszDirectory,
				DWORD* pnPid)
{
	if (pnPid)
		*pnPid = 0;

	SHELLEXECUTEINFO shex;
	memset( &shex, 0, sizeof( shex) );

	shex.cbSize			= sizeof( SHELLEXECUTEINFO ); 
	shex.fMask			= SEE_MASK_NOCLOSEPROCESS;
	shex.hwnd			= NULL;
	shex.lpVerb			= pszVerb; 
	shex.lpFile			= pszPath; 
	shex.lpParameters	= pszParameters; 
	shex.lpDirectory	= pszDirectory; 
	shex.nShow			= SW_NORMAL; 
 
	BOOL ret = ::ShellExecuteEx( &shex );
	if (pnPid && shex.hProcess)
	{
		*pnPid = GetProcessId(shex.hProcess);
	}

	return ret;
}

BOOL UacHelper::IsVistaOrLater()
{
	OSVERSIONINFO osver;
	ZeroMemory(&osver, sizeof(OSVERSIONINFO));
	osver.dwOSVersionInfoSize = sizeof( OSVERSIONINFO );
	
	if (	::GetVersionEx( &osver ) && 
			osver.dwPlatformId == VER_PLATFORM_WIN32_NT && 
			(osver.dwMajorVersion >= 6 ) )
		return TRUE;

	return FALSE;
}

BOOL UacHelper::IsWow64()
{
    BOOL bIsWow64 = FALSE;

	typedef BOOL(WINAPI *LPFN_ISWOW64PROCESS) (HANDLE hProcess, PBOOL Wow64Process);

	static LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS)::GetProcAddress(::GetModuleHandle(_T("kernel32")), "IsWow64Process");

    if (NULL != fnIsWow64Process)
    {
		fnIsWow64Process(::GetCurrentProcess(), &bIsWow64);
    }

    return bIsWow64;
}

bool UacHelper::IsElevated()
{
	if (!IsVistaOrLater())
		return false;

	HRESULT hResult = E_FAIL; // assume an error occurred
	HANDLE hToken	= NULL;

	if ( !::OpenProcessToken( 
				::GetCurrentProcess(), 
				TOKEN_QUERY, 
				&hToken ) )
	{
		return false;
	}

	TOKEN_ELEVATION te = { 0 };
	DWORD dwReturnLength = 0;

	if ( ::GetTokenInformation(
				hToken,
				(TOKEN_INFORMATION_CLASS)TokenElevation,
				&te,
				sizeof( te ),
				&dwReturnLength ) )
	{
		assert( dwReturnLength == sizeof( te ) );

		hResult = te.TokenIsElevated ? S_OK : S_FALSE; 
	}

	::CloseHandle( hToken );

	return (hResult == S_OK);
}

BOOL UacHelper::RunElevated( 
	__in		LPCTSTR pszPath, 
	__in_opt	LPCTSTR pszParameters,
	__in_opt	LPCTSTR pszDirectory,
	__in_opt	DWORD* pnPid)
{
	return UacHelper::CommonShellExec(_T("runas"), pszPath, pszParameters, pszDirectory, pnPid);
}

BOOL UacHelper::RunNonElevated(
	__in		LPCTSTR pszPath, 
	__in_opt	LPCTSTR pszParameters,
	__in_opt	LPCTSTR pszDirectory,
	__in_opt	DWORD* pnPid)
{
	assert( pszPath && *pszPath );	// other args are optional

	if ( !IsElevated() ) 
	{
		// if the current process is not elevated, we can use ShellExecuteAs directly! 
	
		return UacHelper::CommonShellExec(
							NULL, 
							pszPath, 
							pszParameters, 
							pszDirectory,
							pnPid);
	}

	HRESULT hr = CreateMyTask(L"CrashFix Launch NonElevated", pszPath, pszParameters, pszDirectory, pnPid);
	
	return SUCCEEDED(hr);
}


HRESULT UacHelper::CreateMyTask(LPCTSTR pszTaskName, LPCTSTR pszExecutablePath, LPCTSTR pszParameters, LPCTSTR pszDirectory, DWORD* pnPid)
{
	if (pnPid)
		*pnPid = 0;

	if (!pszTaskName)
		return E_INVALIDARG;

	if (!pszExecutablePath)
		return E_INVALIDARG;
	wstring wstrExecutablePath(pszExecutablePath);
	
	wstring wstrParameters;
	if (pszParameters)
		wstrParameters = pszParameters;

	wstring wstrDirectory;
	if (pszDirectory)
		wstrDirectory = pszDirectory;

	//  ------------------------------------------------------
	//  Initialize COM.
	TASK_STATE taskState = TASK_STATE_UNKNOWN;
	int i = 0;
	HRESULT hr = S_OK;
	DWORD nPid = 0;

	do 
	{
		hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		if (FAILED(hr))
		{
			//printf("\nCoInitializeEx failed: %x", hr);
			break;
		}

		//  Set general COM security levels.
		hr = CoInitializeSecurity(
			NULL,
			-1,
			NULL,
			NULL,
			RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
			RPC_C_IMP_LEVEL_IMPERSONATE,
			NULL,
			0,
			NULL);

		if (FAILED(hr))
		{
			//printf("\nCoInitializeSecurity failed: %x", hr);
			break;
		}

		//  ------------------------------------------------------
		//  Create an instance of the Task Service. 
		CComQIPtr<ITaskService> pService;
		hr = CoCreateInstance(CLSID_TaskScheduler,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_ITaskService,
			(void**)&pService);
		if (FAILED(hr))
		{
			//printf("Failed to CoCreate an instance of the TaskService class: %x", hr);
			break;
		}

		//  Connect to the task service.
		hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
		if (FAILED(hr))
		{
			//printf("ITaskService::Connect failed: %x", hr);
			break;
		}

		//  ------------------------------------------------------
		//  Get the pointer to the root task folder.  This folder will hold the
		//  new task that is registered.
		CComQIPtr<ITaskFolder> pRootFolder;
		hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
		if (FAILED(hr))
		{
			//printf("Cannot get Root Folder pointer: %x", hr);
			break;
		}

		//  Check if the same task already exists. If the same task exists, remove it.
		hr = pRootFolder->DeleteTask(_bstr_t(pszTaskName), 0);

		//  Create the task builder object to create the task.
		CComQIPtr<ITaskDefinition> pTask;
		hr = pService->NewTask(0, &pTask);
		if (FAILED(hr))
		{
			//printf("Failed to CoCreate an instance of the TaskService class: %x", hr);
			break;
		}

		//  ------------------------------------------------------
		//  Get the trigger collection to insert the registration trigger.
		CComQIPtr<ITriggerCollection> pTriggerCollection;
		hr = pTask->get_Triggers(&pTriggerCollection);
		if (FAILED(hr))
		{
			//printf("\nCannot get trigger collection: %x", hr);
			break;
		}

		//  Add the registration trigger to the task immediately
		CComQIPtr<ITrigger> pTrigger;
		hr = pTriggerCollection->Create(TASK_TRIGGER_REGISTRATION, &pTrigger);
		if (FAILED(hr))
		{
			//printf("\nCannot add registration trigger to the Task %x", hr);
			break;
		}

		//  ------------------------------------------------------
		//  Add an Action to the task.     

		//  Get the task action collection pointer.
		CComQIPtr<IActionCollection> pActionCollection;
		hr = pTask->get_Actions(&pActionCollection);
		if (FAILED(hr))
		{
			//printf("\nCannot get Task collection pointer: %x", hr);
			break;
		}

		//  Create the action, specifying that it is an executable action.
		CComQIPtr<IAction> pAction;
		hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
		if (FAILED(hr))
		{
			//printf("\npActionCollection->Create failed: %x", hr);
			break;
		}

		CComQIPtr<IExecAction>pExecAction;
		hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
		if (FAILED(hr))
		{
			//printf("\npAction->QueryInterface failed: %x", hr);
			break;
		}

		//  Set the path of the executable to the user supplied executable.
		hr = pExecAction->put_Path(_bstr_t(wstrExecutablePath.c_str()));
		if (FAILED(hr))
		{
			//printf("\nCannot set path of executable: %x", hr);
			break;
		}

		hr = pExecAction->put_Arguments(_bstr_t(wstrParameters.c_str()));
		if (FAILED(hr))
		{
			//printf("\nCannot set arguments of executable: %x", hr);
			break;
		}

		hr = pExecAction->put_WorkingDirectory(_bstr_t(wstrDirectory.c_str()));
		if (FAILED(hr))
		{
			//printf("\nCannot set working directory of executable: %x", hr);
			break;
		}

		//  ------------------------------------------------------
		//  Save the task in the root folder.
		CComQIPtr<IRegisteredTask> pRegisteredTask;
		hr = pRootFolder->RegisterTaskDefinition(
			_bstr_t(pszTaskName),
			pTask,
			TASK_CREATE,
			_variant_t(_bstr_t(L"S-1-5-32-545")),//Well Known SID for \\Builtin\Users group
			_variant_t(),
			TASK_LOGON_GROUP,
			_variant_t(L""),
			&pRegisteredTask);
		if (FAILED(hr))
		{
			//printf("\nError saving the Task : %x", hr);
			break;
		}
		//printf("\n Success! Task successfully registered. ");

		for (i = 0; i < 100; i++)//give 10 seconds for the task to start
		{
			pRegisteredTask->get_State(&taskState);
			if (taskState == TASK_STATE_RUNNING)
			{
				//printf("\nTask is running\n");
				if(pnPid)
				{
					CComQIPtr<IRunningTaskCollection> pRunningTaskCollection;
					if (FAILED(pRegisteredTask->GetInstances(0, &pRunningTaskCollection)) || !pRunningTaskCollection)
						break;

					long count = 0;
					if (FAILED(pRunningTaskCollection->get_Count(&count)) || count == 0)
						break;

					CComQIPtr<IRunningTask> pRunningTask;
					if (FAILED(pRunningTaskCollection->get_Item(_variant_t(1), &pRunningTask)) || !pRunningTask)
						break;

					hr = pRunningTask->get_EnginePID(&nPid);
				}
				break;
			}
			Sleep(100);
		}
		if (i >= 100) 
		{
			//printf("Task didn't start\n");
		}

		//Delete the task when done
		hr = pRootFolder->DeleteTask(
			_bstr_t(pszTaskName),
			NULL);
		if (FAILED(hr))
		{
			//printf("\nError deleting the Task : %x", hr);
			break;
		}

		//printf("\n Success! Task successfully deleted. ");
	} while (false);

	CoUninitialize();

	if (pnPid)
		*pnPid = nPid;
	
	return nPid > 0 ? S_OK : E_FAIL;
}
