#pragma once

HRESULT __stdcall MyCoCreateInstance(
  LPCTSTR szDllName,
  IN REFCLSID rclsid,
  IUnknown* pUnkOuter,
  IN REFIID riid,
  OUT LPVOID FAR* ppv);
