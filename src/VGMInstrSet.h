#pragma once
#include "common.h"
#include "VGMFile.h"
#include "DLSFile.h"
#include "SF2File.h"
#include "Menu.h"

class VGMSampColl;
class VGMInstr;
class VGMRgn;
class VGMSamp;
class VGMRgnItem;
//class VGMArt;

// ***********
// VGMInstrSet
// ***********

class VGMInstrSet :
	public VGMFile
{
public:
	BEGIN_MENU_SUB(VGMInstrSet, VGMFile)
		MENU_ITEM(VGMInstrSet, OnSaveAsDLS, L"Convert to DLS")
		MENU_ITEM(VGMInstrSet, OnSaveAsSF2, L"Convert to SoundFont 2")
	END_MENU()

	VGMInstrSet(const string& format, RawFile* file, ULONG offset, ULONG length = 0, wstring name = L"VGMInstrSet",
				VGMSampColl* theSampColl = NULL);
	virtual ~VGMInstrSet(void);

	//bool CreateDLSFile(DLSFile& dls);

	virtual bool Load();
	virtual bool GetHeaderInfo();
	virtual bool GetInstrPointers();
	virtual bool LoadInstrs();
	
	VGMInstr* AddInstr(ULONG offset, ULONG length, unsigned long bank, unsigned long instrNum, const wstring& instrName = L"");

	virtual FileType GetFileType() { return FILETYPE_INSTRSET; }


	bool OnSaveAsDLS(void);
	bool OnSaveAsSF2(void);
	virtual bool SaveAsDLS(const wchar_t* filepath);
	virtual bool SaveAsSF2(const wchar_t* filepath);

public:
	vector<VGMInstr*> aInstrs;
	VGMSampColl* sampColl;
	DLSFile dls;			//needs to be deleted after I change WD.cpp
};





// ********
// VGMInstr
// ********

class VGMInstr :
	public VGMContainerItem
{
public:
	VGMInstr(VGMInstrSet* parInstrSet, ULONG offset, ULONG length, ULONG bank,
			 ULONG instrNum, const wstring& name = L"Instrument");
	virtual ~VGMInstr(void);

	virtual Icon GetIcon() { return ICON_INSTR; };

	inline void SetBank(ULONG bankNum);
	inline void SetInstrNum(ULONG theInstrNum);
	
	VGMRgn* AddRgn(VGMRgn* rgn);
	VGMRgn* AddRgn(ULONG offset, ULONG length, int sampNum, BYTE keyLow = 0,
					BYTE keyHigh = 0x7F, BYTE velLow = 0, BYTE velHigh = 0x7F);

	virtual bool LoadInstr();

public:
	ULONG bank;
	ULONG instrNum;
 
	VGMInstrSet* parInstrSet;
	vector<VGMRgn*> aRgns;
	//string name;
};
