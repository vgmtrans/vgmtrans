#pragma once
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "AkaoFormat.h"
#include "Matcher.h"

class AkaoInstrSet;

enum class AkaoPs1Version : uint8_t {
  // Final Fantasy 7
  // SaGa Frontier
  // Front Mission 2
  // Chocobo's Mysterious Dungeon
  // Parasite Eve
  VERSION_1,

  VERSION_2
};

static const unsigned short
    delta_time_table[] = {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x6, 0x3, 0x20, 0x10, 0x8, 0x4, 0x0, 0xA0A0, 0xA0A0};


class AkaoSeq:
    public VGMSeq {
 public:
  AkaoSeq(RawFile *file, uint32_t offset);
  virtual ~AkaoSeq(void);

  virtual bool GetHeaderInfo();
  virtual bool GetTrackPointers(void);

  uint8_t GetNumPositiveBits(uint32_t ulWord);

  static AkaoPs1Version GuessVersion(RawFile *file, uint32_t offset);

 public:
  AkaoInstrSet *instrset;
  uint16_t seq_id;
  AkaoPs1Version version;

  bool bUsesIndividualArts;
};

class AkaoTrack
    : public SeqTrack {
 public:
  AkaoTrack(AkaoSeq *parentFile, long offset = 0, long length = 0);

  virtual void ResetVars(void);
  virtual bool ReadEvent(void);

 public:


 protected:
  uint8_t relative_key, base_key;
  uint32_t current_delta_time;
  unsigned long loop_begin_loc[8];
  unsigned long loopID[8];
  int loop_counter[8];
  int loop_layer;
  int loop_begin_layer;
  bool bNotePlaying;
  std::vector<uint32_t> vCondJumpAddr;
};
