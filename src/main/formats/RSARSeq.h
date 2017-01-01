#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "RSARFormat.h"

#include <stack>

class RSARTrack : public SeqTrack {
public:
  RSARTrack(VGMSeq *parentFile, uint32_t offset = 0, uint32_t length = 0) :
    SeqTrack(parentFile, offset, length) {}

private:
  bool noteWait;
  struct StackFrame {
    uint32_t retOffset;
    uint32_t loopCount;
  };
  std::stack<StackFrame> callStack;

  enum ArgType {
    ARG_DEFAULT,
    ARG_VAR,
    ARG_U8,
    ARG_RANDOM,
  };
  ArgType nextArgType = ARG_DEFAULT;

  uint32_t Read24BE(uint32_t &offset) {
    uint32_t val = (GetWordBE(offset - 1) & 0x00FFFFFF);
    offset += 3;
    return val;
  }

  uint32_t ReadArg(ArgType defaultArgType, uint32_t &offset);
  virtual bool ReadEvent() override;
};

class RSARSeq : public VGMSeq {
public:
  RSARSeq(RawFile *file, uint32_t offset, uint32_t pMasterTrackOffset, uint32_t length, std::wstring name) :
    VGMSeq(RSARFormat::name, file, offset, length, name), masterTrackOffset(pMasterTrackOffset) {}

private:
  uint32_t allocTrack;
  uint32_t masterTrackOffset;

  virtual bool GetHeaderInfo() override;
  virtual bool GetTrackPointers() override;
};
