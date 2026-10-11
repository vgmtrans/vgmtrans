/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OceanSnes/OceanSnesVoice.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace vgmtrans::formats::ocean_snes {
using namespace core;

u8 Voice::noteIndex(u8 encodedNote, u8 transpose, bool clampNegative) const {
  const u8 index = static_cast<u8>(((encodedNote - 1) & 0x7f) + region->transpose + instrumentTranspose + transpose);
  return clampNegative && (index & 0x80) ? 0 : index & 0x7f;
}

u16 Voice::pitchAt(const RuntimeConfig& config, u8 index) const {
  const u16 table = config.source.reader().le16(config.layout.pitchTable + index * 2u);
  return static_cast<u16>((u32(table) * tuning) >> 8);
}

u8 Voice::sensitivityAt(const RuntimeConfig& config, u8 index) const {
  const auto reader = config.source.reader();
  const u32 at = config.layout.pitchTable + index * 2u;
  return std::max(1, static_cast<u8>(reader.le16(at + 2) - reader.le16(at)) >> 3);
}

void Voice::start(const RuntimeConfig& config, u8 program, u8 encodedNote, u8 randomPan, PerformanceEmitter& out,
                  VmApi& vm) {
  const auto& selected = config.data->instruments[program & 0x7f];
  if (!selected && !config.data->retainedInstruments.test(program & 0x7f)) {
    vm.diagnostic({.severity = Severity::Warning,
                   .message = fmt::format("OceanSnes instrument {} is invalid; suppressing its note", program & 0x7f),
                   .range = vm.sourceRange()});
    release(out);
    return;
  }
  if (selected) {
    instrumentVolume = selected->volume;
    instrumentTranspose = selected->transpose;
    const auto found = std::ranges::find_if(
        selected->regions, [&](const InstrumentRegion& r) { return r.low <= encodedNote && encodedNote <= r.high; });
    if (found != selected->regions.end()) {
      region = &*found;
      instrument = program & 0x7f;
      regionVolume = region->volume;
      vibratoDelay = region->vibratoDelay;
      vibratoAmount = region->vibratoDepth;
      vibratoRate = region->vibratoRate;
      lfoTarget = region->lfo;
      if (region->pan != 0x81) {
        pan = (pan & 0xff) | (u16(region->pan == 0x80 ? randomPan & 0x7f : region->pan) << 8);
        surround = region->surround;
      }
    }
  }
  // A region miss installs the new header but retains the previous region.
  if (!region || !config.data->samples[region->sample]) {
    vm.diagnostic({.severity = Severity::Warning,
                   .message = "OceanSnes note has no playable sample region",
                   .range = vm.sourceRange()});
    release(out);
    return;
  }
  release(out);
  if (note.valid()) {
    // Preparing a new KON asserts KOFF first. A BE release, in contrast,
    // leaves the voice running with the region's exponential GAIN envelope.
    out.silence();
  }
  tuning = static_cast<u16>(config.data->samples[region->sample]->tuning + region->tuning);
  pendingNote = encodedNote;
  pendingOutput.emplace(out.after(2));
  calculatePitch = (encodedNote & 0x7f) != 0;
  keyOnDelay = 2;
  eventVolume = 0xff;
  bend = 0;
  lfoPitch = 0;
  volumeStep = 0;
  panTicks = 0;
  slideTicks = 0;
  vibratoPhase = 0;
  automaticRelease.reset();
  volumeDirty = true;
}

void Voice::release(PerformanceEmitter& out) {
  if (sounding) {
    out.noteOff(noteKey);
    sounding = false;
  }
  automaticRelease.reset();
  calculatePitch = false;
}

void Voice::portamento(const RuntimeConfig& config, u8 encodedNote, u8 duration, u8 transpose) {
  if (!region) {
    return;
  }
  const u8 index = noteIndex(encodedNote, transpose, config.layout.version != Version::JurassicPark2);
  slideTarget = pitchAt(config, index);
  slideDepth = sensitivityAt(config, index) * vibratoAmount;
  // Duration zero leaves the updater idle, even though the SPC executes DIV.
  slideTicks = duration;
  if (duration != 0) {
    const s16 difference = static_cast<s16>(slideTarget - pitch);
    slideStep = (std::abs(int(difference)) * 256 / duration) * (difference < 0 ? -1 : 1);
  }
}

void Voice::panTo(u8 target, u8 logicalSlot) {
  // C1's voice lookup overwrites D0's requested duration with 2*track.
  panTicks = logicalSlot;
  if (panTicks != 0) {
    panStep = static_cast<u16>((int(target) - int(pan >> 8)) * 256 / panTicks);
    pan &= 0xff00;
  }
}

void Voice::changeVibratoDepth(u8 delta) {
  vibratoAmount = static_cast<u8>(vibratoAmount + delta);
  vibratoDepth = sensitivity * vibratoAmount;
}

void Voice::tick(const RuntimeConfig& config, u8 transpose, u8 masterVolume, u8 musicVolume, u8 outputMode,
                 PerformanceEmitter& out) {
  if (!region) {
    return;
  }
  // New pitches are calculated after all music tracks have run, so a global
  // transpose issued by a later track also affects this frame's earlier notes.
  if (std::exchange(calculatePitch, false)) {
    const u8 index = noteIndex(pendingNote, transpose);
    pitch = pitchAt(config, index);
    sensitivity = sensitivityAt(config, index);
    bendUnit = static_cast<u8>(sensitivity * region->bendScale);
    vibratoDepth = sensitivity * vibratoAmount;
  }
  if (keyOnDelay != 0 && --keyOnDelay == 0) {
    auto& noteOutput = *pendingOutput;
    noteOutput.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = instrument});
    // Retained regions can be outside the new note's range. Choose a key in
    // that region and express the actual hardware pitch independently below.
    noteKey =
        std::clamp<int>(pendingNote, std::max<int>(region->low, 0x80), std::min<int>(region->high, 0xbd)) - 0x81 + 24;
    note = noteOutput.noteOn(noteKey, 1.0);
    pendingOutput.reset();
    sounding = true;
    emittedBend.reset();
    volumeDirty = true;
    // Threshold zero can never pass the driver's strict ENVX < threshold test.
    if (region->sustainTicks != 0 && region->sustain != 0) {
      double seconds = snesDspGainPhysicalSeconds(region->attack | 0xc0, 0, 0x7ff);
      if (region->sustain != 0x7f) {
        const s16 threshold = std::max(0, int(region->sustain) * 16 - 1);
        seconds += snesDspGainPhysicalSeconds(region->decay | 0xa0, 0x7ff, threshold);
      }
      if (std::isfinite(seconds)) {
        automaticRelease = static_cast<u64>(std::ceil(seconds / kTickSeconds)) + region->sustainTicks - 1;
      }
    }
  } else if (automaticRelease && sounding) {
    if (*automaticRelease == 0 || --*automaticRelease == 0) {
      release(out);
    }
  }
  if (panTicks != 0) {
    pan = static_cast<u16>(pan + panStep);
    --panTicks;
    volumeDirty = true;
  }
  bend = std::clamp<int>(bend + bendStep, -128, 127);
  if (slideTicks != 0) {
    if (--slideTicks == 0) {
      pitch = slideTarget;
      pitchFraction = 0;
      vibratoDepth = slideDepth;
    } else {
      const u32 value = static_cast<u32>((u32(pitch) << 8) + pitchFraction + slideStep) & 0xffffff;
      pitch = static_cast<u16>(value >> 8);
      pitchFraction = static_cast<u8>(value);
    }
  }
  if (volumeStep != 0) {
    const unsigned sum = eventVolume + static_cast<u8>(volumeStep);
    eventVolume = static_cast<u8>(sum);
    if (sum > 255 && volumeStep > 0) {
      eventVolume = 255;
      volumeStep = 0;
    }
    volumeDirty = true;
  }
  int vibrato = 0;
  if (vibratoDelay != 0) {
    --vibratoDelay;
  } else {
    const s8 wave = config.source.reader().s8At(config.layout.sineTable + vibratoPhase);
    vibrato = ((std::abs(int(wave)) * vibratoDepth) & 0xffff) >> 5;
    if (wave < 0) {
      vibrato = -vibrato;
    }
    vibratoPhase = static_cast<u8>(vibratoPhase + vibratoRate);
  }
  const u16 beforeBend = static_cast<u16>(pitch + vibrato);
  u16 finalPitch = static_cast<u16>(beforeBend + bend * bendUnit);
  if (!(beforeBend & 0x8000) && (finalPitch & 0x8000)) {
    finalPitch = 0;
  }
  finalPitch = static_cast<u16>(finalPitch + lfoPitch) & 0x3fff;
  // DSP pitch zero freezes the sample. MIDI has no zero-frequency note;
  // silence that interval instead of playing at the minimum nonzero pitch.
  if (note.valid() && muted != (finalPitch == 0)) {
    muted = finalPitch == 0;
    out.expression(muted ? 0.0 : 1.0);
  }
  const double offset = region->unityKey + 12.0 * std::log2(std::max<u16>(1, finalPitch) / 4096.0) - noteKey;
  if (note.valid() && emittedBend != offset) {
    out.pitchBend(offset);
    emittedBend = offset;
  }
  if (volumeDirty) {
    unsigned level =
        config.layout.version == Version::Waterworld ? std::min(eventVolume, instrumentVolume) : eventVolume;
    level = (level * regionVolume) >> 8;
    level = (level * trackVolume) >> 8;
    if (config.layout.version == Version::JurassicPark2) {
      level = (level * instrumentVolume) >> 8;
    }
    level = (level * masterVolume) >> 8;
    level = (level * musicVolume) >> 8;
    if (outputMode == 0) {
      pan = 0x4000;
    }
    const u8 p = pan >> 8;
    const std::array<s8, 2> balance =
        outputMode > 1 && surround != 0
            ? std::array<s8, 2>{static_cast<s8>(level >> 1), static_cast<s8>(-int((level * surround) >> 8))}
            : std::array<s8, 2>{static_cast<s8>((level * static_cast<u8>(127 - p)) >> 8),
                                static_cast<s8>((level * p) >> 8)};
    if (emittedBalance != balance) {
      out.stereoBalance(balance[0] / 128.0, balance[1] / 128.0);
      emittedBalance = balance;
    }
    volumeDirty = false;
  }
}

}  // namespace vgmtrans::formats::ocean_snes
