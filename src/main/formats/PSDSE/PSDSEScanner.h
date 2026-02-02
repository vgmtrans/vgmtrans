#pragma once

#include <vector>
#include "Scanner.h"

class PSDSESampColl;
class PSDSEInstrSet;
class Format;
class RawFile;

class PSDSEScanner : public VGMScanner {
public:
  PSDSEScanner(Format *format);
  virtual ~PSDSEScanner() = default;

  virtual void scan(RawFile *file, void *offset = nullptr) override;

  void onSampCollClose(PSDSESampColl *sampColl);
  void onInstrSetClose(PSDSEInstrSet *instrSet);

  static std::vector<PSDSESampColl *> g_loadedSampColls;
  static std::vector<PSDSEInstrSet *> g_loadedInstrSets;
};
