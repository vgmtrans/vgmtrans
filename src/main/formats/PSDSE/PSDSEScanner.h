#pragma once

#include <vector>
#include "Scanner.h"

class PSDSESampColl;
class PSDSEInstrSet;
class PSDSEPS2SampColl;
class PSDSEPS2InstrSet;
class Format;
class RawFile;

class PSDSEScanner : public VGMScanner {
public:
  PSDSEScanner(Format* format);
  virtual ~PSDSEScanner() = default;

  virtual void scan(RawFile* file, void* offset = nullptr) override;

  static void onSampCollClose(PSDSESampColl* sampColl);
  static void onInstrSetClose(PSDSEInstrSet* instrSet);
  static void onSampCollClose(PSDSEPS2SampColl* sampColl);
  static void onInstrSetClose(PSDSEPS2InstrSet* instrSet);

  static std::vector<PSDSESampColl*> g_loadedSampColls;
  static std::vector<PSDSEInstrSet*> g_loadedInstrSets;
  static std::vector<PSDSEPS2SampColl*> g_loadedPS2SampColls;
  static std::vector<PSDSEPS2InstrSet*> g_loadedPS2InstrSets;
};
