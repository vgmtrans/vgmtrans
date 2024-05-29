/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <iostream>
#include <filesystem>

#include <version.h>
#include "Format.h"
#include "Matcher.h"
#include "CLIVGMRoot.h"
#include "DLSFile.h"
#include "LogManager.h"
#include "SF2File.h"
#include "VGMColl.h"
#include "VGMSeq.h"

using namespace std;
namespace fs = std::filesystem;

CLIVGMRoot cliroot;

// displays a usage message
void CLIVGMRoot::DisplayUsage() {
  cerr << "Usage: " << CLI_APP_NAME << " input_file1 input_file2 ... -o output_directory" << endl;
}

// displays a help message
void CLIVGMRoot::DisplayHelp() {
  cerr << "VGMTrans version " << VGMTRANS_VERSION << endl;
  cerr << "Converts music files used in console video games into industry-standard MIDI and DLS/SF2 files." << endl << endl;
  DisplayUsage();
}

// creates the output directory if it does not already exist
bool CLIVGMRoot::MakeOutputDir() {
  if (!fs::exists(outputDir)) {
    // directory doesn't exist: need to create it
    if (!fs::create_directory(outputDir)) {
      cerr << "Could not create directory " + outputDir.string() << endl;
      return false;
    }
    cout << "Created new directory " + outputDir.string() << endl;
  }
  return true;
}

bool CLIVGMRoot::OpenRawFile(const string &filename) {
  string fname = filename;
  cout << "Loading " << fname << endl;
  return VGMRoot::OpenRawFile(filename);
}

bool CLIVGMRoot::Init() {
  if (inputFiles.empty()) {
    cerr << "Error: must provide at least one input file" << endl;
    DisplayUsage();
    return false;
  }

  VGMRoot::Init();

  // get collection for each input file
  size_t inputFileCtr = 0;
  size_t numColls = 0;
  size_t numVGMFiles = 0;
  // map for deconflicting identical collection names using input filenames
  map<string, vector<pair<size_t, fs::path>>> collNameMap {};
  for (fs::path infile : inputFiles) {
    if (!OpenRawFile(infile.string())) {  // file not found
      return false;
    }
    numVGMFiles = UpdateCollections(numVGMFiles);
    size_t numCollsAdded = GetNumCollections() - numColls;
    if (numCollsAdded == 0) {
      cout << "File " << infile.string() << " is not a recognized music file" << endl;
    }
    else {
      for(size_t i = numColls; i < GetNumCollections(); ++i) {
        VGMColl* coll = vgmColls()[i];
        string collName = coll->GetName();
        auto it = collNameMap.find(collName);
        pair<size_t, fs::path> p = make_pair(i, infile);
        if (it == collNameMap.end()) {
          vector<pair<size_t, fs::path>> pairs {p};
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
      vector<pair<size_t, fs::path>> pairs = collNameIt->second;
      if (pairs.size() > 1) {  // name conflict
        map<string, size_t> baseNameCtr {};
        string baseName;
        size_t ct;
        for(pair<size_t, fs::path> p : pairs) {
          baseName = p.second.stem().string();
          auto baseNameCtrIt = baseNameCtr.find(baseName);
          ct = (baseNameCtrIt == baseNameCtr.end()) ? 0 : baseNameCtrIt->second;
          baseNameCtr[baseName] = ct + 1;
        }
        map<string, size_t> baseNameIdx {};
        for(pair<size_t, fs::path> p : pairs) {
          baseName = p.second.stem().string();
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
          string newCollName = collNameIt->first + suffix;
          vgmColls()[p.first]->SetName(newCollName);
        }
      }
    }

    cout << "\nInput files:        " << inputFileCtr << endl;
    cout << "Output collections: " << numColls << endl << endl;
    return MakeOutputDir();
  }
}

bool CLIVGMRoot::ExportAllCollections() {
  bool success = true;
  for (VGMColl* coll : vgmColls()) {
    string collName = coll->GetName();
    success &= ExportCollection(coll);
  }
  return success;
}

bool CLIVGMRoot::ExportCollection(VGMColl* coll) {
    string collName = coll->GetName();
    cout << "Exporting: " << collName << endl;
    return SaveMidi(coll) & SaveSF2(coll) & SaveDLS(coll);
}

bool CLIVGMRoot::SaveMidi(const VGMColl* coll) {
  if (coll->seq != nullptr) {
    string collName = coll->GetName();
    string filepath = UI_GetSaveFilePath(collName, "mid");
    if (!coll->seq->SaveAsMidi(filepath)) {
      L_ERROR("Failed to save MIDI file");
      return false;
    }
    cout << "\t" + filepath << endl;
  }
  return true;
}

bool CLIVGMRoot::SaveSF2(VGMColl* coll) {
  string collName = coll->GetName();
  string filepath = UI_GetSaveFilePath(collName, "sf2");
  SF2File *sf2file = coll->CreateSF2File();
  bool success = false;
  if (sf2file != nullptr) {
    if (sf2file->SaveSF2File(filepath)) {
      success = true;
    }
    delete sf2file;
  }
  if (success) {
    cout << "\t" + filepath << endl;
  }
  else {
    L_ERROR("Failed to save SF2 file");
  }
  return success;
}

bool CLIVGMRoot::SaveDLS(VGMColl* coll) {
  string collName = coll->GetName();
  string filepath = UI_GetSaveFilePath(collName, "dls");
  DLSFile dlsfile;
  bool success = false;
  if (coll->CreateDLSFile(dlsfile)) {
    if (dlsfile.SaveDLSFile(filepath)) {
      success = true;
    }
  }
  if (success) {
    cout << "\t" + filepath << endl;
  }
  else {
    L_ERROR("Failed to save DLS file");
  }
  return success;
}

void CLIVGMRoot::UI_SetRootPtr(VGMRoot** theRoot) {
  *theRoot = &cliroot;
}

void CLIVGMRoot::UI_Log(LogItem* theLog) {
  if (theLog->GetLogLevel() <= LOG_LEVEL_WARN) {
    string source = theLog->GetSource();
    string text = theLog->GetText();
    cerr << "[" << source << "]" << text << endl;
  }
}

size_t CLIVGMRoot::UpdateCollections(size_t startOffset) {
  auto files = vgmFiles();
  for (int i = startOffset; i < files.size(); ++i) {
    auto targFile = variantToVGMFile(files[i]);
    Format *fmt = targFile->format();
    if (fmt && fmt->matcher) {
      fmt->matcher->MakeCollectionsForFile(targFile);
    }
  }
  return vgmFiles().size();
}

string CLIVGMRoot::UI_GetSaveFilePath(const string& suggestedFilename, const string& extension) {
  fs::path savePath = outputDir / fs::path(ConvertToSafeFileName(suggestedFilename) + "." + extension);
  return savePath.string();
}

string CLIVGMRoot::UI_GetSaveDirPath(const std::string& suggestedDir) {
  return this->outputDir.string();
}
