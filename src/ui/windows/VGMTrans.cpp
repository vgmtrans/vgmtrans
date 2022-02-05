/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "pch.h"

#include "resource.h"

#include "aboutdlg.h"
#include "MainFrm.h"
#include "Root.h"

CAppModule _Module;

CMainFrame* pMainFrame;

int Run(LPTSTR lpstrCmdLine, [[maybe_unused]] int nCmdShow = SW_SHOWDEFAULT) {
  int argc;
  lpstrCmdLine = GetCommandLine();  // wWinMain lpszCmdLine does not work well (do not know why)
  LPWSTR* argv = CommandLineToArgvW(lpstrCmdLine, &argc);

  CMessageLoop theLoop;
  _Module.AddMessageLoop(&theLoop);

  CMainFrame wndMain;

  if (wndMain.CreateEx() == NULL) {
    ATLTRACE(_T("Main window creation failed!\n"));
    LocalFree((HLOCAL)argv);
    return 0;
  }

  pMainFrame = &wndMain;

  for (int argi = 1; argi < argc; argi++) {
    pRoot->OpenRawFile(argv[argi]);
  }

  int nRet = theLoop.Run();
  _Module.RemoveMessageLoop();

  LocalFree((HLOCAL)argv);
  return nRet;
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine,
                     int nCmdShow) {
#ifdef _DEBUG
  ::_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

#ifdef _MSC_VER
  setlocale(LC_ALL, ".ACP");
#endif

  const HRESULT hCoInitializeResult = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
  ATLASSERT(SUCCEEDED(hCoInitializeResult) || hCoInitializeResult == RPC_E_CHANGED_MODE);

  AtlInitCommonControls(ICC_COOL_CLASSES | ICC_BAR_CLASSES);

  HRESULT hRes = _Module.Init(NULL, hInstance);
  ATLASSERT(SUCCEEDED(hRes));

  AtlAxWinInit();

  int nRet = Run(lpstrCmdLine, nCmdShow);

  _Module.Term();
  if (SUCCEEDED(hCoInitializeResult))
    ::CoUninitialize();

  return nRet;
}
