#include "stdafx.h"
#include "MyCoCreateInstance.h"

HRESULT __stdcall MyCoCreateInstance(
  LPCTSTR szDllName,
  IN REFCLSID rclsid,
  IUnknown* pUnkOuter,
  IN REFIID riid,
  OUT LPVOID FAR* ppv)
{
  HRESULT hr = REGDB_E_KEYMISSING;

  HMODULE hDll = ::LoadLibrary(szDllName);
  if (hDll == 0)
  {
	  HRESULT blah = GetLastError();
    return hr;
  }

  typedef HRESULT (__stdcall *pDllGetClassObject)(IN REFCLSID rclsid, 
                   IN REFIID riid, OUT LPVOID FAR* ppv);

  pDllGetClassObject GetClassObject = 
     (pDllGetClassObject)::GetProcAddress(hDll, "DllGetClassObject");
  if (GetClassObject == 0)
  {
    ::FreeLibrary(hDll);
    return hr;
  }

  IClassFactory *pIFactory;

  hr = GetClassObject(rclsid, IID_IClassFactory, (LPVOID *)&pIFactory);

  if (!SUCCEEDED(hr))
    return hr;

  hr = pIFactory->CreateInstance(pUnkOuter, riid, ppv);
  pIFactory->Release();

  return hr;
}