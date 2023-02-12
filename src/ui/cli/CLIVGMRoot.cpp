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

// gets the base name of a file path
string getBaseName(const string& filename) {
  size_t lastSlash = filename.find_last_of("/");
  string fname = (lastSlash == string::npos) ? filename : filename.substr(lastSlash + 1, filename.length());
  size_t lastDot = fname.find_last_of(".");
  return (lastDot == string::npos) ? fname : fname.substr(0, lastDot);
}

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
    cout << "Created new directory " + outputDir << endl;
  }
  return true;
}

bool CLIVGMRoot::OpenRawFile(const wstring &filename) {
  wstring fname = filename;
  cout << "Loading " << wstring2string(fname) << endl;
  return VGMRoot::OpenRawFile(filename);
}

bool CLIVGMRoot::Init() {
  if (inputFiles.empty()) {
    DisplayUsage();
    cerr << "Error: must provide at least one input file" << endl;
    return false;
  }

  VGMRoot::Init();

  // get collection for each input file
  size_t inputFileCtr = 0;
  size_t numColls = 0;
  // map for deconflicting identical collection names using input filenames
  map<wstring, vector<pair<size_t, string>>> collNameMap {};
  for (string infile : inputFiles) {
    if (!OpenRawFile(string2wstring(infile))) {  // file not found
      return false;
    }
    size_t numCollsAdded = GetNumCollections() - numColls;
    if (numCollsAdded == 0) {
      cout << "File " << infile << " is not a recognized music file" << endl;
    }
    else {
      // string collStr = (numCollsAdded == 1) ? "collection" : "collections";
      // cout << numCollsAdded << " " << collStr << " added for " << infile << endl;
      for(size_t i = numColls; i < GetNumCollections(); ++i) {
        VGMColl* coll = vVGMColl[i];
        wstring collName = *coll->GetName();
        auto it = collNameMap.find(collName);
        pair<size_t, string> p = make_pair(i, infile);
        if (it == collNameMap.end()) {
          vector<pair<size_t, string>> pairs {p};
          collNameMap[collName] = pairs;
        }
        else {
          it->second.push_back(p);
        }
      }
      ++inputFileCtr;
      numColls += numCollsAdded;
    }
  }

  if (numColls == 0) {
    cout << "\nNo music files to convert." << endl;
    return true;
  }
  else {
    // deconflict collection names
    for(auto collNameIt = collNameMap.begin(); collNameIt != collNameMap.end(); ++collNameIt) {
      vector<pair<size_t, string>> pairs = collNameIt->second;
      if (pairs.size() > 1) {  // name conflict
        map<string, size_t> baseNameCtr {};
        string baseName;
        size_t ct;
        for(pair<size_t, string> p : pairs) {
          baseName = getBaseName(p.second);
          auto baseNameCtrIt = baseNameCtr.find(baseName);
          ct = (baseNameCtrIt == baseNameCtr.end()) ? 0 : baseNameCtrIt->second;
          baseNameCtr[baseName] = ct + 1;
        }
        map<string, size_t> baseNameIdx {};
        for(pair<size_t, string> p : pairs) {
          baseName = getBaseName(p.second);
          // apply a unique suffix to the collection name
          string suffix = " - " + baseName;
          ct = baseNameCtr.find(baseName)->second;
          if (ct > 1) {
            // base name is not unique, so we additionally need an integer to distinguish
            auto baseNameIdxIt = baseNameIdx.find(baseName);
            size_t idx = (baseNameIdxIt == baseNameIdx.end()) ? 0 : baseNameIdxIt->second;
            ++idx;
            suffix += " - " + to_string(idx);
            baseNameIdx[baseName] = idx;
          }
          // update collection name to be unique
          wstring newCollName = collNameIt->first + string2wstring(suffix);
          vVGMColl[p.first]->SetName(&newCollName);
        }
      }
    }

    cout << "\nInput files:     " << inputFileCtr << endl;
    cout << "VGM collections: " << numColls << endl;
    cout << "Output files:    " << cliroot.vVGMFile.size() << endl << endl;
    return MakeOutputDir();
  }
}

bool CLIVGMRoot::ExportAllCollections() {
  bool success = true;
  for (VGMColl* coll : vVGMColl) {
    wstring collName = *coll->GetName();
    success &= ExportCollection(coll);
  }
  return success;
}

bool CLIVGMRoot::ExportCollection(VGMColl* coll) {
    wstring collName = *coll->GetName();
    cout << "Exporting: " << wstring2string(collName) << endl;
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
    cout << "\t" + wstring2string(filepath) << endl;
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
    cout << "\t" + wstring2string(filepath) << endl;
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
    cout << "\t" + wstring2string(filepath) << endl;
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
    wstring source = theLog->GetSource();
    wstring text = theLog->GetText();
    cerr << "[" << wstring2string(source) << "]" << wstring2string(text) << endl;
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
