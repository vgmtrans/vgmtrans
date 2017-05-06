#include "pch.h"
#include "WinUICallbacks.h"
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

using namespace std;

WinUICallbacks winroot;

extern HANDLE killProgramSem;

WinUICallbacks::WinUICallbacks(void)
: selectedItem(NULL), selectedColl(NULL), loadedColl(NULL), bClosingVGMFile(false), bExiting(false)
{
}

WinUICallbacks::~WinUICallbacks(void)
{
}

void WinUICallbacks::SelectItem(VGMItem* item)
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

void WinUICallbacks::SelectColl(VGMColl* coll)
{
	if (bClosingVGMFile)
		return;
	selectedColl = coll;

	pMainFrame->SelectColl(coll);
}


void WinUICallbacks::Play(void)
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

void WinUICallbacks::Pause(void)
{
//	if (pMainFrame->UIGetState(ID_STOP) ==  CMainFrame::UPDUI_DISABLED)	//if the stop button is disabled, return
//		return;
	musicplayer.Pause();
//	pMainFrame->UIEnable(ID_STOP, 0);
}

void WinUICallbacks::Stop(void)
{
	if (pMainFrame->UIGetState(ID_STOP) ==  CMainFrame::UPDUI_DISABLED)	//if the stop button is disabled, return
		return;
	musicplayer.Stop();
	pMainFrame->UIEnable(ID_STOP, 0);
	pMainFrame->UIEnable(ID_PAUSE, 0);
}

void WinUICallbacks::UI_PreExit()
{
	bExiting = true;

	musicplayer.Stop();
	WaitForSingleObject(killProgramSem, INFINITE);
}

void WinUICallbacks::UI_Exit()
{
	pMainFrame->CloseUpShop();		//this occurs after Reset() is called in Root:Exit().  We can't be deleting items from our
									//interface after the interface has closed down.  we must do that before
}

void WinUICallbacks::UI_AddRawFile(RawFile* newFile)
{
	rawFileListView.AddFile(newFile);
}

void WinUICallbacks::UI_CloseRawFile(RawFile* targFile)
{
	rawFileListView.RemoveFile(targFile);
}

void WinUICallbacks::UI_OnBeginScan()
{
	scanDlg = new CScanDlg();
	scanDlg->Create(pMainFrame->m_hWnd);
	scanDlg->ShowWindow(SW_SHOW);
}

void WinUICallbacks::UI_OnEndScan()
{
	scanDlg->SendMessage(WM_CLOSE);
}

void WinUICallbacks::UI_AddVGMFile(VGMFile* theFile)
{
	theVGMFileListView.AddFile(theFile);
}

void WinUICallbacks::UI_AddVGMColl(VGMColl* theColl)
{
	theVGMCollListView.AddColl(theColl);
}

void WinUICallbacks::UI_AddLogItem(LogItem* theLog)
{
	theLogListView.AddLogItem(theLog);
}

void WinUICallbacks::UI_RemoveVGMFile(VGMFile* targFile)
{
	pMainFrame->OnRemoveVGMFile(targFile);
	theVGMFileListView.RemoveFile(targFile);
}

void WinUICallbacks::UI_RemoveVGMColl(VGMColl* targColl)
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

void WinUICallbacks::UI_BeginRemoveVGMFiles()
{
	theVGMFileListView.ShowWindow(false);
	bClosingVGMFile = true;
}

void WinUICallbacks::UI_EndRemoveVGMFiles()
{
	theVGMFileListView.ShowWindow(true);
	bClosingVGMFile = false;
}

void WinUICallbacks::UI_AddItem(VGMItem* item, VGMItem* parent, const wstring& itemName, VOID* UI_specific)
{
	CItemTreeView* itemView = (CItemTreeView*)UI_specific;
	itemView->AddItem(item, parent, itemName);
//	pMainFrame->itemViewMap[vgmfile]->AddItem(vgmfile, item, parent, itemName);
}

void WinUICallbacks::UI_AddItemSet(VGMFile* vgmfile, vector<ItemSet>* vItemSets)
{
//	pMainFrame->itemViewMap[vgmfile]->AddItemSet(vgmfile, vItemSets);
}

wstring WinUICallbacks::UI_GetOpenFilePath(const wstring& suggestedFilename, const wstring& extension)
{
	CFileDialog dlgFile(TRUE, extension.c_str(), suggestedFilename.c_str(), OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER);

	if (dlgFile.DoModal() != IDOK)
		return L"";

	//dlgFile.GetFilePath(/*strFilePath.GetBufferSetLength( MAX_PATH )*/strFilePath, MAX_PATH);
	//strFilePath.ReleaseBuffer();
	return dlgFile.m_szFileName;
}

wstring WinUICallbacks::UI_GetSaveFilePath(const wstring& suggestedFilename, const wstring& extension)
{
	CFileDialog dlgFile(FALSE, extension.c_str(), suggestedFilename.c_str(), OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_EXPLORER);

	if (dlgFile.DoModal() != IDOK)
		return L"";

	//dlgFile.GetFilePath(/*strFilePath.GetBufferSetLength( MAX_PATH )*/strFilePath, MAX_PATH);
	//strFilePath.ReleaseBuffer();
	return dlgFile.m_szFileName;
}


wstring WinUICallbacks::UI_GetSaveDirPath(const wstring& suggestedDir)
{
	wstring myStr;
	GetFolder(myStr, L"Save to Folder");
	return myStr;
}




//bool UI_WriteBufferToFile(string filename, unsigned char* buf, unsigned long size)
//{
//	
bool WinUICallbacks::GetFolder(std::wstring& folderpath, const wstring& szCaption, HWND hOwner)
{
	bool retVal = false;

	// The BROWSEINFO struct tells the shell 
	// how it should display the dialog.
	BROWSEINFO bi;
	memset(&bi, 0, sizeof(bi));

	bi.ulFlags   = BIF_USENEWUI;
	bi.hwndOwner = hOwner;
	bi.lpszTitle = szCaption.c_str();

	// must call this if using BIF_USENEWUI
	::OleInitialize(NULL);

	// Show the dialog and get the itemIDList for the selected folder.
	LPITEMIDLIST pIDL = ::SHBrowseForFolder(&bi);

	if(pIDL != NULL)
	{
		// Create a buffer to store the path, then get the path.
		wchar_t buffer[_MAX_PATH] = {'\0'};
		if(::SHGetPathFromIDList(pIDL, buffer) != 0)
		{
			// Set the string value.
			folderpath = buffer;
			retVal = true;
		}		

		// free the item id list
		CoTaskMemFree(pIDL);
	}

	::OleUninitialize();

	return retVal;
}//}