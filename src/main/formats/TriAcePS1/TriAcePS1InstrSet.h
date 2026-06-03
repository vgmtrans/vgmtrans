#pragma once
#include "base/Types.h"
#include "TriAcePS1Format.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"

// *****************
// TriAcePS1InstrSet
// *****************

class TriAcePS1InstrSet
    : public VGMInstrSet {

 public:
  TriAcePS1InstrSet(RawFile *file, u32 offset);
  virtual ~TriAcePS1InstrSet();

  virtual bool parseHeader();
  virtual bool parseInstrPointers();

 public:

  //-----------------------
  //1,Sep.2009 revise		to do こうしたい。
  //-----------------------
  typedef struct _InstrHeader {
    u32 FileSize;    //
    u16 InstSize;    //End of Instruction information
    u16 unk_06;        //
    u16 unk_08;        //
    u16 unk_0A;        //
  } InstrHeader;
  //	↑　↑　↑
  u16 instrSectionSize;        // to do delete
  u8 numInstrs;                // to do delete
  //-----------------------

  //u32 dwSampSectOffset;
  //u32 dwSampSectSize;
  //u32 dwNumInstrs;
  //u32 dwTotalRegions;
};


// **************
// TriAcePS1Instr
// **************

class TriAcePS1Instr
    : public VGMInstr {
 public:

  typedef struct _RgnInfo {
    u8 note_range_low;        //These ranges only seem to kick in when the instr has more than 1 rgn
    u8 note_range_high;
    u8 vel_range_low;
    u8 vel_range_high;
    u32 sampOffset;
    u32 loopOffset;
    u8 attenuation;
    s8 pitchTuneSemitones;
    s8 pitchTuneFine;
    u8 unk_17;
    u32 unk_18;
  } RgnInfo;

  typedef struct _InstrInfo {
    u8 progNum;
    u8 bankNum;
    u16 ADSR1;
    u16 ADSR2;
    u8 unk_06;
    u8 numRgns;
  } InstrInfo;


 public:
  TriAcePS1Instr(VGMInstrSet *instrSet, u32 offset, u32 length, u32 bank, u32 instrNum);
  ~TriAcePS1Instr() { if (rgns) delete[] rgns; }
  virtual bool loadInstr();

 public:
  InstrInfo instrinfo;
  RgnInfo *rgns;
};
