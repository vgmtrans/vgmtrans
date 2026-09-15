/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SculptSoftSnes/SculptSoftSnesVoice.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace vgmtrans::formats::sculpt_soft_snes {
using namespace core;

namespace {
constexpr std::array<u8, 4> kCurveFlags{1, 4, 8, 2};  // Gain, pitch, pan, sample.
}

u16 Voice::curveValue(u8 lane) const {
  return track.revision == Revision::Late ? track.lateCurves[lane].value : track.curves[lane].value;
}

u8 Voice::gainTarget() const {
  const u8 gain = static_cast<u8>((track.patch.flags & 1) ? curveValue(0) : track.patch.gain);
  return track.revision == Revision::Late ? ((gain * 255) >> 8) * 255 >> 8 : gain;
}

void Voice::warning(std::string message) {
  vm.diagnostic(Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = vm.sourceRange()});
}

double Voice::key() const {
  if ((track.data->sampleFlags[track.sample] & 0x80) != 0) {
    return 72.0;
  }
  u16 pitch = track.voicePitch;
  if ((track.patch.flags & 4) != 0) {
    pitch = static_cast<u16>(pitch + curveValue(1) - 0x4b0);
  }
  pitch = static_cast<u16>(pitch + track.data->sampleTuning[track.sample]);
  if ((track.patch.flags & 0x40) != 0) {
    return 72.0 + 12.0 * std::log2(std::max(1u, unsigned((pitch + track.rawPitchOffset) & 0x3fff)) / 4096.0);
  }
  const unsigned topOctave = (track.revision == Revision::Extended || track.revision == Revision::Late) ? 10 : 4;
  if (topOctave == 10) {
    pitch = static_cast<u16>(pitch + 0x5a0);
  }
  // Clamp high octaves; negative words take the wrapped low-octave path.
  unsigned octave = topOctave;
  if (pitch < (topOctave + 1) * 240) {
    octave = pitch / 240;
  } else if (pitch >= 0x8000) {
    pitch = static_cast<u16>(pitch + 0xdf20);
    octave = 0;
  }
  // Preserve the driver's musical units without its integer table/shift rounding.
  const double key = track.data->pitchBaseKey - 12.0 * (topOctave - octave) + (pitch % 240) / 20.0;
  if (track.rawPitchOffset == 0) {
    return key;
  }
  const double dspPitch = 4096.0 * std::exp2((key - 72.0) / 12.0);
  const double shifted = std::fmod(dspPitch + track.rawPitchOffset, 16384.0);
  return 72.0 + 12.0 * std::log2(std::max(1.0, shifted) / 4096.0);
}

void Voice::selectSample(u8 sample) {
  track.sample = sample;
  if (track.emittedSample != sample) {
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = sample});
    track.emittedSample = sample;
  }
}

void Voice::emitEcho() {
  const u8 bit = static_cast<u8>(1u << track.dspVoice);
  echo.mask &= static_cast<u8>(~bit);
  if ((track.patch.flags & 0x10) != 0) {
    if (const auto& preset = track.data->echoes[track.patch.echo]) {
      echo.preset = preset;
      echo.mask |= bit;
    } else {
      warning("Invalid SculptSoftSnes echo preset");
    }
  }
  if (!echo.preset) {
    out.reverb(ReverbPerformanceEvent{.voiceMask = echo.mask, .send = 0.0});
    return;
  }
  const auto& preset = echo.preset;
  const auto gain = [&](u8 value) { return std::min<u8>(value, track.revision == Revision::Late ? 127 : 75) / 128.0; };
  const double left = gain((*preset)[1]);
  const double right = gain((*preset)[2]);
  out.reverb(ReverbPerformanceEvent{.voiceMask = echo.mask,
                                    .send = std::max(std::abs(left), std::abs(right)),
                                    .leftGain = left,
                                    .rightGain = right,
                                    .delayMilliseconds = ((*preset)[0] & 15) * 16.0,
                                    .feedback = static_cast<s8>((*preset)[3]) / 128.0});
}

void Voice::emitVoice() {
  const int basePan = static_cast<u8>((track.patch.flags & 8) ? curveValue(2) : track.fixedPan);
  const u8 pan = track.revision == Revision::Late ? std::clamp(basePan + track.panOffset, 0, 100) : basePan;
  const u8 left = static_cast<u8>((track.voiceVolume * pan) / 100);
  const u8 right = static_cast<u8>(track.voiceVolume - left);
  std::array<s8, 2> balance{static_cast<s8>(left), static_cast<s8>(right)};
  if (track.alternatePan) {
    std::swap(balance[0], balance[1]);
  }
  if (track.emittedBalance != balance) {
    out.stereoBalance(balance[0] / 128.0, balance[1] / 128.0);
    track.emittedBalance = balance;
  }
  const double expression = track.envelope / 2047.0;
  if (track.emittedExpression != expression) {
    out.expression(expression);
    track.emittedExpression = expression;
  }
  const double bend = key() - track.noteKey;
  if (track.emittedPitch != bend) {
    out.pitchBend(bend);
    track.emittedPitch = bend;
  }
}

void Voice::attack(u8 patchIndex, u8 volume, u16 gate, bool legato) {
  if (!track.data->patches[patchIndex]) {
    warning(fmt::format("Invalid SculptSoftSnes instrument {}", patchIndex));
    return;
  }
  track.patch = *track.data->patches[patchIndex];
  track.voiceVolume = std::min<u8>(volume, 127);
  const std::array<u8, 4> indices{track.patch.gain, track.patch.pitch, track.patch.pan, track.patch.sample};
  for (u8 lane = 0; lane < (legato ? 2 : 4); ++lane) {
    if ((track.patch.flags & kCurveFlags[lane]) == 0) {
      continue;
    }
    const auto& curve = track.data->curves[lane][indices[lane]];
    if (!curve) {
      warning(
          fmt::format("Invalid SculptSoftSnes envelope: patch {}, lane {}, index {}", patchIndex, lane, indices[lane]));
      track.patch.flags &= static_cast<u8>(~kCurveFlags[lane]);
    } else {
      // Sequence commands run before the frame's envelope updates.
      if (track.revision == Revision::Late) {
        track.lateCurves[lane].start(*curve, track.curveSpeeds[lane], lane == 1);
        track.lateCurves[lane].tick(false);
      } else {
        track.curves[lane].start(*curve, gate);
        track.curves[lane].tick();
      }
    }
  }
  if (!legato) {
    track.alternatePan =
        (track.patch.flags & 8) != 0 &&
        (track.revision == Revision::Late ? track.lateCurves[2].curve : track.curves[2].curve)->alternate &&
        !track.alternatePan;
    if ((track.patch.flags & 8) == 0) {
      track.fixedPan = track.patch.pan;
    }
    if ((track.patch.flags & 2) == 0) {
      track.fixedSample = track.patch.sample;
    }
  }
  const u8 sample = static_cast<u8>((track.patch.flags & 2) ? curveValue(3) : track.fixedSample);
  const bool retrigger =
      !legato && (!track.sounding || track.sample != sample || (track.data->sampleFlags[sample] & 0x40) != 0);
  if (retrigger && track.note) {
    out.setNoteEnd(*track.note, vm.tick());
  }
  if (!legato) {
    selectSample(sample);
  }
  if ((track.patch.flags & 0x20) != 0 && !track.warnedPitchModulation) {
    track.warnedPitchModulation = true;
    warning("SculptSoftSnes DSP pitch modulation is not representable by the current performance model");
  }
  if (retrigger) {
    // Keep sample tuning and fractional pitch in the bend when choosing an
    // integral MIDI key for the new voice.
    track.noteKey = std::clamp(std::round(key()), 0.0, 127.0);
  }
  track.released = false;
  if (legato) {
    updateGainRegister();
  } else {
    track.gainRegister = gainTarget();
    track.envelope = snesDspGainEnvelopeValue(track.gainRegister, 0, 0.0);
    emitEcho();
  }
  emitVoice();
  if (retrigger) {
    track.note = out.note(track.noteKey, 1.0, 1);
  }
  track.sounding = track.sounding || retrigger;
}

void Voice::tick() {
  if (!track.sounding) {
    return;
  }
  // DSP rate counters continue across the 20 ms software updates. Restarting
  // a rate on every frame would freeze slow release rates indefinitely.
  constexpr std::array<u32, 32> periods{30720, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256,
                                        192,   160,  128,  96,   80,   64,  48,  40,  32,  24,  20,
                                        16,    12,   10,   8,    6,    5,   4,   3,   2,   1};
  const u32 period = periods[track.gainRegister & 31];
  const auto samplesPerFrame = static_cast<u64>(std::llround(track.frameSeconds * kSnesDspSampleRate));
  const u64 clocks = (vm.tick() * samplesPerFrame) / period - ((vm.tick() - 1) * samplesPerFrame) / period;
  track.envelope =
      snesDspGainEnvelopeValue(track.gainRegister, track.envelope, (clocks * period + 0.001) / kSnesDspSampleRate);
  for (u8 lane = 0; lane < track.curves.size(); ++lane) {
    if ((track.patch.flags & kCurveFlags[lane]) != 0) {
      if (track.revision == Revision::Late) {
        track.lateCurves[lane].tick(track.released);
      } else {
        track.curves[lane].tick();
      }
    }
  }
  const u8 sample = static_cast<u8>(curveValue(3));
  if ((track.patch.flags & 2) != 0 && sample != track.sample) {
    out.setNoteEnd(*track.note, vm.tick());
    selectSample(sample);
    track.noteKey = std::clamp(std::round(key()), 0.0, 127.0);
    track.note = out.note(track.noteKey, 1.0, 1);
  }
  updateGainRegister();
  emitVoice();
  out.setNoteEnd(*track.note, vm.tick() + 1);
}

void Voice::updateGainRegister() {
  // Choose a linear GAIN rate from the distance to the next target.
  constexpr std::array<u8, 22> distances{255, 214, 160, 128, 107, 80, 64, 54, 40, 32, 27,
                                         20,  16,  14,  11,  8,   6,  5,  4,  3,  2,  1};
  constexpr std::array<u8, 22> rates{29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19,
                                     18, 17, 16, 15, 14, 13, 12, 10, 9,  6,  0};
  const int target = gainTarget();
  const int difference = target - (track.envelope >> 4);
  const unsigned distance = std::abs(difference) / 2;
  unsigned rateIndex = 21;
  while (rateIndex > 0 && distance >= distances[rateIndex]) {
    --rateIndex;
  }
  track.gainRegister =
      distance == 0 && target == 0 ? 0x82 : static_cast<u8>((difference < 0 ? 0x80 : 0xc0) | rates[rateIndex]);
}

void Voice::silence() {
  if (track.note) {
    out.setNoteEnd(*track.note, vm.tick());
  }
  track.note.reset();
  track.sounding = false;
}

}  // namespace vgmtrans::formats::sculpt_soft_snes
