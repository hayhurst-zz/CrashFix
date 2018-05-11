#pragma once

class UacHelper
{
public:
	static BOOL RunElevated(LPCTSTR pszPath, LPCTSTR pszParameters = NULL, LPCTSTR pszDirectory = NULL, DWORD* pnPid = NULL);
	static BOOL RunNonElevated(LPCTSTR pszPath, LPCTSTR pszParameters = NULL, LPCTSTR pszDirectory = NULL, DWORD* pnPid = NULL);

protected:
	static BOOL IsVistaOrLater();
	static BOOL IsWow64();
	static bool IsElevated(void);
	static HRESULT CreateMyTask(LPCTSTR pszTaskName, LPCTSTR pszExecutablePath, LPCTSTR pszParameters, LPCTSTR pszDirectory, DWORD* pnPid);
	static BOOL CommonShellExec(LPCTSTR pszVerb, LPCTSTR pszPath, LPCTSTR pszParameters, LPCTSTR pszDirectory, DWORD* pnPid);
};

