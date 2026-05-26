#pragma once

#include "util/types.h"
#include "VGMSeqNoTrks.h"
#include "SeqTrack.h"
#include "SonyPS2Format.h"

#define COMP_KEYON2POLY        1        //compression mode

class SonyPS2Seq:
    public VGMSeqNoTrks {
 public:
  typedef struct _HdrCk {
    u32 Creator;
    u32 Type;
    u32 chunkSize;
    u32 fileSize;
    u32 songChunkAddr;
    u32 midiChunkAddr;
    u32 seSequenceChunkAddr;
    u32 seSongChunkAddr;
  } HdrCk;

  SonyPS2Seq(RawFile *file, u32 offset, std::string name);
  ~SonyPS2Seq() override = default;

  bool parseHeader() override;
  bool readEvent() override;
  u8 getDataByte(u32 offset);

 private:
  VersCk versCk;
  HdrCk hdrCk;
  u32 midiChunkSize;
  u32 maxMidiNumber;   //this determines the number of midi blocks.  Seems very similar to SMF Type 2 (blech!)
                            //for the moment, I'm going to assume the maxMidiNumber is always 0 (meaning one midi data block)
  u32 midiOffsetAddr;  //the offset for midi data block 0.  I won't bother with > 0 data blocks for the time being
  u16 compOption;      //determines compression mode for midi data block 0

  bool bSkipDeltaTime;
  u8 runningStatus;
};

