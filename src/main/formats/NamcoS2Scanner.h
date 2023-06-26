#pragma once
#include "Scanner.h"

class MAMERomGroup;
class NamcoS2C140InstrSet;
class NamcoS2SampColl;
class NamcoS2Seq;

typedef map<uint32_t, uint32_t> InstrMap;

class NamcoS2Scanner:
    public VGMScanner {
 public:
  virtual void Scan(RawFile *file, void *info = 0);

 private:
  vector<NamcoS2Seq*> LoadSequences(
      MAMERomGroup* cpuRomGroup,
      uint32_t c140BankOffset,
      uint32_t ym2151BankOffset,
      shared_ptr<InstrMap> instrMap
  );
  NamcoS2SampColl* LoadSampColl(
      MAMERomGroup* cpuRomGroup,
      MAMERomGroup* pcmRomGroup,
      uint32_t c140BankOffset
  );
  NamcoS2C140InstrSet* LoadInstruments(
      MAMERomGroup* cpuRomGroup,
      uint32_t c140BankOffset,
      NamcoS2SampColl* sampColl,
      uint32_t ym2151BankOffset,
      shared_ptr<InstrMap> instrMap
  );

};
