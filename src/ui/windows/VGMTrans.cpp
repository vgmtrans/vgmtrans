// VGMTrans.cpp : main source file for VGMTrans.exe
//

#include "stdafx.h"

//#include <vld.h>

#include "resource.h"

#include "aboutdlg.h"
#include "MainFrm.h"
#include "MediaThread.h"
#include "Root.h"

CAppModule _Module;
bool g_bXPOrLater;

CMainFrame* pMainFrame;

int Run(LPTSTR lpstrCmdLine, int nCmdShow = SW_SHOWDEFAULT)
{
	nCmdShow; //avoid level 4 warning

	int argc;
	lpstrCmdLine = GetCommandLine(); // wWinMain lpszCmdLine does not work well (do not know why)
	LPWSTR* argv = CommandLineToArgvW(lpstrCmdLine, &argc);

	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	CMainFrame wndMain;

	if(wndMain.CreateEx() == NULL)
	{
		ATLTRACE(_T("Main window creation failed!\n"));
		LocalFree((HLOCAL)argv);
		return 0;
	}

	pMainFrame = &wndMain;

//	wndMain.ShowWindow(nCmdShow);
	mediaThread.Init();
	//mediaThread.Run();

	for (int argi = 1; argi < argc; argi++)
	{
		pRoot->OpenRawFile(argv[argi]);
	}

	int nRet = theLoop.Run();

	//mediaThread.Terminate();
	_Module.RemoveMessageLoop();

	LocalFree((HLOCAL)argv);
	return nRet;
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow)
{
#ifdef _DEBUG
	::_CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

#ifdef _MSC_VER
	setlocale(LC_ALL, ".ACP");
#endif

	HRESULT hRes = ::CoInitialize(NULL);
// If you are running on NT 4.0 or higher you can use the following call instead to 
// make the EXE free threaded. This means that calls come in on a random RPC thread.
//	HRESULT hRes = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
	ATLASSERT(SUCCEEDED(hRes));

	// this resolves ATL window thunking problem when Microsoft Layer for Unicode (MSLU) is used
	::DefWindowProc(NULL, 0, 0, 0L);

	OleInitialize(NULL);
	AtlInitCommonControls(ICC_COOL_CLASSES | ICC_BAR_CLASSES);	// add flags to support other controls

	hRes = _Module.Init(NULL, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

	AtlAxWinInit();

	
	

	//g_bXPOrLater = _winmajor > 5 || (_winmajor == 5 && _winminor > 0);
	OSVERSIONINFOEX osvi;
	ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	g_bXPOrLater = osvi.dwMajorVersion > 5 || (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion > 0);
	

	int nRet = Run(lpstrCmdLine, nCmdShow);

	_Module.Term();
	::CoUninitialize();

	return nRet;
}
