/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include <spdlog/fmt/fmt.h>
#include <fstream>

#include "VGMExport.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"

namespace conversion {

bool SaveAsDLS(const VGMInstrSet &set, const std::string &filepath) {
  DLSFile dlsfile;

  if (set.assocColls.empty()) {
    return false;
  }

  if (set.assocColls.front()->CreateDLSFile(dlsfile)) {
    return dlsfile.SaveDLSFile(filepath);
  }
  return false;
}

bool SaveAsSF2(const VGMInstrSet &set, const std::string &filepath) {
  if (set.assocColls.empty()) {
    return false;
  }

  if (auto sf2file = set.assocColls.front()->CreateSF2File(); sf2file) {
    bool bResult = sf2file->SaveSF2File(filepath);
    delete sf2file;
    return bResult;
  }

  return false;
}



void SaveAllAsWav(const VGMSampColl &coll, const std::string &save_dir) {
  for (auto &sample : coll.samples) {
    auto path = fmt::format("{}/{} - {}.wav",
      save_dir, ConvertToSafeFileName(coll.name()), ConvertToSafeFileName(sample->name));
    sample->SaveAsWav(path);
  }
}


bool saveDataToFile(const char* begin, uint32_t length, const std::string& filepath) {
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

bool SaveAsOriginal(const RawFile& rawfile, const std::string& filepath) {
  return saveDataToFile(rawfile.begin(), rawfile.size(), filepath);
}

bool SaveAsOriginal(const VGMFile& file, const std::string& filepath) {
  return saveDataToFile(file.rawFile()->begin() + file.dwOffset, file.unLength, filepath);
}
}  // namespace conversion
