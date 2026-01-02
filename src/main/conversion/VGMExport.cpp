/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include <spdlog/fmt/fmt.h>
#include <fstream>

#include "VGMExport.h"
#include "helper.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMColl.h"
#include "SynthFile.h"

namespace conversion {

bool saveAsDLS(VGMInstrSet &set, const std::string &filepath) {
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

bool saveAsSF2(VGMInstrSet &set, const std::string &filepath) {
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

bool saveAsSF2(const VGMColl &coll, const std::string &filepath) {
  if (auto sf2file = createSF2File(coll.instrSets(), coll.sampColls(), &coll); sf2file) {
    bool bResult = sf2file->saveSF2File(filepath);
    delete sf2file;
    return bResult;
  }

  return false;
}

void saveAllAsWav(const VGMSampColl &coll, const std::string &save_dir) {
  for (auto &sample : coll.samples) {
    auto path = fmt::format("{}/{} - {}.wav",
      save_dir, ConvertToSafeFileName(coll.name()), ConvertToSafeFileName(sample->name()));
    sample->saveAsWav(path);
  }
}

bool saveDataToFile(const char* begin, uint32_t length, const std::string& filepath) {
  std::ofstream out(pathFromUtf8(filepath), std::ios::out | std::ios::binary);

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

bool saveAsOriginal(const RawFile& rawfile, const std::string& filepath) {
  return saveDataToFile(rawfile.begin(), rawfile.size(), filepath);
}

bool saveAsOriginal(const VGMFile& file, const std::string& filepath) {
  return saveDataToFile(file.rawFile()->begin() + file.dwOffset, file.unLength, filepath);
}
}  // namespace conversion
