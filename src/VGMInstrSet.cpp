#include "stdafx.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "VGMColl.h"
#include "Root.h"
#include "math.h"

DECLARE_MENU(VGMInstrSet)


// ***********
// VGMInstrSet
// ***********

VGMInstrSet::VGMInstrSet(const string& format,/*FmtID fmtID,*/ RawFile* file, ULONG offset, ULONG length, wstring name,
						 VGMSampColl* theSampColl)
: VGMFile(FILETYPE_INSTRSET, /*fmtID,*/format, file, offset, length, name), sampColl(theSampColl)
{	
	AddContainer<VGMInstr>(aInstrs);
}

VGMInstrSet::~VGMInstrSet()
{	
	DeleteVect<VGMInstr>(aInstrs);
	//VGMSampColl* sampColl;
}



VGMInstr* VGMInstrSet::AddInstr(ULONG offset, ULONG length, unsigned long bank,
								unsigned long instrNum, const wstring& instrName)
{
	wostringstream	name;
	if (instrName == L"")
		name << L"Instrument " << aInstrs.size();
	else
		name << instrName;

	VGMInstr* instr = new VGMInstr(this, offset, length, bank, instrNum, name.str());
	aInstrs.push_back(instr);
	return instr;
}

int VGMInstrSet::Load()
{
	if (!GetHeaderInfo())
		return false;
	if (!GetInstrPointers())
		return false;
	if (!LoadInstrs())
		return false;

	if (aInstrs.size() == 0)
		return false;

	if (unLength == 0)
		unLength = aInstrs.back()->dwOffset + aInstrs.back()->unLength - dwOffset;

	if (sampColl)
		sampColl->Load();

	//CreateDLSFile();

	//UseRawFileData();
	LoadLocalData();
	UseLocalData();
	pRoot->AddVGMFile(this);
	return true;
}

int VGMInstrSet::GetHeaderInfo()
{
	return true;
}

int VGMInstrSet::GetInstrPointers()
{
	return true;
}


int VGMInstrSet::LoadInstrs()
{
	ULONG nInstrs = aInstrs.size();
	for (UINT i=0; i < nInstrs; i++)
	{
		if (!aInstrs[i]->LoadInstr())
			return false;
	}
	return true;
}


bool VGMInstrSet::OnSaveAsDLS(void)
{
	wstring filepath = pRoot->UI_GetSaveFilePath(name.c_str(), L"dls");
	if (filepath.length() != 0)
	{	
		return SaveAsDLS(filepath.c_str());
	}
	return false;
}

bool VGMInstrSet::OnSaveAsSF2(void)
{
	wstring filepath = pRoot->UI_GetSaveFilePath(name.c_str(), L"sf2");
	if (filepath.length() != 0)
	{	
		return SaveAsSF2(filepath.c_str());
	}
	return false;
}


bool VGMInstrSet::SaveAsDLS(const wchar_t* filepath)
{
	DLSFile dlsfile;
//	if (sampColl)
//		CreateDLSFile(dlsfile);
//	else 
	if (assocColls.size())
		assocColls.front()->CreateDLSFile(dlsfile);
	else
		return false;
	return dlsfile.SaveDLSFile(filepath);
}

bool VGMInstrSet::SaveAsSF2(const wchar_t* filepath)
{
	SF2File* sf2file = NULL;
//	if (sampColl)
//		CreateDLSFile(dlsfile);
//	else 
	if (assocColls.size())
		sf2file = assocColls.front()->CreateSF2File();
	else
		return false;
	if (sf2file != NULL)
	{	
		bool bResult =sf2file->SaveSF2File(filepath);
		delete sf2file;
		return bResult;
	}
	return false;
}


// ********
// VGMInstr
// ********

VGMInstr::VGMInstr(VGMInstrSet* instrSet, ULONG offset, ULONG length, ULONG theBank,
				  ULONG theInstrNum, const wstring& name)
 : 	VGMContainerItem(instrSet, offset, length, name), 
    parInstrSet(instrSet), 
	bank(theBank), 
	instrNum(theInstrNum)
{
	AddContainer<VGMRgn>(aRgns);
}

VGMInstr::~VGMInstr()
{
	DeleteVect<VGMRgn>(aRgns);
}

void VGMInstr::SetBank(ULONG bankNum)
{
	bank = bankNum;
}

void VGMInstr::SetInstrNum(ULONG theInstrNum)
{
	instrNum = theInstrNum;
}

VGMRgn* VGMInstr::AddRgn(VGMRgn* rgn)
{
	aRgns.push_back(rgn);
	return rgn;
}

VGMRgn* VGMInstr::AddRgn(ULONG offset, ULONG length, int sampNum, BYTE keyLow, BYTE keyHigh,
			   BYTE velLow, BYTE velHigh)
{
	VGMRgn* newRgn = new VGMRgn(this, offset, length, keyLow, keyHigh, velLow, velHigh, sampNum);
	aRgns.push_back(newRgn);
	return newRgn;
}


int VGMInstr::LoadInstr()
{
	return true;
}

