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
                Type type = Type::Header);

  virtual bool load();
  virtual bool parseTrackPointers();
  virtual bool postLoad();

  VGMMultiSectionSeq *parentSeq;
  std::vector<SeqTrack *> aTracks;
};
