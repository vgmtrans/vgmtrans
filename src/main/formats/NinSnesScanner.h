/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Scanner.h"
#include "BytePattern.h"

class NinSnesScanner:
    public VGMScanner {
 public:
  NinSnesScanner(void) {
    USE_EXTENSION(L"spc");
  }
  virtual ~NinSnesScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForNinSnesFromARAM(RawFile *file);
  void SearchForNinSnesFromROM(RawFile *file);

 private:
  static BytePattern ptnBranchForVcmd;
  static BytePattern ptnBranchForVcmdReadahead;
  static BytePattern ptnJumpToVcmd;
  static BytePattern ptnJumpToVcmdSMW;
  static BytePattern ptnReadVcmdLengthSMW;
  static BytePattern ptnDispatchNoteYI;
  static BytePattern ptnIncSectionPtr;
  static BytePattern ptnLoadInstrTableAddress;
  static BytePattern ptnLoadInstrTableAddressSMW;
  static BytePattern ptnSetDIR;
  static BytePattern ptnSetDIRYI;
  static BytePattern ptnSetDIRSMW;

  // PATTERNS FOR DERIVED VERSIONS
  static BytePattern ptnIncSectionPtrGD3;
  static BytePattern ptnIncSectionPtrYSFR;
  static BytePattern ptnIncSectionPtrYs4;
  static BytePattern ptnInitSectionPtrHE4;
  static BytePattern ptnJumpToVcmdCTOW;
  static BytePattern ptnJumpToVcmdYSFR;
  static BytePattern ptnJumpToVcmdYs4;
  static BytePattern ptnReadVcmdLengthYSFR;
  static BytePattern ptnReadVcmdLengthYs4;
  static BytePattern ptnDispatchNoteGD3;
  static BytePattern ptnDispatchNoteYSFR;
  static BytePattern ptnDispatchNoteLEM;
  static BytePattern ptnDispatchNoteFE3;
  static BytePattern ptnDispatchNoteFE4;
  static BytePattern ptnDispatchNoteYs4;
  static BytePattern ptnWriteVolumeKSS;
  static BytePattern ptnRD1VCmd_FA_FE;
  static BytePattern ptnRD2VCmdInstrADSR;
  static BytePattern ptnIntelliVCmdFA;
  static BytePattern ptnInstrVCmdGD3;
  static BytePattern ptnLoadInstrTableAddressSOS;
  static BytePattern ptnLoadInstrTableAddressCTOW;
  static BytePattern ptnLoadInstrTableAddressYSFR;
  static BytePattern ptnSetDIRCTOW;
  static BytePattern ptnSetDIRTS;
  static BytePattern ptnInstrVCmdACTR;
  static BytePattern ptnInstrVCmdACTR2;
  static BytePattern ptnInstrVCmdTS;
};
