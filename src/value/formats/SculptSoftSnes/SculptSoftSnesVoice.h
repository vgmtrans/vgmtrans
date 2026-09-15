/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/SculptSoftSnes/SculptSoftSnes.h"
#include "value/sequence/SequenceVm.h"

#include <memory>

namespace vgmtrans::formats::sculpt_soft_snes {

struct RuntimeConfig {
  std::shared_ptr<const DriverData> data;
  u32 frameMicroseconds;
  Revision revision;
};

struct EchoState {
  u8 mask = 0;
  std::optional<std::array<u8, 12>> preset;
};

struct VoiceState {
  VoiceState(core::TrackStateContext context, const RuntimeConfig& config)
      : data(config.data), frameSeconds(config.frameMicroseconds / 1000000.0), revision(config.revision),
        number(context.sourceTrackNumber) {}

  std::shared_ptr<const DriverData> data;
  double frameSeconds;
  Revision revision;
  u32 number;
  u8 sample = 0;
  u8 voiceVolume = 0;
  u8 fixedPan = 50;
  u8 fixedSample = 0;
  Patch patch;
  std::array<CurvePlayer, 4> curves;
  bool alternatePan = false;
  std::optional<core::PerformanceNoteId> note;
  double noteKey = 0;
  u16 voicePitch = 0;
  s16 envelope = 0;
  u8 gainRegister = 0;
  bool sounding = false;
  bool warnedPitchModulation = false;
  std::optional<u8> emittedSample;
  std::optional<std::array<s8, 2>> emittedBalance;
  std::optional<double> emittedExpression;
  std::optional<double> emittedPitch;
};

// The sequence revisions supply pitch, patch, volume and a gate in physical
// frames; the DSP-facing sample and envelope behavior is shared.
struct Voice {
  VoiceState& track;
  EchoState& echo;
  core::PerformanceEmitter& out;
  core::VmApi& vm;

  void warning(std::string message);
  [[nodiscard]] double key() const;
  void selectSample(u8 sample);
  void emitEcho();
  void emitVoice();
  void attack(u8 patchIndex, u8 volume, u16 gate, bool legato = false);
  void tick();
  void updateGainRegister();
  void silence();
};

}  // namespace vgmtrans::formats::sculpt_soft_snes
