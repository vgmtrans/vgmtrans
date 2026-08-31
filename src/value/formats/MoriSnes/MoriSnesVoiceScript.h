/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/MoriSnes/MoriSnes.h"

#include <optional>
#include <vector>

namespace vgmtrans::formats::mori_snes {

struct DriverConfig {
  core::ByteReader data;
  DriverTraits traits;
  u16 presetTable = 0;
  u16 presetPitchHigh = 0;
  u16 panTable = 0;
};

struct VoiceScriptAnalysis {
  struct Point {
    u32 tick = 0;
    s32 pitch256 = 0;
    bool fineExplicit = false;
    u8 volume = 0xff;
  };

  struct Attack {
    u32 tick = 0;
    s32 pitch256 = 0;
    bool fineExplicit = false;
    u8 volume = 0xff;
    u8 pan = 0;
    bool panExplicit = false;
    std::optional<u32> keyOff;
  };

  u16 scriptAddress = 0;
  std::optional<u16> rowAddress;
  core::SourceRange scriptRange;
  std::optional<u8> releaseDelay;
  std::optional<u32> scriptEnd;
  s32 attackPitch256 = 0;
  bool attackFineExplicit = false;
  bool attackAbsolutePitch = false;
  u8 attackVolume = 0xff;
  u8 attackPan = 0;
  bool attackPanExplicit = false;
  std::optional<u32> cycleStart;
  std::optional<u32> cycleLength;
  std::optional<double> cyclePitchCenter256;
  bool cycleFineExplicit = false;
  std::optional<u8> cycleVolume;
  std::vector<Point> points;
  std::vector<Attack> attacks;
};

[[nodiscard]] VoiceScriptAnalysis analyzeVoiceScript(const DriverConfig& driver, u16 script,
                                                      std::optional<u8> percussionNote = std::nullopt);

}  // namespace vgmtrans::formats::mori_snes
