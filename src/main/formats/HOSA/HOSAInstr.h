#pragma once
#include "base/Types.h"
#include "HOSAFormat.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"



// *****************
// HOSAInstrSet
// *****************

class HOSAInstrSet
    : public VGMInstrSet {

 public:
  HOSAInstrSet(RawFile *file, u32 offset);
  ~HOSAInstrSet() override;

  bool parseHeader() override;
  bool parseInstrPointers() override;

 public:

  typedef struct _InstrHeader {
    char strHeader[8];
    u32 numInstr;
  } InstrHeader;

 public:
  InstrHeader instrheader{};
};


// **************
// HOSAInstr
// **************

class HOSAInstr
    : public VGMInstr {
 public:

  typedef struct _InstrInfo {
    u32 numRgns;
  } InstrInfo;

  typedef struct _RgnInfo {
    u32 sampOffset;
    u8 volume;           //percent volume 0-0xFF
    u8 note_range_high;
    u8 iSemiToneTune;    //unity key
    u8 iFineTune;        //unknown - definitely not finetune
    u8 ADSR_unk;         //the nibbles get read individually.  Conditional code related to this gets 0'd out in PSF file
                              //I disassembled (removed during optimization), so I can't see what it does. probably determines
                              //Sm and Sd, so not terribly important.
    u8 ADSR_Am;          // Determines ADSR Attack Mode value.
    u8 unk_A;
    u8 iPan;             //pan 0x80 - hard left    0xFF - hard right.  anything below results in center (but may be undefined)
    u32 ADSR_vals;       //The ordering is all messed up.  The code which loads these values is at 8007D8EC
  } RgnInfo;


 public:
  HOSAInstr(VGMInstrSet *instrSet, u32 offset, u32 length, u32 theBank, u32 theInstrNum);
  ~HOSAInstr() override { if (rgns) delete[] rgns; }
  bool loadInstr() override;

 public:
  InstrInfo instrinfo{};
  RgnInfo *rgns;
};
