/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include <spdlog/fmt/fmt.h>
#include <fstream>
#include <algorithm>

#include "VGMExport.h"
#include "helper.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMColl.h"
#include "SynthFile.h"

namespace fs = std::filesystem;

namespace conversion {

bool saveAsDLS(VGMInstrSet &set, const fs::path &filepath) {
  VGMColl* coll = !set.assocColls.empty() ? set.assocColls.front() : nullptr;
  if (!coll && !set.sampColl)
    return false;

  std::vector<VGMInstrSet*> instrsets;
  std::vector<VGMSampColl*> sampcolls;
  if (coll) {
    instrsets = coll->instrSets();
    sampcolls = coll->sampColls();
  } else {
    instrsets.emplace_back(&set);
  }

  DLSFile dlsfile;
  if (createDLSFile(dlsfile, instrsets, sampcolls, coll)) {
    return dlsfile.saveDLSFile(filepath);
  }
  return false;
}

bool saveAsSF2(VGMInstrSet &set, const fs::path &filepath) {
  VGMColl* coll = !set.assocColls.empty() ? set.assocColls.front() : nullptr;
  if (!coll && !set.sampColl)
    return false;

  std::vector<VGMInstrSet*> instrsets;
  std::vector<VGMSampColl*> sampcolls;
  if (coll) {
    instrsets = coll->instrSets();
    sampcolls = coll->sampColls();
  } else {
    instrsets.emplace_back(&set);
  }

  if (auto sf2file = createSF2File(instrsets, sampcolls, coll); sf2file) {
    bool bResult = sf2file->saveSF2File(filepath);
    delete sf2file;
    return bResult;
  }

  return false;
}

bool saveAsSF2(const VGMColl &coll, const fs::path &filepath) {
  if (auto sf2file = createSF2File(coll.instrSets(), coll.sampColls(), &coll); sf2file) {
    bool bResult = sf2file->saveSF2File(filepath);
    delete sf2file;
    return bResult;
  }

  return false;
}

void saveAllAsWav(const VGMSampColl &coll, const fs::path &save_dir) {
  const auto collName = makeSafeFilePath(coll.name()).u8string();
  for (auto &sample : coll.samples) {
    std::u8string filename = collName;
    filename += u8" - ";
    filename += makeSafeFilePath(sample->name()).u8string();
    filename += u8".wav";
    auto path = save_dir / fs::path(filename);
    sample->saveAsWav(path);
  }
}

bool saveDataToFile(const char* begin, uint32_t length, const fs::path& filepath) {
  std::ofstream out(filepath, std::ios::out | std::ios::binary);

  if (!out) {
    return false;
  }

  try {
    std::copy_n(begin, length, std::ostreambuf_iterator<char>(out));
  } catch (...) {
    return false;
  }

  if (!out.good()) {
    return false;
  }

  out.close();
  return true;
}

bool saveAsOriginal(const RawFile& rawfile, const fs::path& filepath) {
  return saveDataToFile(rawfile.begin(), rawfile.size(), filepath);
}

bool saveAsOriginal(const VGMFile& file, const fs::path& filepath) {
  return saveDataToFile(file.rawFile()->begin() + file.dwOffset, file.unLength, filepath);
}
}  // namespace conversion
