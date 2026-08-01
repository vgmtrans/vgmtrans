/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"

#include <string>
#include <vector>

namespace vgmtrans::formats::nin_snes {

// Owns a masked signature so the few patterns patched with a detected direct-
// page address are just as easy to search as the static driver signatures.
class Pattern {
public:
  Pattern() = default;
  Pattern(const char* bytes, const char* mask, size_t size);

  [[nodiscard]] std::optional<u32> find(core::ByteReader reader) const;

private:
  std::vector<u8> bytes_;
  std::string mask_;
};

// The legacy and value scanners intentionally share the signature definitions
// from the shared NinSnesScannerPatterns.inc. Keeping one catalog prevents the two
// architectures from silently recognizing different engine permutations.
struct Patterns {
  static Pattern makeInitSectionPtrPattern(u8 sectionPointer);
  static Pattern makeInitSectionPtrYIPattern(u8 sectionPointer);
  static Pattern makeInitSectionPtrSMWPattern(u8 sectionPointer);
  static Pattern makeInitSectionPtrGD3Pattern(u8 sectionPointer);
  static Pattern makeInitSectionPtrYSFRPattern(u8 sectionPointer);
  static Pattern makeInitSectionPtrTSPattern(u8 sectionPointer);
  static Pattern makeInitSectionPtrYs4Pattern(u8 sectionPointer);
  static Pattern makeInitSongListPtrYSFRPattern(u8 songListPointer);

  static Pattern ptnBranchForVcmd;
  static Pattern ptnBranchForVcmdReadahead;
  static Pattern ptnJumpToVcmd;
  static Pattern ptnJumpToVcmdSMW;
  static Pattern ptnReadVcmdLengthSMW;
  static Pattern ptnDispatchNoteYI;
  static Pattern ptnIncSectionPtr;
  static Pattern ptnLoadInstrTableAddress;
  static Pattern ptnLoadInstrTableAddressSMW;
  // Value-only behavioral probes; these do not participate in legacy format recognition.
  static Pattern ptnEarlierPercussionTable;
  static Pattern ptnKonamiPercussionDispatch;
  static Pattern ptnReadSongRequestPort;
  static Pattern ptnFixedPercussionBaseDispatch;
  static Pattern ptnFixedPercussionBaseLoader;
  static Pattern ptnSetDIR;
  static Pattern ptnSetDIRYI;
  static Pattern ptnSetDIRVS;
  static Pattern ptnSetDIRSMW;

  static Pattern ptnIncSectionPtrGD3;
  static Pattern ptnIncSectionPtrYSFR;
  static Pattern ptnIncSectionPtrYs4;
  static Pattern ptnInitSectionPtrHE4;
  static Pattern ptnJumpToVcmdCTOW;
  static Pattern ptnJumpToVcmdYSFR;
  static Pattern ptnJumpToVcmdYs4;
  static Pattern ptnReadVcmdLengthYSFR;
  static Pattern ptnReadVcmdLengthYs4;
  static Pattern ptnDispatchNoteGD3;
  static Pattern ptnDispatchNoteYSFR;
  static Pattern ptnDispatchNoteLEM;
  static Pattern ptnDispatchNoteFE3;
  static Pattern ptnDispatchNoteFE4;
  static Pattern ptnDispatchNoteYs4;
  static Pattern ptnWriteVolumeKSS;
  static Pattern ptnRD1VCmd_FA_FE;
  static Pattern ptnRD2VCmdInstrADSR;
  static Pattern ptnIntelliVCmdFA;
  static Pattern ptnInstrVCmdGD3;
  static Pattern ptnLoadInstrTableAddressSOS;
  static Pattern ptnLoadInstrTableAddressCTOW;
  static Pattern ptnLoadInstrTableAddressYSFR;
  static Pattern ptnSetDIRCTOW;
  static Pattern ptnSetDIRTS;
  static Pattern ptnInstrVCmdACTR;
  static Pattern ptnInstrVCmdACTR2;
  static Pattern ptnInstrVCmdTS;
};

[[nodiscard]] std::optional<u8> detectFixedPercussionBase(core::ByteReader reader, u8 percussionMinimum);

}  // namespace vgmtrans::formats::nin_snes
