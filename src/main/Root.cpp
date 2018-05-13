#include "pch.h"

#include "common.h"
#include "Root.h"
#include "VGMFile.h"
#include "VGMColl.h"

#include "Format.h"
#include "AkaoFormat.h"
#include "PS1Format.h"

#include "PSF1Loader.h"
#include "PSF2Loader.h"
#include "GSFLoader.h"
#include "SNSFLoader.h"
#include "NDS2SFLoader.h"
#include "NCSFLoader.h"
#include "SPCLoader.h"
#include "MAMELoader.h"

using namespace std;

VGMRoot *pRoot;

VGMRoot::VGMRoot(void) {
}

VGMRoot::~VGMRoot(void) {
  DeleteVect<VGMLoader>(vLoader);
  //DeleteVect<VGMScanner>(vScanner);
  DeleteVect<RawFile>(vRawFile);
  DeleteVect<VGMFile>(vVGMFile);
  DeleteVect<LogItem>(vLogItem);
}

// initializes the VGMRoot class by pushing every VGMScanner and
// VGMLoader onto the vectors.
bool VGMRoot::Init(void) {
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
  AddScanner("HeartBeatPS1");
  AddScanner("TamSoftPS1");
  AddScanner("KonamiPS1");
  //AddScanner("Org");
  //AddScanner("QSound");
  //AddScanner("SegSat");
  //AddScanner("TaitoF3");

  // the following scanners use USE_EXTENSION
  //AddScanner("NinSnes");
  //AddScanner("SuzukiSnes");
  //AddScanner("CapcomSnes");
  //AddScanner("RareSnes");

  //load all the... loaders
  AddLoader<PSF1Loader>();
  AddLoader<PSF2Loader>();
  AddLoader<GSFLoader>();
  AddLoader<SNSFLoader>();
  AddLoader<NDS2SFLoader>();
  AddLoader<NCSFLoader>();
  AddLoader<SPCLoader>();
  AddLoader<MAMELoader>();

  return true;
}

void VGMRoot::AddScanner(const string &formatname) {
  Format *fmt = Format::GetFormatFromName(formatname);
  if (!fmt)
    return;
  VGMScanner &scanner = fmt->GetScanner();
  if (!scanner.Init())
    return;
  vScanner.push_back(&scanner);
}


void VGMRoot::Exit(void) {
  UI_PreExit();
  Reset();
  UI_Exit();
}

void VGMRoot::Reset(void) {
  //Close all RawFiles
  DeleteVect<RawFile>(vRawFile);
}

// opens up a file from the filesystem and scans it for known formats
bool VGMRoot::OpenRawFile(const wstring &filename) {
  RawFile *newRawFile = new RawFile(filename);
  if (!newRawFile->open(filename)) {
    delete newRawFile;
    return false;
  }

  //if the file was set up properly, apply loaders, scan it, and add it to our list if it contains vgmfiles
  return SetupNewRawFile(newRawFile);
}

// creates a virtual file, a RawFile that was data was created manually,
// not actually opened from the filesystem.  Used, for example, when decompressing
// the contents of PSF2 files
bool VGMRoot::CreateVirtFile(uint8_t *databuf,
                             uint32_t fileSize,
                             const wstring &filename,
                             const wstring &parRawFileFullPath,
                             const VGMTag tag) {
  assert(fileSize != 0);

  VirtFile *newVirtFile = new VirtFile(databuf, fileSize, filename.c_str(), parRawFileFullPath.c_str(), tag);
  if (newVirtFile == NULL) {
    return false;
  }

  if (!SetupNewRawFile(newVirtFile)) {
    delete newVirtFile;
    return false;
  }

  return true;
}

// called by OpenRawFile.  Applies all of the loaders and scanners
// to the RawFile
bool VGMRoot::SetupNewRawFile(RawFile *newRawFile) {
  newRawFile->SetProPreRatio((float) 0.80);

  if (newRawFile->processFlags & PF_USELOADERS)
    for (uint32_t i = 0; i < vLoader.size(); i++) {
      if (vLoader[i]->Apply(newRawFile) == DELETE_IT) {
        delete newRawFile;
        return true;
      }
    }

  //this->UI_OnBeginScan();
  if (newRawFile->processFlags & PF_USESCANNERS) {
    // see if there is an extension discriminator
    list<VGMScanner *> *lScanners = ExtensionDiscriminator::instance().GetScannerList(newRawFile->GetExtension());
    if (lScanners) {
      // if there is, scan with all relevant scanners
      for (list<VGMScanner *>::iterator iter = lScanners->begin(); iter != lScanners->end(); iter++)
        (*iter)->Scan(newRawFile);
    }
    else {
      //otherwise, use every scanner
      for (uint32_t i = 0; i < vScanner.size(); i++)
        vScanner[i]->Scan(newRawFile);
    }
  }
  //this->UI_OnEndScan();

  if (newRawFile->containedVGMFiles.size() == 0) {
    delete newRawFile;
    return true;
  }
  newRawFile->SetProPreRatio(0.5);
  vRawFile.push_back(newRawFile);
  UI_AddRawFile(newRawFile);
  return true;
}

// Name says it all.
bool VGMRoot::CloseRawFile(RawFile *targFile) {
  if (targFile == NULL)
    return false;
  vector<RawFile *>::iterator iter = find(vRawFile.begin(), vRawFile.end(), targFile);
  if (iter != vRawFile.end())
    vRawFile.erase(iter);
  else
    pRoot->AddLogItem(new LogItem(std::wstring(L"Error: trying to delete a rawfile which cannot be found in vRawFile."),
                                  LOG_LEVEL_DEBUG,
                                  L"Root"));

  delete targFile;
  return true;
}

// Adds a a VGMFile to the interface.  The UI_AddVGMFile function will handle the
// interface-specific stuff
void VGMRoot::AddVGMFile(VGMFile *theFile) {
  theFile->GetRawFile()->AddContainedVGMFile(theFile);
  vVGMFile.push_back(theFile);
  UI_AddVGMFile(theFile);
}

// Removes a VGMFile from the interface.  The UI_RemoveVGMFile will handle the
// interface-specific stuff
void VGMRoot::RemoveVGMFile(VGMFile *targFile, bool bRemoveFromRaw) {
  // First we should call the format's onClose handler in case it needs to use
  // the RawFile before we close it (FilenameMatcher, for ex)
  Format *fmt = targFile->GetFormat();
  if (fmt)
    fmt->OnCloseFile(targFile);

  vector<VGMFile *>::iterator iter = find(vVGMFile.begin(), vVGMFile.end(), targFile);
  if (iter != vVGMFile.end())
    vVGMFile.erase(iter);
  else
    pRoot->AddLogItem(new LogItem(std::wstring(L"Error: trying to delete a VGMFile which cannot be found in vVGMFile."),
                                  LOG_LEVEL_DEBUG,
                                  L"Root"));
  if (bRemoveFromRaw)
    targFile->rawfile->RemoveContainedVGMFile(targFile);
  while (targFile->assocColls.size())
    RemoveVGMColl(targFile->assocColls.back());


  UI_RemoveVGMFile(targFile);
  delete targFile;
}


void VGMRoot::AddVGMColl(VGMColl *theColl) {
  vVGMColl.push_back(theColl);
  UI_AddVGMColl(theColl);
}


void VGMRoot::RemoveVGMColl(VGMColl *targColl) {
  targColl->RemoveFileAssocs();
  vector<VGMColl *>::iterator iter = find(vVGMColl.begin(), vVGMColl.end(), targColl);
  if (iter != vVGMColl.end())
    vVGMColl.erase(iter);
  else
    pRoot->AddLogItem(new LogItem(std::wstring(L"Error: trying to delete a VGMColl which cannot be found in vVGMColl."),
                                  LOG_LEVEL_DEBUG,
                                  L"Root"));
  UI_RemoveVGMColl(targColl);
  delete targColl;
}


// This virtual function is called whenever a VGMFile is added to the interface.
// By default, it simply sorts out what type of file was added and then calls a more
// specific virtual function for the file type.  It is virtual in case a user-interface
// wants do something universally whenever any type of VGMFiles is added.
void VGMRoot::UI_AddVGMFile(VGMFile *theFile) {
  switch (theFile->GetFileType()) {
    case FILETYPE_SEQ:
      UI_AddVGMSeq((VGMSeq *) theFile);
      break;
    case FILETYPE_INSTRSET:
      UI_AddVGMInstrSet((VGMInstrSet *) theFile);
      break;
    case FILETYPE_SAMPCOLL:
      UI_AddVGMSampColl((VGMSampColl *) theFile);
      break;
    case FILETYPE_MISC:
      UI_AddVGMMisc((VGMMiscFile *) theFile);
      break;
  }
}

// Given a pointer to a buffer of data, size, and a filename, this function writes the data
// into a file on the filesystem.
bool VGMRoot::UI_WriteBufferToFile(const wstring &filepath, uint8_t *buf, uint32_t size) {
#if _MSC_VER < 1400            //if we're not using VC8, and the new STL that supports widechar filenames in ofstream...
  char newpath[PATH_MAX];
  wcstombs(newpath, filepath.c_str(), PATH_MAX);
  ofstream outfile(newpath, ios::out | ios::trunc | ios::binary);
#else
  ofstream outfile (filepath, ios::out | ios::trunc | ios::binary);
#endif
  if (!outfile.is_open())        //if attempt to open file failed
    return false;

  outfile.write((const char *) buf, size);
  outfile.close();
  return true;

}


bool VGMRoot::SaveAllAsRaw() {
  wstring dirpath = UI_GetSaveDirPath();\
    if (dirpath.length() != 0) {
    for (uint32_t i = 0; i < vVGMFile.size(); i++) {
      bool result;
      VGMFile *file = vVGMFile[i];
      wstring filepath = dirpath + L"\\" + file->GetName()->c_str();
      uint8_t *buf = new uint8_t[file->unLength];        //create a buffer the size of the file
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
void VGMRoot::AddLogItem(LogItem *theLog) {
  vLogItem.push_back(theLog);
  UI_AddLogItem(theLog);
}
