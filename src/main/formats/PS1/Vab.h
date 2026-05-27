#pragma once

#include "util/SizeOffsetPair.h"
#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "VGMSamp.h"

// VAB Header
struct VabHdr {
  s32 form;
  /*always "VABp"*/
  s32 ver;
  /*format version number*/
  s32 id;
  /*bank ID*/
  u32 fsize;
  /*file size*/
  u16 reserved0;
  /*system reserved*/
  u16 ps;
  /*total number of programs in this bank*/
  u16 ts;
  /*total number of effective tones*/
  u16 vs;
  /*number of waveforms (VAG)*/
  u8 mvol;
  /*master volume*/
  u8 pan;
  /*master pan*/
  u8 attr1;
  /*bank attribute*/
  u8 attr2;
  /*bank attribute*/
  u32 reserved1; /*system reserved*/
};


//Program Attributes
struct ProgAtr {
  u8 tones;
  /*number of effective tones which compose the program*/
  u8 mvol;
  /*program volume*/
  u8 prior;
  /*program priority*/
  u8 mode;
  /*program mode*/
  u8 mpan;
  /*program pan*/
  s8 reserved0;
  /*system reserved*/
  s16 attr;
  /*program attribute*/
  u32 reserved1;
  /*system reserved*/
  u32 reserved2; /*system reserved*/
};


//Tone Attributes
struct VagAtr {
  u8 prior;
  /*tone priority (0 - 127); used for controlling allocation when more voices than can be keyed on are requested*/
  u8 mode;
  /*tone mode (0 = normal; 4 = reverb applied */
  u8 vol;
  /*tone volume*/
  u8 pan;
  /*tone pan*/
  u8 center;
  /*center note (0~127)*/
  u8 shift;
  /*pitch correction (0~127,cent units)*/
  u8 min;
  /*minimum note limit (0~127)*/
  u8 max;
  /*maximum note limit (0~127, provided min < max)*/
  u8 vibW;
  /*vibrato width (1/128 rate,0~127)*/
  u8 vibT;
  /*1 cycle time of vibrato (tick units)*/
  u8 porW;
  /*portamento width (1/128 rate, 0~127)*/
  u8 porT;
  /*portamento holding time (tick units)*/
  u8 pbmin;
  /*pitch bend (-0~127, 127 = 1 octave)*/
  u8 pbmax;
  /*pitch bend (+0~127, 127 = 1 octave)*/
  u8 reserved1;
  /*system reserved*/
  u8 reserved2;
  /*system reserved*/
  u16 adsr1;
  /*ADSR1*/
  u16 adsr2;
  /*ADSR2*/
  s16 prog;
  /*parent program*/
  s16 vag;
  /*waveform (VAG) used*/
  s16 reserved[4]; /*system reserved*/
};


class Vab:
    public VGMInstrSet {
 public:
  Vab(RawFile *file, u32 offset);
  virtual ~Vab();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();
  bool isViableSampCollMatch(VGMSampColl* sampColl) override;

 public:
  VabHdr hdr;

private:
  std::vector<SizeOffsetPair> m_vagLocations;
};


// ********
// VabInstr
// ********

class VabInstr
    : public VGMInstr {
 public:
  VabInstr(VGMInstrSet *instrSet,
           u32 offset,
           u32 length,
           u32 theBank,
           u32 theInstrNum,
           const std::string &name = "Instrument");
  virtual ~VabInstr();

  virtual bool loadInstr();

 public:
  ProgAtr attr;
  u8 masterVol;
};


// ******
// VabRgn
// ******

class VabRgn
    : public VGMRgn {
 public:
  VabRgn(VabInstr *instr, u32 offset);

  virtual bool loadRgn();

 public:
  u16 ADSR1;                //raw ps2 ADSR1 value (articulation data)
  u16 ADSR2;                //raw ps2 ADSR2 value (articulation data)
  u8 bStereoRegion;
  u8 StereoPairOrder;
  u8 bFirstRegion;
  u8 bLastRegion;
  u8 bUnknownFlag2;
  u32 sample_offset;

  VagAtr attr;
};
