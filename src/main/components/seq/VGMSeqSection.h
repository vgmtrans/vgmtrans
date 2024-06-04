#pragma once

#include "VGMItem.h"
#include "SeqTrack.h"

class VGMMultiSectionSeq;

class VGMSeqSection
    : public VGMItem {
 public:
  VGMSeqSection(VGMMultiSectionSeq *parentFile,
                uint32_t theOffset,
                uint32_t theLength = 0,
                const std::string& name = "Section",
                EventColor color = CLR_HEADER);

  virtual bool Load();
  virtual bool GetTrackPointers();
  virtual bool PostLoad();

  VGMMultiSectionSeq *parentSeq;
  std::vector<SeqTrack *> aTracks;
};
