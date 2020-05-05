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

  static AkaoPs1Version GuessVersion(RawFile *file, uint32_t offset);
  static uint32_t ReadNumOfTracks(RawFile *file, uint32_t offset);

private:
  static uint8_t GetNumPositiveBits(uint32_t ulWord) {
    return   ((ulWord & 0x80000000)>0) + ((ulWord & 0x40000000)>0) + ((ulWord & 0x20000000)>0) + ((ulWord & 0x10000000)>0)
      + ((ulWord & 0x8000000)>0) + ((ulWord & 0x4000000)>0) + ((ulWord & 0x2000000)>0) + ((ulWord & 0x1000000)>0)
      + ((ulWord & 0x800000)>0) + ((ulWord & 0x400000)>0) + ((ulWord & 0x200000)>0) + ((ulWord & 0x100000)>0)
      + ((ulWord & 0x80000)>0) + ((ulWord & 0x40000)>0) + ((ulWord & 0x20000)>0) + ((ulWord & 0x10000)>0)
      + ((ulWord & 0x8000)>0) + ((ulWord & 0x4000)>0) + ((ulWord & 0x2000)>0) + ((ulWord & 0x1000)>0)
      + ((ulWord & 0x800)>0) + ((ulWord & 0x400)>0) + ((ulWord & 0x200)>0) + ((ulWord & 0x100)>0)
      + ((ulWord & 0x80)>0) + ((ulWord & 0x40)>0) + ((ulWord & 0x20)>0) + ((ulWord & 0x10)>0)
      + ((ulWord & 0x8)>0) + ((ulWord & 0x4)>0) + ((ulWord & 0x2)>0) + ((ulWord & 0x1));
  }

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
