/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <sys/stat.h>

#include "pch.h"
#include "CLIVGMRoot.h"
#include "DLSFile.h"
#include "SF2File.h"
#include "VGMColl.h"
#include "VGMSeq.h"

CLIVGMRoot cliroot;

// displays a usage message
void CLIVGMRoot::DisplayUsage() {
    cerr << "usage: " << CLI_APP_NAME << " input_file1 input_file2 ... -o output_directory" << endl;
}

// creates the output directory if it does not already exist
bool CLIVGMRoot::MakeOutputDir() {
  struct stat sb;
  if (stat(outputDir.c_str(), &sb) != 0) {
    // directory doesn't exist: need to create it
    if (mkdir(outputDir.c_str(), 0770)) {
      cerr << "Could not create directory " + outputDir << endl;
      return false;
    }
    cerr << "Created new directory " + outputDir << endl;
  }
  return true;
}

bool CLIVGMRoot::OpenRawFile(const wstring &filename) {
  wcerr << "Loading " << filename << endl;
  return VGMRoot::OpenRawFile(filename);
}

bool CLIVGMRoot::Init() {
  if (inputFiles.empty()) {
    DisplayUsage();
    cerr << "error: must provide at least one input file" << endl;
    return false;
  }

  VGMRoot::Init();

  // get collection for each input file
  int inputFileCtr = 0;
  for (string infile : inputFiles) {
    if (!OpenRawFile(string2wstring(infile))) {  // file not found
      return false;
    }
    int numCollsAdded = GetNumCollections() - inputFileCtr;
    if (numCollsAdded == 0) {
      cerr << "File " << infile << " is not a recognized music file" << endl;
    }
    else if (numCollsAdded == 1) {
      ++inputFileCtr;
    }
    else {
      // TODO: handle this case
      cerr << numCollsAdded << " collections added for " << infile << endl;
      return false;
    }
  }
  size_t numCollections = GetNumCollections();
  if (numCollections == 0) {
    cerr << "\nNo music files to convert." << endl;
    return true;
  }
  else {
    cerr << "\nInput files:     " << inputFileCtr << endl;
    cerr << "VGM collections: " << numCollections << endl;
    cerr << "Output files:    " << cliroot.vVGMFile.size() << endl << endl;
    return MakeOutputDir();
  }
}

bool CLIVGMRoot::ConvertAllCollections() {
  bool success = true;
  for (VGMColl* coll : vVGMColl) {
    wstring collName = *coll->GetName();
    success &= ConvertCollection(coll);
  }
  return success;
}

bool CLIVGMRoot::ConvertCollection(VGMColl* coll) {
    wstring collName = *coll->GetName();
    wcerr << "Converting " << collName << " -> " << endl;
    return SaveMidi(coll) & SaveSF2(coll) & SaveDLS(coll);
}

bool CLIVGMRoot::SaveMidi(VGMColl* coll) {
  if (coll->seq != nullptr) {
    wstring collName = *coll->GetName();
    wstring filepath = UI_GetSaveFilePath(collName, L"mid");
    if (!coll->seq->SaveAsMidi(filepath)) {
      pRoot->AddLogItem(new LogItem(std::wstring(L"Failed to save MIDI file"),
        LOG_LEVEL_ERR, L"VGMColl"));
      return false;
    }
    wcerr << L"\t" + filepath << endl;
  }
  return true;
}

bool CLIVGMRoot::SaveSF2(VGMColl* coll) {
  wstring collName = *coll->GetName();
  wstring filepath = UI_GetSaveFilePath(collName, L"sf2");
  SF2File *sf2file = coll->CreateSF2File();
  bool success = false;
  if (sf2file != nullptr) {
    if (sf2file->SaveSF2File(filepath)) {
      success = true;
    }
    delete sf2file;
  }
  if (success) {
    wcerr << L"\t" + filepath << endl;
  }
  else {
    pRoot->AddLogItem(new LogItem(std::wstring(L"Failed to save SF2 file"),
      LOG_LEVEL_ERR, L"VGMColl"));
  }
  return success;
}

bool CLIVGMRoot::SaveDLS(VGMColl* coll) {
  wstring collName = *coll->GetName();
  wstring filepath = UI_GetSaveFilePath(collName, L"dls");
  DLSFile dlsfile;
  bool success = false;
  if (coll->CreateDLSFile(dlsfile)) {
    if (dlsfile.SaveDLSFile(filepath)) {
      success = true;
    }
  }
  if (success) {
    wcerr << L"\t" + filepath << endl;
  }
  else {
    pRoot->AddLogItem(new LogItem(std::wstring(L"Failed to save DLS file"),
      LOG_LEVEL_ERR, L"VGMColl"));
  }
  return success;
}

void CLIVGMRoot::UI_SetRootPtr(VGMRoot** theRoot) {
  *theRoot = &cliroot;
}

void CLIVGMRoot::UI_AddLogItem(LogItem* theLog) {
  if (theLog->GetLogLevel() <= LOG_LEVEL_WARN) {
    wcerr << "[" << theLog->GetSource() << "]" << theLog->GetText() << endl;
  }
}

wstring CLIVGMRoot::UI_GetOpenFilePath(const wstring& suggestedFilename, const wstring& extension) {
  return L"Placeholder";
}

wstring CLIVGMRoot::UI_GetSaveFilePath(const wstring& suggestedFilename, const wstring& extension) {
  return string2wstring(outputDir) + L"/" + ConvertToSafeFileName(suggestedFilename) + L"." + extension;
}

wstring CLIVGMRoot::UI_GetSaveDirPath(const std::wstring& suggestedDir) {
  return string2wstring(this->outputDir);
}
