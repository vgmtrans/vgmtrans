#pragma once

#include "common.h"
#include "VGMItem.h"
#include "VGMFile.h"
#include "VGMMultiSectionSeq.h"
#include "SeqTrack.h"

class VGMMultiSectionSeq;

class VGMSeqSection
    : public VGMContainerItem {
 public:
  VGMSeqSection(VGMMultiSectionSeq *parentFile,
                uint32_t theOffset,
                uint32_t theLength = 0,
                const std::wstring theName = L"Section",
                EventColor color = CLR_HEADER);
  virtual ~VGMSeqSection(void);

  virtual bool Load();
  virtual bool GetTrackPointers();
  virtual bool PostLoad();

  VGMMultiSectionSeq *parentSeq;
  std::vector<SeqTrack *> aTracks;
};
