#pragma once
#include "VGMSeqNoTrks.h"

class PS1Seq:
    public VGMSeqNoTrks {
 public:
  PS1Seq(RawFile *file, uint32_t offset);
  virtual ~PS1Seq();

  virtual bool parseHeader();
  virtual void resetVars();
  virtual bool readEvent();

 private:
  uint8_t runningStatus;
};
