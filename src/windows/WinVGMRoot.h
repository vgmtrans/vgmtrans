#pragma once
#include "root.h"

class VGMItem;
class CScanDlg;

class WinVGMRoot :
	public VGMRoot
{
public:
	WinVGMRoot(void);
public:
	virtual ~WinVGMRoot(void);

	void SelectItem(VGMItem* item);
	void SelectColl(VGMColl* coll);
	void Play(void);
	void Pause(void);
	void Stop(void);
	inline bool AreWeExiting() { return bExiting; }

	virtual void UI_SetRootPtr(VGMRoot** theRoot);
	virtual void UI_PreExit();
	virtual void UI_Exit();
	virtual void UI_AddRawFile(RawFile* newFile);
	virtual void UI_CloseRawFile(RawFile* targFile);

	virtual void UI_OnBeginScan();
	virtual void UI_SetScanInfo();
	virtual void UI_OnEndScan();
	virtual void UI_AddVGMFile(VGMFile* theFile);
	virtual void UI_AddVGMSeq(VGMSeq* theSeq);
	virtual void UI_AddVGMInstrSet(VGMInstrSet* theInstrSet);
	virtual void UI_AddVGMSampColl(VGMSampColl* theSampColl);
	virtual void UI_AddVGMMisc(VGMMiscFile* theMiscFile);
	virtual void UI_AddVGMColl(VGMColl* theColl);
	virtual void UI_AddLogItem(LogItem* theLog);
	virtual void UI_RemoveVGMFile(VGMFile* targFile);
	virtual void UI_RemoveVGMColl(VGMColl* targColl);
	virtual void UI_BeginRemoveVGMFiles();
	virtual void UI_EndRemoveVGMFiles();
	//virtual void UI_RemoveVGMFileRange(VGMFile* first, VGMFile* last);
	virtual void UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName, VOID* UI_specific);
	virtual void UI_AddItemSet(VGMFile* file, std::vector<ItemSet>* itemset);
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


extern WinVGMRoot winroot;