#include "stdafx.h"

#include "common.h"
#include "Root.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "VGMColl.h"
#include "Loader.h"
#include "Scanner.h"
#include "LogItem.h"

#include "Format.h"
#include "NDSFormat.h"
#include "AkaoFormat.h"
#include "FFTFormat.h"		//2009. 6.19(Wed.)	Add
#include "HOSAFormat.h"		//2009. 6.20(Thu.)	Add
#include "PS1Format.h"
#include "SquarePS2Format.h"
#include "MP2kFormat.h"
#include "KonamiGXFormat.h"
#include "QSoundFormat.h"
#include "SonyPS2Format.h"
#include "TriAcePS1Format.h"
#include "OrgFormat.h"
#include "QSoundFormat.h"
#include "SegSatFormat.h"
#include "TaitoF3Format.h"
#include "NinSnesFormat.h"
#include "SquSnesFormat.h"
#include "RareSnesFormat.h"

#include "PSF1Loader.h"
#include "PSF2Loader.h"
#include "SPCLoader.h"
#include "MAMELoader.h"

VGMRoot* pRoot;

VGMRoot::VGMRoot(void)
{
}

VGMRoot::~VGMRoot(void)
{
	DeleteVect<VGMLoader>(vLoader);
	//DeleteVect<VGMScanner>(vScanner);
	DeleteVect<RawFile>(vRawFile);
	DeleteVect<VGMFile>(vVGMFile);
	DeleteVect<LogItem>(vLogItem);
}

// initializes the VGMRoot class by pushing every VGMScanner and
// VGMLoader onto the vectors.
bool VGMRoot::Init(void)
{
	UI_SetRootPtr(&pRoot);

	AddScanner("NDS");
	AddScanner("Akao");
	AddScanner("FFT");
	AddScanner("HOSA");
	AddScanner("PS1");
	AddScanner("SquarePS2");
	AddScanner("SonyPS2");
	AddScanner("TriAcePS1");
	AddScanner("MP2k");
	//AddScanner("Org");
	//AddScanner("QSound");
	//AddScanner("SegSat");
	//AddScanner("TaitoF3");

	// the following scanners use USE_EXTENSION
	//AddScanner("NinSnes");
	//AddScanner("SquSnes");
	AddScanner("RareSnes");

	//load all the... loaders
	AddLoader<PSF1Loader>();
	AddLoader<PSF2Loader>();
	AddLoader<SPCLoader>();
	AddLoader<MAMELoader>();

	return true;
}

void VGMRoot::AddScanner(const string& formatname)
{
	Format* fmt = Format::GetFormatFromName(formatname);
	if (!fmt)
		return;
	VGMScanner& scanner = fmt->GetScanner();
	if (!scanner.Init())
		return;
	vScanner.push_back(&scanner);
}


void VGMRoot::Exit(void)
{
	UI_PreExit();
	Reset();
	UI_Exit();
}

void VGMRoot::Reset(void)
{
	//Close all RawFiles
	DeleteVect<RawFile>(vRawFile);

	//UINT nRawFiles = vRawFile.size();
	//for (UINT i=0; i<nRawFiles; i++)
	//{
	//	RawFile* rawfile = vRawFile[i];
	//	UI_CloseRawFile(rawfile);
	//	delete rawfile;
	//}
	//vRawFile.clear();
	//while (vRawFile.size())
	//	CloseRawFile(vRawFile.back());
}

// opens up a file from the filesystem and scans it for known formats
bool VGMRoot::OpenRawFile(const wstring& filename)
{
	RawFile* newRawFile = new RawFile(filename);
	if (!newRawFile->open(filename))
	{
		delete newRawFile;
		return false;
	}
	
	//if the file was set up properly, apply loaders, scan it, and add it to our list if it contains vgmfiles
	return SetupNewRawFile(newRawFile);
}

// creates a virtual file, a RawFile that was data was created manually,
// not actually opened from the filesystem.  Used, for example, when decompressing
// the contents of PSF2 files
bool VGMRoot::CreateVirtFile(BYTE *databuf, ULONG fileSize, const wstring& filename, const wstring& parRawFileFullPath)
{
	assert(fileSize);
	return SetupNewRawFile(new VirtFile(databuf, fileSize, filename.c_str(), parRawFileFullPath.c_str()));
}

// called by OpenRawFile.  Applies all of the loaders and scanners
// to the RawFile
bool VGMRoot::SetupNewRawFile(RawFile* newRawFile)
{
	newRawFile->SetProPreRatio(0.80);

	if (newRawFile->processFlags & PF_USELOADERS)
		for (UINT i=0; i<vLoader.size(); i++)
		{
			if (vLoader[i]->Apply(newRawFile) == DELETE_IT)
			{
				delete newRawFile;
				return true;
			}
		}

	//this->UI_OnBeginScan();
	if (newRawFile->processFlags & PF_USESCANNERS)
	{
		// see if there is an extension discriminator
		list<VGMScanner*>* lScanners = ExtensionDiscriminator::instance().GetScannerList(newRawFile->GetExtension());
		if (lScanners)
		{
			// if there is, scan with all relevant scanners
			for (list<VGMScanner*>::iterator iter = lScanners->begin(); iter != lScanners->end(); iter++)
				(*iter)->Scan(newRawFile);
		}
		else
		{
			//otherwise, use every scanner
			for (UINT i=0; i<vScanner.size(); i++)
				vScanner[i]->Scan(newRawFile);
		}
	}
	//this->UI_OnEndScan();

	if (newRawFile->containedVGMFiles.size() == 0)
	{
		delete newRawFile;
		return true;
	}
	newRawFile->SetProPreRatio(0.5);
	vRawFile.push_back(newRawFile);
	UI_AddRawFile(newRawFile);
	return true;
}

// Name says it all.
bool VGMRoot::CloseRawFile(RawFile* targFile)
{
	if (targFile == NULL)
		return false;
	vector<RawFile*>::iterator iter = find(vRawFile.begin(), vRawFile.end(), targFile);
	if (iter != vRawFile.end())
		vRawFile.erase(iter);
	else
		Alert(L"Error: trying to delete a rawfile which cannot be found in vRawFile.");
	
	delete targFile;
	return true;
}

// Adds a a VGMFile to the interface.  The UI_AddVGMFile function will handle the
// interface-specific stuff
void VGMRoot::AddVGMFile(VGMFile* theFile)
{
	theFile->GetRawFile()->AddContainedVGMFile(theFile);
	vVGMFile.push_back(theFile);
	UI_AddVGMFile(theFile);
}




/*void VGMRoot::RemoveVGMFileRange(VGMFile* first, VGMFile* last)
{
	vector<VGMFile*>::iterator fIter = find(vVGMFile.begin(), vVGMFile.end(), first);
	vector<VGMFile*>::iterator lIter = find(vVGMFile.begin(), vVGMFile.end(), last);
	UI_RemoveVGMFileRange(targFile);
	for (vector<VGMFile*>::iterator iter = fIter; iter <= lIter; iter++)
		delete *iter;
	if (fIter != vVGMFile.end() && lIter != vVGMFile.end())
		vVGMFile.erase(fIter, lIter);
	else
		Alert("Error: trying to delete a vgmfile which cannot be found in vVGMFile.");
	//delete targFile;
}*/

// Removes a VGMFile from the interface.  The UI_RemoveVGMFile will handle the
// interface-specific stuff
void VGMRoot::RemoveVGMFile(VGMFile* targFile, bool bRemoveFromRaw)
{
	// First we should call the format's onClose handler in case it needs to use
	// the RawFile before we close it (FilenameMatcher, for ex)
	Format* fmt = targFile->GetFormat();
	if (fmt)
		fmt->OnCloseFile(targFile);

	vector<VGMFile*>::iterator iter = find(vVGMFile.begin(), vVGMFile.end(), targFile);
	if (iter != vVGMFile.end())
		vVGMFile.erase(iter);
	else
		Alert(L"Error: trying to delete a VGMFile which cannot be found in vVGMFile.");
	if (bRemoveFromRaw)
		targFile->rawfile->RemoveContainedVGMFile(targFile);
	while (targFile->assocColls.size())
		RemoveVGMColl(targFile->assocColls.back());

	

	UI_RemoveVGMFile(targFile);
	delete targFile;
}



void VGMRoot::AddVGMColl(VGMColl* theColl)
{
	vVGMColl.push_back(theColl);
	UI_AddVGMColl(theColl);
}


void VGMRoot::RemoveVGMColl(VGMColl* targColl)
{
	targColl->RemoveFileAssocs();
	vector<VGMColl*>::iterator iter = find(vVGMColl.begin(), vVGMColl.end(), targColl);
	if (iter != vVGMColl.end())
		vVGMColl.erase(iter);
	else
		Alert(L"Error: trying to delete a VGMColl which cannot be found in vVGMColl.");
	UI_RemoveVGMColl(targColl);
	delete targColl;
}



// This virtual function is called whenever a VGMFile is added to the interface.
// By default, it simply sorts out what type of file was added and then calls a more
// specific virtual function for the file type.  It is virtual in case a user-interface
// wants do something universally whenever any type of VGMFiles is added.
void VGMRoot::UI_AddVGMFile(VGMFile* theFile)
{
	switch(theFile->GetFileType())
	{
	case FILETYPE_SEQ:
		UI_AddVGMSeq((VGMSeq*)theFile);
		break;
	case FILETYPE_INSTRSET:
		UI_AddVGMInstrSet((VGMInstrSet*)theFile);
		break;
	case FILETYPE_SAMPCOLL:
		UI_AddVGMSampColl((VGMSampColl*)theFile);
		break;
	case FILETYPE_MISC:
		UI_AddVGMMisc((VGMMiscFile*)theFile);
		break;
	}
}

// Given a pointer to a buffer of data, size, and a filename, this function writes the data
// into a file on the filesystem.
bool VGMRoot::UI_WriteBufferToFile(const wstring& filepath, BYTE* buf, ULONG size)
{	
#if _MSC_VER < 1400			//if we're not using VC8, and the new STL that supports widechar filenames in ofstream...
	char newpath[_MAX_PATH];
	wcstombs(newpath, filepath, _MAX_PATH);
	ofstream outfile (newpath, ios::out | ios::trunc | ios::binary);
#else
	ofstream outfile (filepath, ios::out | ios::trunc | ios::binary);
#endif
	if (!outfile.is_open())		//if attempt to open file failed
		return false;

	outfile.write((const char*)buf, size);
	outfile.close();
	return true;
	
}




bool VGMRoot::SaveAllAsRaw()
{
	wstring dirpath = UI_GetSaveDirPath();\
	if (dirpath.length() != 0)
	{
		for (UINT i=0; i<vVGMFile.size(); i++)
		{
			bool result;
			VGMFile* file = vVGMFile[i];
			wstring filepath = dirpath + L"\\" + file->GetName()->c_str();
			BYTE* buf = new BYTE[file->unLength];		//create a buffer the size of the file
			file->GetBytes(file->dwOffset, file->unLength, buf);
			result = UI_WriteBufferToFile(filepath.c_str(), buf, file->unLength);
			delete[] buf;
		}
		return true;
	}
	return false;
}

// Adds a log item to the interface.  The UI_AddLog function will handle the
// interface-specific stuff
void VGMRoot::AddLogItem(LogItem* theLog)
{
	vLogItem.push_back(theLog);
	UI_AddLogItem(theLog);
}
