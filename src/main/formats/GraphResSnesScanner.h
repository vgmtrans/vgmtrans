#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class GraphResSnesScanner:
    public VGMScanner {
 public:
  GraphResSnesScanner(void) {
    USE_EXTENSION("spc");
  }
  virtual ~GraphResSnesScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForGraphResSnesFromARAM(RawFile *file);
  void SearchForGraphResSnesFromROM(RawFile *file);

 private:
  std::map<uint8_t, uint8_t> GetInitDspRegMap(RawFile *file);

  static BytePattern ptnLoadSeq;
  static BytePattern ptnDspRegInit;
};
