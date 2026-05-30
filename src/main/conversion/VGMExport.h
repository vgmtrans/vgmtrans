/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"
#include "ConversionContext.h"
#include "DLSConversion.h"
#include "DLSFile.h"
#include "Options.h"
#include "SF2Conversion.h"
#include "SF2File.h"
#include "util/Path.h"
#include "VGMColl.h"
#include "VGMSeq.h"

#include <filesystem>

/*
 * The following free functions implement
 * saving various formats to disk
 */

namespace conversion {

enum class Target : u32 { MIDI = 1u << 0u, DLS = 1u << 1u, SF2 = 1u << 2u };

inline constexpr Target operator|(Target a, Target b) {
  return static_cast<Target>(static_cast<u32>(a) | static_cast<u32>(b));
}

inline constexpr u32 operator&(Target a, Target b) {
  return static_cast<u32>(a) & static_cast<u32>(b);
}

inline constexpr bool hasTarget(Target options, Target target) {
  return (options & target) != 0;
}

inline constexpr SynthTarget modulationSynthTargetFor(Target options) {
  return hasTarget(options, Target::DLS)
      ? SynthTarget::DLS
      : SynthTarget::SoundFont;
}

bool saveAsDLS(VGMInstrSet &set, const std::filesystem::path &filepath);
bool saveAsSF2(VGMInstrSet &set, const std::filesystem::path &filepath);

void saveAllAsWav(const VGMSampColl &coll, const std::filesystem::path &save_dir);

bool saveAsOriginal(const VGMFile& file, const std::filesystem::path& filepath);
bool saveAsOriginal(const RawFile& rawfile, const std::filesystem::path& filepath);

template <Target options>
void saveAs(const VGMColl &coll, const std::filesystem::path &dir_path) {
  auto filename = makeSafeFileName(coll.name());
  auto filepath = dir_path / filename;
  const auto context = ConversionContext::fromOptions(ConversionOptions::the(), modulationSynthTargetFor(options));

  if constexpr (hasTarget(options, Target::MIDI)) {
    auto midiPath = filepath;
    midiPath.replace_extension(".mid");
    coll.seq()->saveAsMidi(midiPath, &coll, context);
  }

  if constexpr (hasTarget(options, Target::DLS)) {
    DLSFile dlsfile;
    if (createDLSFile(dlsfile, coll, context)) {
      auto dlsPath = filepath;
      dlsPath.replace_extension(".dls");
      dlsfile.saveDLSFile(dlsPath);
    }
  }

  if constexpr (hasTarget(options, Target::SF2)) {
    if (auto sf2file = createSF2File(coll, context)) {
      auto sf2Path = filepath;
      sf2Path.replace_extension(".sf2");
      sf2file->saveSF2File(sf2Path);
    }
  }
}
}
