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
  u8 m_runningStatus;
  u8 m_hasSetProgramForChannel[16]{};  // It seems the program assignment defaults to channel number,
                                       // (example: Umihara Kawase: Shun). We use these flags to track if
                                       // a default program change event is necessary for each channel
};
