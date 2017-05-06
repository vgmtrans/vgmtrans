#pragma once
#include "UICallbacks.h"

class VGMItem;
class CScanDlg;

class WinUICallbacks :
	public UICallbacks
{
public:
	WinUICallbacks(void);
public:
	virtual ~WinUICallbacks(void);

	void SelectItem(VGMItem* item);
	void SelectColl(VGMColl* coll);
	void Play(void);
	void Pause(void);
	void Stop(void);
	inline bool AreWeExiting() { return bExiting; }

	virtual void UI_PreExit();
	virtual void UI_Exit();
	virtual void UI_AddRawFile(RawFile* newFile);
	virtual void UI_CloseRawFile(RawFile* targFile);

	virtual void UI_OnBeginScan();
	virtual void UI_OnEndScan();
	virtual void UI_AddVGMFile(VGMFile* theFile);
	virtual void UI_AddVGMColl(VGMColl* theColl);
	virtual void UI_AddLogItem(LogItem* theLog);
	virtual void UI_RemoveVGMFile(VGMFile* targFile);
	virtual void UI_RemoveVGMColl(VGMColl* targColl);
	virtual void UI_BeginRemoveVGMFiles();
	virtual void UI_EndRemoveVGMFiles();
	virtual void UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName, VOID* UI_specific);
	virtual void UI_AddItemSet(VGMFile* file, std::vector<ItemSet>* itemset);
	virtual std::wstring UI_GetOpenFilePath(const std::wstring& suggestedFilename = L"", const std::wstring& extension = L"");
	virtual std::wstring UI_GetSaveFilePath(const std::wstring& suggestedFilename, const std::wstring& extension = L"");
	virtual std::wstring UI_GetSaveDirPath(const std::wstring& suggestedDir = L"");
	bool GetFolder(std::wstring& folderpath, const std::wstring& szCaption = NULL, HWND hOwner = NULL);

	//virtual bool UI_WriteBufferToFile(const char* filepath, const char* buf, unsigned long size);

	//CRawFileTreeView* pFileTreeView;
	//CVGMFileTreeView* pVGMFilesView;

private:
	CScanDlg* scanDlg;
	VGMItem* selectedItem;
	VGMColl* selectedColl;
	VGMColl* loadedColl;
	bool bClosingVGMFile;
	bool bExiting;
};


extern WinUICallbacks winroot;