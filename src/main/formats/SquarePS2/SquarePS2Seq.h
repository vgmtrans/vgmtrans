#pragma once

#include "base/Types.h"
#include "SeqTrack.h"
#include "SquarePS2Format.h"
#include "VGMSeq.h"

class BGMSeq : public VGMSeq {
 public:
  BGMSeq(RawFile *file, u32 offset);
  ~BGMSeq() override = default;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  u32 id() const override { return assocWDID; }

 protected:
  unsigned short seqID;
  unsigned short assocWDID;
};


class BGMTrack : public SeqTrack {
 public:
  BGMTrack(BGMSeq *parentSeq, u32 offset = 0, u32 length = 0);

  bool readEvent() override;
};