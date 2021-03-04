/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "pch.h"
#include "WinVGMRoot.h"
#include "VGMFileTreeView.h"
#include "VGMFileListView.h"
#include "VGMCollListView.h"
#include "RawFileListView.h"
#include "LogListView.h"
#include "ItemTreeView.h"
#include "MusicPlayer.h"
#include "MainFrm.h"
#include "ScanDlg.h"
#include "DLSFile.h"
#include "osdepend.h"

WinVGMRoot winroot;

extern HANDLE killProgramSem;

WinVGMRoot::WinVGMRoot(void)
: selectedItem(NULL), selectedColl(NULL), loadedColl(NULL), bClosingVGMFile(false), bExiting(false)
{
}

WinVGMRoot::~WinVGMRoot(void)
{
}

void WinVGMRoot::SelectItem(VGMItem* item)
{
	if (bClosingVGMFile)//AreWeExiting())
		return;
	selectedItem = item;
	if (item->GetType() == ITEMTYPE_VGMFILE)
	{
	//	if (((VGMFile*)item)->GetFileType() == FILETYPE_INSTRSET)
	//	{
	//		DLSFile dls;
	//		((VGMInstrSet*)item)->CreateDLSFile(dls);
	//		musicplayer.ChangeDLS(&dls);
	//	}
	}

	pMainFrame->SelectItem(item);
}

void WinVGMRoot::SelectColl(VGMColl* coll)
{
	if (bClosingVGMFile)
		return;
	selectedColl = coll;

	pMainFrame->SelectColl(coll);
}


void WinVGMRoot::Play(void)
{
	if (pMainFrame->UIGetState(ID_PLAY) ==  CMainFrame::UPDUI_DISABLED)	//if the play button is disabled, return
		return;
	if (selectedColl)
	{
		VGMSeq * seq = selectedColl->GetSeq();
		if (seq == NULL)
		{
			return;
		}
		if (loadedColl != selectedColl)
		{
			DLSFile dls;
			if (!selectedColl->CreateDLSFile(dls))
			{
				Alert(L"Unable to create DLS. It is likely corrupted.");
				return;
			}
			musicplayer.ChangeDLS(&dls);
			loadedColl = selectedColl;
		}
		musicplayer.Play((VGMItem*)seq, 0);
	}
	else	
		musicplayer.Play(selectedItem, 0);

	pMainFrame->UIEnable(ID_STOP, 1);
	pMainFrame->UIEnable(ID_PAUSE, 1);
}

void WinVGMRoot::Pause(void)
{
//	if (pMainFrame->UIGetState(ID_STOP) ==  CMainFrame::UPDUI_DISABLED)	//if the stop button is disabled, return
//		return;
	musicplayer.Pause();
//	pMainFrame->UIEnable(ID_STOP, 0);
}

void WinVGMRoot::Stop(void)
{
	if (pMainFrame->UIGetState(ID_STOP) ==  CMainFrame::UPDUI_DISABLED)	//if the stop button is disabled, return
		return;
	musicplayer.Stop();
	pMainFrame->UIEnable(ID_STOP, 0);
	pMainFrame->UIEnable(ID_PAUSE, 0);
}




//void WinVGMRoot::SetWndPtrs(CMainFrame* mainFrm)
//{
//	pFileTreeView = &mainFrm->m_RawFileTreeView;
//	pVGMFilesView = &mainFrm->m_VGMFilesView;
//}

void WinVGMRoot::UI_SetRootPtr(VGMRoot** theRoot)
{
	*theRoot = &winroot;
}

void WinVGMRoot::UI_PreExit()
{
	bExiting = true;

	musicplayer.Stop();
	WaitForSingleObject(killProgramSem, INFINITE);
}

void WinVGMRoot::UI_Exit()
{
	pMainFrame->CloseUpShop();		//this occurs after Reset() is called in Root:Exit().  We can't be deleting items from our
									//interface after the interface has closed down.  we must do that before
}

void WinVGMRoot::UI_AddRawFile(RawFile* newFile)
{
	rawFileListView.AddFile(newFile);
}

void WinVGMRoot::UI_CloseRawFile(RawFile* targFile)
{
	rawFileListView.RemoveFile(targFile);
}

void WinVGMRoot::UI_OnBeginScan()
{
	scanDlg = new CScanDlg();
	scanDlg->Create(pMainFrame->m_hWnd);
	scanDlg->ShowWindow(SW_SHOW);
}

void WinVGMRoot::UI_SetScanInfo()
{
}

void WinVGMRoot::UI_OnEndScan()
{
	scanDlg->SendMessage(WM_CLOSE);
}

void WinVGMRoot::UI_AddVGMFile(VGMFile* theFile)
{
	pMainFrame->OnAddVGMFile(theFile);
	VGMRoot::UI_AddVGMFile(theFile);
}

void WinVGMRoot::UI_AddVGMSeq(VGMSeq* theSeq)
{
	theVGMFileListView.AddFile((VGMFile*)theSeq);
}

void WinVGMRoot::UI_AddVGMInstrSet(VGMInstrSet* theInstrSet)
{
	theVGMFileListView.AddFile((VGMFile*)theInstrSet);
}

void WinVGMRoot::UI_AddVGMSampColl(VGMSampColl* theSampColl)
{
	theVGMFileListView.AddFile((VGMFile*)theSampColl);
}

void WinVGMRoot::UI_AddVGMMisc(VGMMiscFile* theMiscFile)
{
	theVGMFileListView.AddFile((VGMFile*)theMiscFile);
}

void WinVGMRoot::UI_AddVGMColl(VGMColl* theColl)
{
	theVGMCollListView.AddColl(theColl);
}

void WinVGMRoot::UI_AddLogItem(LogItem* theLog)
{
	theLogListView.AddLogItem(theLog);
}

void WinVGMRoot::UI_RemoveVGMFile(VGMFile* targFile)
{
	pMainFrame->OnRemoveVGMFile(targFile);
	theVGMFileListView.RemoveFile(targFile);
}

void WinVGMRoot::UI_RemoveVGMColl(VGMColl* targColl)
{
	if (targColl == loadedColl)		//then we might be playing the collection up for removal
	{
		pMainFrame->UIEnable(ID_PLAY, 0);
		pMainFrame->UIEnable(ID_PAUSE, 0);
		Stop();						//so stop playback
		loadedColl = NULL;
	}
	if (targColl == selectedColl)
	{
		pMainFrame->UIEnable(ID_PLAY, 0);
		theCollDialog.Clear();
		selectedColl = NULL;
	}
	theVGMCollListView.RemoveColl(targColl);
}

void WinVGMRoot::UI_BeginRemoveVGMFiles()
{
	theVGMFileListView.ShowWindow(false);
	bClosingVGMFile = true;
}

void WinVGMRoot::UI_EndRemoveVGMFiles()
{
	theVGMFileListView.ShowWindow(true);
	bClosingVGMFile = false;
}

void WinVGMRoot::UI_AddItem(VGMItem* item, VGMItem* parent, const wstring& itemName, VOID* UI_specific)
{
	CItemTreeView* itemView = (CItemTreeView*)UI_specific;
	itemView->AddItem(item, parent, itemName);
//	pMainFrame->itemViewMap[vgmfile]->AddItem(vgmfile, item, parent, itemName);
}

void WinVGMRoot::UI_AddItemSet(VGMFile* vgmfile, vector<ItemSet>* vItemSets)
{
//	pMainFrame->itemViewMap[vgmfile]->AddItemSet(vgmfile, vItemSets);
}

wstring WinVGMRoot::UI_GetOpenFilePath(const wstring& suggestedFilename, const wstring& extension) {
  HRESULT hr = S_OK;

  /* Create a new common open file dialog */
  IFileOpenDialog* pfd = nullptr;
  hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
  if (!SUCCEEDED(hr)) {
    return {};
  }

  DWORD dwOptions;
  pfd->GetOptions(&dwOptions);
  pfd->SetOptions(dwOptions | FOS_FILEMUSTEXIST);
  pfd->SetTitle(L"Select file to analyze");

  /* We don't have the parent window pointer here.. */
  hr = pfd->Show(nullptr);
  if (!SUCCEEDED(hr)) {
    return {};
  }

  /* Get the selection */
  IShellItem* psiResult = nullptr;
  pfd->GetResult(&psiResult);
  PWSTR pszPath = nullptr;
  std::wstring directory{};
  hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
  if (SUCCEEDED(hr)) {
    directory = std::wstring(pszPath);
    CoTaskMemFree(pszPath);
  }
  psiResult->Release();

  pfd->Release();

  return directory;
}

wstring WinVGMRoot::UI_GetSaveFilePath(const wstring& suggestedFilename, const wstring& extension) {
  HRESULT hr = S_OK;

  /* Create a new common save file dialog */
  IFileSaveDialog* pfd = nullptr;
  hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
  if (!SUCCEEDED(hr)) {
    return {};
  }

  DWORD dwOptions;
  pfd->GetOptions(&dwOptions);
  pfd->SetOptions(dwOptions);
  pfd->SetTitle(L"Save file");
  pfd->SetFileName(suggestedFilename.c_str());
  pfd->SetDefaultExtension(extension.c_str());

  /* We don't have the parent window pointer here.. */
  hr = pfd->Show(nullptr);
  if (!SUCCEEDED(hr)) {
    return {};
  }

  /* Get the selection */
  IShellItem* psiResult = nullptr;
  pfd->GetResult(&psiResult);
  PWSTR pszPath = nullptr;
  std::wstring directory{};
  hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
  if (SUCCEEDED(hr)) {
    directory = std::wstring(pszPath);
    CoTaskMemFree(pszPath);
  }
  psiResult->Release();

  pfd->Release();

  return directory;
}

wstring WinVGMRoot::UI_GetSaveDirPath(const wstring& suggestedDir) {
  HRESULT hr = S_OK;

  /* Create a new common open file dialog */
  IFileOpenDialog* pfd = nullptr;
  hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
  if (!SUCCEEDED(hr)) {
    return {};
  }

  DWORD dwOptions;
  pfd->GetOptions(&dwOptions);
  /* Make the dialog a folder picker*/
  pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
  pfd->SetTitle(L"Select the export folder");

  /* We don't have the parent window pointer here.. */
  hr = pfd->Show(nullptr);
  if (!SUCCEEDED(hr)) {
    return {};
  }

  /* Get the selection */
  IShellItem* psiResult = nullptr;
  pfd->GetResult(&psiResult);
  PWSTR pszPath = nullptr;
  std::wstring directory{};
  hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
  if (SUCCEEDED(hr)) {
    directory = std::wstring(pszPath);
    CoTaskMemFree(pszPath);
  }
  psiResult->Release();

  pfd->Release();

  return directory;
}
