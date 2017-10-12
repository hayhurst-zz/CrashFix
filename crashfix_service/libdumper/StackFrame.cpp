#include "stdafx.h"
#include "StackFrame.h"

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
	buffer.flush(); // TODO
	return S_OK;
}

STDMETHODIMP_(ULONG) StackFrameDumperCallback::Release(THIS) {
	return 0;
}

STDMETHODIMP_(ULONG) StackFrameDumperCallback::AddRef(THIS) {
	return 1;
}

