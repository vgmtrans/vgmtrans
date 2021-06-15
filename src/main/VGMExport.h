/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "common.h"
#include "DLSFile.h"
#include "MidiFile.h"
#include "SF2File.h"
#include "VGMColl.h"
#include "VGMSeq.h"

/*
 * The following free functions implement
 * saving various formats to disk
 */

enum class VGMCollConversionTarget : uint32_t { MIDI = 1u << 0u, DLS = 1u << 1u, SF2 = 1u << 2u };

inline constexpr VGMCollConversionTarget operator|(VGMCollConversionTarget a, VGMCollConversionTarget b) {
  return static_cast<VGMCollConversionTarget>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr uint32_t operator&(VGMCollConversionTarget a, VGMCollConversionTarget b) {
  return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}

template <VGMCollConversionTarget options>
void SaveAs(VGMColl &coll, const std::wstring &dir_path) {
  auto filename = ConvertToSafeFileName(*coll.GetName());
  auto filepath = dir_path + L"/" + filename;

  if constexpr ((options & VGMCollConversionTarget::MIDI) != 0) {
    coll.seq->SaveAsMidi(filepath + L".mid");
  }

  if constexpr ((options & VGMCollConversionTarget::DLS) != 0) {
    DLSFile dlsfile;
    if (coll.CreateDLSFile(dlsfile)) {
      dlsfile.SaveDLSFile(filepath + L".dls");
    }
  }

  if constexpr ((options & VGMCollConversionTarget::SF2) != 0) {
    SF2File *sf2file = coll.CreateSF2File();
    if (sf2file) {
      sf2file->SaveSF2File(filepath + L".sf2");
    }
  }
}