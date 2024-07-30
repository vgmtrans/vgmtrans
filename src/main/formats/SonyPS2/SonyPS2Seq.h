#pragma once
#include "VGMSeqNoTrks.h"
#include "SeqTrack.h"
#include "SonyPS2Format.h"

#define COMP_KEYON2POLY        1        //compression mode

class SonyPS2Seq:
    public VGMSeqNoTrks {
 public:
  typedef struct _HdrCk {
    uint32_t Creator;
    uint32_t Type;
    uint32_t chunkSize;
    uint32_t fileSize;
    uint32_t songChunkAddr;
    uint32_t midiChunkAddr;
    uint32_t seSequenceChunkAddr;
    uint32_t seSongChunkAddr;
  } HdrCk;

  SonyPS2Seq(RawFile *file, uint32_t offset, std::string name);
  ~SonyPS2Seq() override = default;

  bool parseHeader() override;
  bool readEvent() override;
  uint8_t getDataByte(uint32_t offset);

 private:
  VersCk versCk;
  HdrCk hdrCk;
  uint32_t midiChunkSize;
  uint32_t maxMidiNumber;   //this determines the number of midi blocks.  Seems very similar to SMF Type 2 (blech!)
                            //for the moment, I'm going to assume the maxMidiNumber is always 0 (meaning one midi data block)
  uint32_t midiOffsetAddr;  //the offset for midi data block 0.  I won't bother with > 0 data blocks for the time being
  uint16_t compOption;      //determines compression mode for midi data block 0

  bool bSkipDeltaTime;
  uint8_t runningStatus;
};

