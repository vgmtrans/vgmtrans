/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/WolfTeamSnes/WolfTeamSnes.h"
#include "value/formats/WolfTeamSnes/WolfTeamSnesGrammar.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::wolf_team_snes {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 32768;
constexpr u32 kDefaultPitchTable = 0x0868;
constexpr u32 kLeadingJockeyPitchTable = 0x082a;
constexpr u32 kSegmentedPitchTable = 0x0180;
constexpr u32 kLatePitchEntries = 0x60;
constexpr u32 kSegmentedPitchEntries = 14;
constexpr u32 kPitchUnityIndex = 72;
constexpr u16 kMinUnityPitch = 0x0e00;
constexpr u16 kMaxUnityPitch = 0x1200;
// Repeat slots 0 and 1 mirror the driver's two nested loop markers. The VM
// includes repeat state in its visit key, so a reserved third slot carries the
// runtime phrase/segment index when two table entries share one source address.
constexpr u8 kSourcePositionRepeatSlot = 2;

[[nodiscard]] s16 signedByte(u8 value) {
  return value < 0x80 ? value : static_cast<s16>(value) - 0x100;
}

[[nodiscard]] s16 centeredByte(u8 value) {
  return signedByte(static_cast<u8>(value - 0x40));
}

[[nodiscard]] u8 compactMultiply(u8 lhs, u8 rhs) {
  return static_cast<u8>((static_cast<u16>(lhs) * static_cast<u16>(rhs) * 2) >> 7);
}

[[nodiscard]] u32 roundedDivide(u32 numerator, u32 denominator) {
  return denominator == 0 ? numerator : (numerator + denominator / 2) / denominator;
}

[[nodiscard]] u8 timerTargetFromScalar(u8 headerTempo, u8 tempoScale, u8 timerScale) {
  u8 scalar = compactMultiply(headerTempo, tempoScale);
  scalar = static_cast<u8>(compactMultiply(scalar, timerScale) + 1);
  if (scalar == 0) {
    scalar = 1;
  }
  return static_cast<u8>(roundedDivide(10000, scalar));
}

[[nodiscard]] u32 lateTimerTarget(u8 headerTempo, u8 tempoScale) {
  u8 scalar = compactMultiply(headerTempo, tempoScale);
  scalar = static_cast<u8>(compactMultiply(scalar, 0x40) + 1);
  if (scalar == 0) {
    scalar = 1;
  }
  return roundedDivide(10000, scalar);
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u32 timerTarget) {
  if (timerTarget == 0) {
    timerTarget = 256;
  }
  return timerTarget * kPpqn * 125;
}

[[nodiscard]] int normalizeDriverPitchIndex(int pitch) {
  while (pitch < 0) {
    pitch += 12;
  }
  while (pitch >= 0x60) {
    pitch -= 12;
  }
  return pitch;
}

[[nodiscard]] int floorDivide(int value, int divisor) {
  int quotient = value / divisor;
  if (value % divisor < 0) {
    --quotient;
  }
  return quotient;
}

[[nodiscard]] int positiveModulo(int value, int divisor) {
  const int remainder = value % divisor;
  return remainder < 0 ? remainder + divisor : remainder;
}

[[nodiscard]] u8 addPackedSemitones(u8 noteCode, s16 semitones) {
  const int total = std::clamp<int>((noteCode >> 4) * 12 + (noteCode & 0x0f) + semitones, 0, 0x0f * 12 + 11);
  return static_cast<u8>((floorDivide(total, 12) << 4) | positiveModulo(total, 12));
}

[[nodiscard]] double centsForPitchOffset(u16 basePitch, s16 offset) {
  if (basePitch == 0) {
    return 0.0;
  }
  const int result = static_cast<int>(basePitch) + offset;
  return result <= 0 ? -1200.0 : 1200.0 * std::log2(static_cast<double>(result) / basePitch);
}

[[nodiscard]] double vibratoDepthSemitones(u8 depth, u16 basePitch) {
  if (basePitch == 0) {
    basePitch = 0x1000;
  }
  const double up = centsForPitchOffset(basePitch, depth);
  const double down = centsForPitchOffset(basePitch, -static_cast<s16>(depth));
  return std::max(std::abs(up), std::abs(down)) / 100.0;
}

[[nodiscard]] StereoBalance latePan(u8 raw) {
  const u8 linear = static_cast<u8>(0x7f - std::min<u8>(raw, 0x7f));
  const double right = linear == 0x7f ? 1.0 : linear / 128.0;
  return StereoBalance{.leftGain = 1.0 - right, .rightGain = right};
}

[[nodiscard]] StereoBalance segmentedPan(u8 raw) {
  const double right = (0x80 - std::min<u8>(raw, 0x80)) / 128.0;
  return StereoBalance{.leftGain = 1.0 - right, .rightGain = right};
}

[[nodiscard]] bool looksLikePitchTable(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kLatePitchEntries * 2)) {
    return false;
  }
  const u16 unity = reader.le16(offset + kPitchUnityIndex * 2);
  if (unity < kMinUnityPitch || unity > kMaxUnityPitch) {
    return false;
  }
  u16 previous = reader.le16(offset);
  if (previous == 0) {
    return false;
  }
  for (u32 index = 1; index < kLatePitchEntries; ++index) {
    const u16 current = reader.le16(offset + index * 2);
    if (current <= previous) {
      return false;
    }
    previous = current;
  }
  for (u32 index = 0; index + 12 < kLatePitchEntries; ++index) {
    const u32 pitch = reader.le16(offset + index * 2);
    const u32 octave = reader.le16(offset + (index + 12) * 2);
    const u32 expected = pitch * 2;
    const u32 tolerance = std::max<u32>(4, expected / 128);
    if (octave + tolerance < expected || octave > expected + tolerance) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] u32 findPitchTable(ByteReader reader, Variant variant) {
  for (u32 offset = 0; reader.has(offset, 7); ++offset) {
    if (reader.u8At(offset) != 0xf5 || reader.u8At(offset + 3) != 0xfd || reader.u8At(offset + 4) != 0xf5) {
      continue;
    }
    const u8 highLow = reader.u8At(offset + 1);
    const u8 page = reader.u8At(offset + 2);
    const u8 low = reader.u8At(offset + 5);
    if (reader.u8At(offset + 6) == page && highLow == static_cast<u8>(low + 1)) {
      const u32 table = static_cast<u32>(page) << 8 | low;
      if (looksLikePitchTable(reader, table)) {
        return table;
      }
    }
  }

  const std::array knownTables{
      variant == Variant::LeadingJockey ? kLeadingJockeyPitchTable : kDefaultPitchTable,
      variant == Variant::LeadingJockey ? kDefaultPitchTable : kLeadingJockeyPitchTable,
  };
  for (const u32 table : knownTables) {
    if (looksLikePitchTable(reader, table)) {
      return table;
    }
  }
  return kDefaultPitchTable;
}

struct RuntimeTrackConfig {
  u8 status = 0;
  std::vector<Address> streamStarts;
};

struct RuntimeConfig {
  Variant variant = Variant::LateFamily;
  LateTraits lateTraits;
  u8 headerTempo = 0;
  u8 timerScale = 0x40;
  std::array<s16, kInstrumentCount> instrumentPitch{};
  std::vector<u16> pitchTable;
  std::map<u32, RuntimeTrackConfig> tracks;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config)
      : variant(config.variant), headerTempo(config.headerTempo), timerScale(config.timerScale),
        instrumentPitch(config.instrumentPitch), pitchTable(config.pitchTable) {}

  [[nodiscard]] bool middle() const noexcept { return isMiddleSegmentedVariant(variant); }

  [[nodiscard]] u32 timerTarget(u8 tempoScale) const {
    const bool segmented = variant == Variant::Arcus || middle();
    return segmented ? timerTargetFromScalar(headerTempo, tempoScale, middle() ? 0x40 : timerScale)
                     : lateTimerTarget(headerTempo, tempoScale);
  }

  [[nodiscard]] u16 pitch(u32 index) const {
    if (index < pitchTable.size() && pitchTable[index] != 0) {
      return pitchTable[index];
    }
    return variant == Variant::Arcus || middle() ? 0 : 0x1000;
  }

  Variant variant = Variant::LateFamily;
  u8 headerTempo = 0;
  u8 timerScale = 0x40;
  std::array<s16, kInstrumentCount> instrumentPitch{};
  std::vector<u16> pitchTable;
};

struct ActiveNote {
  PerformanceNoteId id;
  double key = 0.0;
  u16 baseDspPitch = 0;
  s16 fineDspPitchOffset = 0;
  u64 endTick = 0;
  bool sustained = false;
};

struct RuntimeLoopMarker {
  bool active = false;
  size_t streamIndex = 0;
  Address destination;
};

struct TrackState {
  TrackState(const TrackProgram& trackProgram, const RuntimeConfig& config)
      : variant(config.variant), lateTraits(config.lateTraits) {
    const RuntimeTrackConfig& track = config.tracks.at(trackProgram.sourceTrackNumber);
    if (!segmented()) {
      vibratoEnabled = (track.status & 0x01) != 0;
    }
    streamStarts = track.streamStarts;
  }

  [[nodiscard]] bool segmented() const noexcept { return isSegmentedVariant(variant); }
  [[nodiscard]] bool middle() const noexcept { return isMiddleSegmentedVariant(variant); }

  Variant variant = Variant::LateFamily;
  LateTraits lateTraits;
  bool initialized = false;
  u8 rawProgram = 0;
  u8 activeProgram = 0;
  s16 instrumentPitch = 0;
  u8 vibratoDelay = 0;
  u8 vibratoPhaseStep = 0;
  u8 vibratoDepth = 0;
  u8 vibratoRawDepth = 0;
  bool vibratoEnabled = false;
  s16 finePitch = 0;
  u16 lastVibratoBase = 0x1000;
  size_t savedStream = 0;
  size_t currentStream = 0;
  std::vector<Address> streamStarts;
  std::array<RuntimeLoopMarker, 2> loopMarkers{};
  std::vector<ActiveNote> activeNotes;
};

struct NotePitch {
  double key = 0.0;
  u16 baseDspPitch = 0;
  s16 fineDspPitchOffset = 0;
  double tuningCents = 0.0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void beforeCommand() {
    if (track.initialized) {
      return;
    }
    track.initialized = true;
    selectInstrument(0);
  }

  void purgeNotes() {
    const u64 tick = vm.tick();
    std::erase_if(track.activeNotes,
                  [tick](const ActiveNote& note) { return !note.sustained && note.endTick <= tick; });
  }

  void keyOff() {
    purgeNotes();
    for (const ActiveNote& note : track.activeNotes) {
      static_cast<void>(out.setNoteEnd(note.id, vm.tick()));
    }
    track.activeNotes.clear();
  }

  void timedNote(double key, double velocity, u32 duration, u16 baseDspPitch, s16 fineDspPitchOffset, bool sustained) {
    purgeNotes();
    if (!sustained) {
      for (auto candidate = track.activeNotes.rbegin(); candidate != track.activeNotes.rend(); ++candidate) {
        if (candidate->sustained || std::abs(candidate->key - key) >= 0.000001) {
          continue;
        }

        static_cast<void>(out.setNoteEnd(candidate->id, vm.tick() + duration));
        candidate->baseDspPitch = baseDspPitch;
        candidate->fineDspPitchOffset = fineDspPitchOffset;
        candidate->endTick = vm.tick() + duration;
        return;
      }
    }

    const PerformanceNoteId note = out.note(key, velocity, duration);
    track.activeNotes.push_back(ActiveNote{
        .id = note,
        .key = key,
        .baseDspPitch = baseDspPitch,
        .fineDspPitchOffset = fineDspPitchOffset,
        .endTick = vm.tick() + duration,
        .sustained = sustained,
    });
  }

  [[nodiscard]] u8 effectiveProgram(u8 raw) const {
    if (track.segmented()) {
      return raw;
    }
    const auto& traits = track.lateTraits;
    if (!traits.remapHighInstrumentIds || raw < 0x40) {
      return raw;
    }
    u8 effective = raw & 1;
    if (raw < traits.specialInstrumentUpper) {
      effective = static_cast<u8>(effective + 2);
    }
    return effective;
  }

  void activateProgram(u8 programNumber) {
    track.activeProgram = programNumber;
    track.instrumentPitch = programNumber < kInstrumentCount ? program.instrumentPitch[programNumber] : 0;
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = programNumber});
  }

  void selectInstrument(u8 raw) {
    track.rawProgram = raw;
    activateProgram(effectiveProgram(raw));
  }

  [[nodiscard]] u8 starOceanProgram(u8 key) const {
    if (key < 0x2d) {
      return 0x0a;
    }
    if (key < 0x3e) {
      return 0x09;
    }
    if (key < 0x53) {
      return 0x08;
    }
    if (key < 0x5d) {
      return 0x07;
    }
    return 0x06;
  }

  void activateProgramForKey(u8 key) {
    const u8 desired = track.lateTraits.hasInstrument5KeySplit && track.rawProgram == 5
                           ? starOceanProgram(key)
                           : effectiveProgram(track.rawProgram);
    if (desired != track.activeProgram) {
      activateProgram(desired);
    }
  }

  [[nodiscard]] LfoPerformanceContext lfoContext() const {
    return LfoPerformanceContext{
        .cyclesPerTick = track.vibratoPhaseStep / 64.0,
        .delayTicks = track.vibratoDelay,
        .delayIsTempoRelative = true,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .initialPhaseCycles = 0.0,
    };
  }

  void beginVibrato(u16 baseDspPitch) {
    track.lastVibratoBase = baseDspPitch == 0 ? 0x1000 : baseDspPitch;
    if (!track.vibratoEnabled || track.vibratoPhaseStep == 0 || track.vibratoDepth == 0) {
      out.vibratoDepth(0.0, lfoContext());
      return;
    }
    const u8 initial =
        track.segmented() ? std::min<u8>(track.vibratoRawDepth < 4 ? track.vibratoRawDepth : 4, track.vibratoDepth) : 0;
    const u8 step = static_cast<u8>((track.vibratoDepth >> 3) + 1);
    const u32 ramp =
        track.vibratoDepth <= initial ? 0 : (static_cast<u32>(track.vibratoDepth - initial) + step - 1) / step;
    const double target = vibratoDepthSemitones(track.vibratoDepth, track.lastVibratoBase);
    const double initialDepth = vibratoDepthSemitones(initial, track.lastVibratoBase);
    out.vibratoDepth(initialDepth, lfoContext());
    if (ramp != 0) {
      static_cast<void>(out.noteEnvelope(PerformanceAutomationTarget::VibratoDepth, target, ramp, track.vibratoDelay));
    } else if (target != initialDepth) {
      out.vibratoDepth(target, lfoContext());
    }
  }

  [[nodiscard]] Effects lateNote(u8 key, u8 delay, u8 gate, u8 velocity) {
    activateProgramForKey(key);
    const int driverKey = normalizeDriverPitchIndex(static_cast<int>(key) + track.instrumentPitch);
    const int outputKey = std::clamp<int>(driverKey - track.instrumentPitch, 0, 127);
    const u16 baseDsp = program.pitch(static_cast<u32>(driverKey));
    beginVibrato(baseDsp);
    const u32 duration = gate == 0xff ? std::max<u32>(delay, 1) : static_cast<u32>(gate) + 1;
    timedNote(outputKey, velocity / 255.0, duration, baseDsp, 0, false);
    return Effects::wait(delay);
  }

  [[nodiscard]] u16 scaleSegmentPitch(u32 index, int octave) const {
    u32 pitch = static_cast<u32>(program.pitch(index)) << 1;
    while (octave < 6 && pitch != 0) {
      pitch >>= 1;
      ++octave;
    }
    return static_cast<u16>(std::min<u32>(pitch, std::numeric_limits<u16>::max()));
  }

  [[nodiscard]] NotePitch segmentedPitch(u8 noteCode) const {
    if (!track.middle()) {
      const int adjusted = (noteCode >> 4) * 12 + (noteCode & 0x0f) + track.instrumentPitch;
      const int octave = floorDivide(adjusted, 12);
      const int semitone = positiveModulo(adjusted, 12);
      const u16 base = scaleSegmentPitch(static_cast<u32>(semitone), octave);
      return NotePitch{
          .key = static_cast<double>(std::clamp<int>((noteCode >> 4) * 12 + (noteCode & 0x0f), 0, 127)),
          .baseDspPitch = base,
          .fineDspPitchOffset = track.finePitch,
          .tuningCents = centsForPitchOffset(base, track.finePitch),
      };
    }

    const u8 adjusted = addPackedSemitones(noteCode, track.instrumentPitch);
    const int octave = adjusted >> 4;
    const int semitone = adjusted & 0x0f;
    const int previous = scaleSegmentPitch(static_cast<u32>(semitone), octave);
    const int base = scaleSegmentPitch(static_cast<u32>(semitone + 1), octave);
    const int next = scaleSegmentPitch(static_cast<u32>(semitone + 2), octave);
    const int fine = std::clamp<int>(track.finePitch, -64, 63);
    int pitch = base;
    if (fine > 0) {
      pitch += ((next - base) * fine) / 64;
    } else if (fine < 0) {
      pitch -= ((base - previous) * -fine) / 64;
    }
    pitch = std::clamp<int>(pitch, 0, std::numeric_limits<u16>::max());
    if (pitch == 0) {
      return NotePitch{.key = static_cast<double>(std::clamp<int>((adjusted >> 4) * 12 + (adjusted & 0x0f), 0, 127))};
    }
    const double key = 60.0 + 12.0 * std::log2(pitch / 4096.0);
    const double rounded = std::clamp<double>(std::round(key), 0.0, 127.0);
    return NotePitch{.key = rounded,
                     .baseDspPitch = static_cast<u16>(pitch),
                     .fineDspPitchOffset = 0,
                     .tuningCents = (key - rounded) * 100.0};
  }

  [[nodiscard]] Effects segmentedNote(u8 key, u8 delay, u8 gate, u8 velocity) {
    out.pitchBend(0.0);
    const NotePitch pitch = segmentedPitch(key);
    out.tuning(pitch.tuningCents);
    const int tunedDspPitch = static_cast<int>(pitch.baseDspPitch) + pitch.fineDspPitchOffset;
    beginVibrato(tunedDspPitch <= 0 ? u16{0x1000}
                                    : static_cast<u16>(std::min<int>(tunedDspPitch, std::numeric_limits<u16>::max())));
    const double linearVelocity = track.middle() ? velocity / 255.0 : std::min<u8>(velocity, 0x7f) / 127.0;
    const u32 duration = gate == 0 ? std::max<u32>(delay, 1) : gate;
    timedNote(pitch.key, linearVelocity, duration, pitch.baseDspPitch, pitch.fineDspPitchOffset, gate == 0);
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects wait(u8 delay) { return Effects::wait(delay); }

  [[nodiscard]] Effects rest(u8 delay) {
    keyOff();
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects delayedProgramChange(u8 delay, u8 raw) {
    selectInstrument(raw);
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects delayedPitchBend(u8 delay, u8 raw) {
    const int midiBend = std::clamp<int>(centeredByte(raw) * 128, -8192, 8191);
    out.pitchBend(midiBend * 12.0 / 8192.0);
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects tempo(u8 delay, u8 value) {
    out.tempo(tempoMicrosecondsPerQuarter(program.timerTarget(value)));
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects level(u8 delay, u8 value) {
    const double gain = track.segmented() ? std::min<u8>(value, 0x7f) / 127.0 : value / 255.0;
    out.level(gain, ValueQuantization{.levels = track.segmented() ? 128u : 256u});
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects expression(u8 delay, u8 value) {
    out.expression(value / 255.0, ValueQuantization{.levels = 256});
    return Effects::wait(delay);
  }

  [[nodiscard]] Effects pan(u8 delay, u8 value) {
    const StereoBalance balance = track.segmented() ? segmentedPan(value) : latePan(value);
    out.stereoBalance(balance.leftGain, balance.rightGain);
    return Effects::wait(delay);
  }

  void vibratoEnabled(bool enabled) {
    track.vibratoEnabled = enabled;
    if (!enabled) {
      out.vibratoDepth(0.0, lfoContext());
    }
  }

  void lateVibratoParameters(u8 delay, u8 rawDepth, u8 rate) {
    track.vibratoDelay = delay;
    track.vibratoRawDepth = rawDepth;
    track.vibratoDepth = rawDepth & 0x7f;
    track.vibratoPhaseStep = rate == 0 ? 0 : std::min<u8>(rate, 32);
  }

  void segmentedVibratoParameters(u8 delay, u8 rawDepth, u8 rate) {
    track.vibratoDelay = delay;
    track.vibratoRawDepth = rawDepth;
    track.vibratoDepth = rawDepth & 0x7f;
    track.vibratoPhaseStep = static_cast<u8>(16 - (rate & 0x0f));
  }

  void fineTune(u8 raw) {
    track.finePitch = centeredByte(raw);
    if (!track.segmented()) {
      out.tuning(track.finePitch * 100.0 / 64.0);
    }
  }

  void echoSend(bool enabled) { out.reverb(enabled ? 40.0 / 127.0 : 0.0); }

  void echoMode(u8 mode) {
    static constexpr std::array<u8, 6> depth{0, 40, 40, 40, 40, 64};
    out.reverb((mode < depth.size() ? depth[mode] : 40) / 127.0);
  }

  void adsr(u8 adsr1, u8 adsr2) {
    out.replaceEnvelope(snesDspEnvelope(static_cast<u8>(adsr1 | 0x80), adsr2, 0xb8),
                        VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  [[nodiscard]] Effects activeVoicePitch(u8 delay, u8 raw) {
    purgeNotes();
    if (!track.activeNotes.empty()) {
      const ActiveNote& note = track.activeNotes.back();
      const int tuned = static_cast<int>(note.baseDspPitch) + note.fineDspPitchOffset;
      if (tuned > 0) {
        const int current = static_cast<u16>(tuned);
        const s16 offset = centeredByte(raw) * 2;
        int bent = current;
        if (offset >= 0) {
          bent += (current * offset) >> 8;
        } else {
          bent -= ((current >> 1) * -offset) >> 8;
        }
        out.pitchBend(bent <= 0 ? -12.0 : 12.0 * std::log2(static_cast<double>(bent) / current));
      }
    }
    return Effects::wait(delay);
  }

  void saveSegment() { track.savedStream = track.currentStream; }

  void syncSourcePosition(size_t index) {
    vm.repeatCounter(kSourcePositionRepeatSlot).start(static_cast<u32>(index) + 1);
  }

  [[nodiscard]] Effects advanceStream(bool stopNotesAtEnd) {
    if (track.currentStream + 1 >= track.streamStarts.size()) {
      if (stopNotesAtEnd) {
        keyOff();
      }
      return vm.end();
    }
    ++track.currentStream;
    syncSourcePosition(track.currentStream);
    return vm.finiteBranch(track.streamStarts[track.currentStream]);
  }

  [[nodiscard]] Effects advanceSegment() { return advanceStream(true); }

  [[nodiscard]] Effects advancePhrase() { return advanceStream(false); }

  [[nodiscard]] Effects returnSaved() {
    if ((track.variant == Variant::Arcus && track.savedStream == 0) || track.savedStream >= track.streamStarts.size()) {
      keyOff();
      return vm.end();
    }
    track.currentStream = track.savedStream;
    syncSourcePosition(track.currentStream);
    return vm.jump(track.streamStarts[track.savedStream]);
  }

  [[nodiscard]] Effects endTrack() {
    keyOff();
    return vm.end();
  }

  void beginRepeat(Address destination) {
    const u8 slot = track.loopMarkers[0].active ? 1 : 0;
    vm.repeatCounter(slot).finish();
    track.loopMarkers[slot] =
        RuntimeLoopMarker{.active = true, .streamIndex = track.currentStream, .destination = destination};
  }

  [[nodiscard]] Effects endRepeat(u8 count) {
    const u8 slot = track.loopMarkers[1].active ? 1 : 0;
    RuntimeLoopMarker& marker = track.loopMarkers[slot];
    if (!marker.active) {
      return {};
    }
    const Effects effects =
        count == 0 ? vm.declaredLoop(marker.destination) : vm.countedRepeatUntil(slot, count, marker.destination);
    if (effects.flowOverride) {
      track.currentStream = marker.streamIndex;
      syncSourcePosition(track.currentStream);
    } else {
      marker = {};
    }
    return effects;
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeLateCommand(ByteReader reader, u32 begin, const Layout& layout,
                                                       std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, "wolf-team-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode < 0x80) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 key = event.opcodeValue("key", opcode, SourceValueDisplay::Default, SemanticOperandRole::NoteKey);
    const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
    const u8 gate = event.u8("gate_minus_one", SemanticOperandRole::Duration);
    const u8 velocity = event.u8("velocity");
    return event.invoke<&Playback::lateNote>(key, delay, gate, velocity);
  }

  switch (opcode) {
    case 0x90: {
      auto event = cursor.command("Wait", SequenceSemantic::Rest);
      return event.invoke<&Playback::wait>(event.u8("delay", SemanticOperandRole::Duration));
    }
    case 0x91:
    case 0xfd: {
      auto event = cursor.command("Phrase Boundary", SequenceSemantic::End);
      return event.invoke<&Playback::advancePhrase>().runtimeControlFlow();
    }
    case 0x92: {
      auto event = cursor.command("Loop Marker", SequenceSemantic::Repeat, CommandPlaybackStatus::AffectsControlFlow);
      const Address destination{begin + 1};
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      return event.invoke<&Playback::beginRepeat>(destination);
    }
    case 0x93: {
      auto event = cursor.command("Loop End", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      event.runtimeControlFlow();
      return event.invoke<&Playback::endRepeat>(count);
    }
    case 0x94: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::delayedPitchBend>(delay, event.u8("bend", SemanticOperandRole::Pitch));
    }
    case 0x95: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::tempo>(delay, event.u8("scale"));
    }
    case 0x96: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      if (layout.lateTraits.programChangeHasDelay) {
        const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
        return event.invoke<&Playback::delayedProgramChange>(
            delay, event.u8("program", SemanticOperandRole::InstrumentProgram));
      }
      return event.invoke<&Playback::selectInstrument>(event.u8("program", SemanticOperandRole::InstrumentProgram));
    }
    case 0x97: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::level>(delay, event.u8("volume", SemanticOperandRole::Level));
    }
    case 0x98: {
      auto event = cursor.command("Expression", SequenceSemantic::Level);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::expression>(delay, event.u8("expression", SemanticOperandRole::Level));
    }
    case 0x99: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::pan>(delay, event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0x9a:
      return cursor.ignored("No Operation", 2, "nop");
    case 0x9b: {
      auto event = cursor.command("Vibrato Toggle", SequenceSemantic::Modulation);
      return event.invoke<&Playback::vibratoEnabled>(event.u8("enabled") != 0);
    }
    case 0x9c: {
      auto event = cursor.command("Vibrato/LFO Parameters", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::lateVibratoParameters>(delay, depth,
                                                            event.u8("rate", SemanticOperandRole::Modulation));
    }
    case 0xa2: {
      auto event = cursor.command("Fine Tune", SequenceSemantic::Pitch);
      return event.invoke<&Playback::fineTune>(event.u8("centered_value", SemanticOperandRole::Pitch));
    }
    case 0xa3: {
      auto event = cursor.command("Echo Send", SequenceSemantic::State);
      return event.invoke<&Playback::echoSend>(event.u8("enabled") != 0);
    }
    case 0xaa:
      return cursor.ignored("Echo Feedback/FIR", 2, "echo-feedback-fir");
    case 0xad: {
      auto event = cursor.command("Phase/Surround", SequenceSemantic::Pan, CommandPlaybackStatus::SourceOnly);
      static_cast<void>(event.u8("mode"));
      return event.ignore();
    }
    case 0xae: {
      auto event = cursor.command("Random Volume", SequenceSemantic::State, CommandPlaybackStatus::SourceOnly);
      static_cast<void>(event.u8("enabled"));
      return event.ignore();
    }
    case 0xaf: {
      auto event = cursor.command("ADSR Override", SequenceSemantic::Envelope);
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      return event.invoke<&Playback::adsr>(adsr1, event.u8("adsr2", SourceValueDisplay::Hex));
    }
    case 0xb0: {
      auto event = cursor.command("Echo Volume Mode", SequenceSemantic::State);
      return event.invoke<&Playback::echoMode>(event.u8("mode"));
    }
    case 0xb2: {
      auto event = cursor.command("Gate Jitter", SequenceSemantic::State, CommandPlaybackStatus::SourceOnly);
      static_cast<void>(event.u8("enabled"));
      return event.ignore();
    }
    default:
      return cursor.unsupported("Invalid Late-Family Opcode", "invalid").stop();
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeSegmentedCommand(ByteReader reader, u32 begin, const Layout& layout,
                                                            std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, "wolf-team-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const bool middle = layout.middleSegmented();
  const u8 opcode = cursor.opcode();
  if (opcode < 0x80) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 key = event.opcodeValue("packed_key", opcode, SourceValueDisplay::Hex, SemanticOperandRole::NoteKey);
    const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
    const u8 gate = event.u8("gate", SemanticOperandRole::Duration);
    return event.invoke<&Playback::segmentedNote>(key, delay, gate, event.u8("velocity"));
  }

  auto command = [&](std::string_view label, SequenceSemantic semantic,
                     CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback,
                     std::string_view kind = {}) { return cursor.command(label, semantic, playback, kind); };

  switch (opcode) {
    case 0xe0: {
      auto event = command("Rest / Key Off", SequenceSemantic::Rest);
      return event.invoke<&Playback::rest>(event.u8("delay", SemanticOperandRole::Duration));
    }
    case 0xfd: {
      auto event = command("Segment Boundary", SequenceSemantic::End);
      return event.invoke<&Playback::advanceSegment>().runtimeControlFlow();
    }
    case 0xf1:
      if (middle) {
        return command("End", SequenceSemantic::End).invoke<&Playback::endTrack>().runtimeControlFlow();
      }
      return command("No Operation", SequenceSemantic::Meta, CommandPlaybackStatus::NoOp, "nop").ignore();
    case 0xe1: {
      auto event = command("Volume", SequenceSemantic::Level);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::level>(delay, event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xe2: {
      auto event = command("Pan", SequenceSemantic::Pan);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::pan>(delay, event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0xe7: {
      auto event = command("Tempo/Speed", SequenceSemantic::Tempo);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::tempo>(delay, event.u8("scale"));
    }
    case 0xec: {
      auto event = command("Program Change (SRCN)", SequenceSemantic::Program);
      return event.invoke<&Playback::selectInstrument>(event.u8("srcn", SemanticOperandRole::InstrumentProgram));
    }
    case 0xee: {
      auto event = command("Active Voice Pitch Bend", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::activeVoicePitch>(delay, event.u8("offset", SemanticOperandRole::Pitch));
    }
    case 0xe4: {
      auto event = command("Vibrato Toggle", SequenceSemantic::Modulation);
      return event.invoke<&Playback::vibratoEnabled>(event.u8("enabled") != 0);
    }
    case 0xf0:
    case 0xf7: {
      if (middle && opcode == 0xf7) {
        return cursor.unsupported("Invalid Middle-Family Opcode", "invalid").stop();
      }
      auto event = command("Driver Flag", SequenceSemantic::State, CommandPlaybackStatus::SourceOnly, "driver-flag");
      static_cast<void>(event.u8("enabled"));
      return event.ignore();
    }
    case 0xe6:
      if (middle) {
        auto event = command("Control E6", SequenceSemantic::State, CommandPlaybackStatus::SourceOnly, "control");
        static_cast<void>(event.rawBytes("bytes", 2));
        return event.ignore();
      }
      return command("No Operation", SequenceSemantic::Meta, CommandPlaybackStatus::NoOp, "nop").ignore();
    case 0xe8:
    case 0xe9:
      if (middle) {
        auto event = command(opcode == 0xe8 ? "Control E8" : "Control E9", SequenceSemantic::State,
                             CommandPlaybackStatus::SourceOnly, "control");
        static_cast<void>(event.rawBytes("bytes", 3));
        return event.ignore();
      }
      return command("No Operation", SequenceSemantic::Meta, CommandPlaybackStatus::NoOp, "nop").ignore();
    case 0xe5: {
      auto event = command("Vibrato Parameters", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::segmentedVibratoParameters>(
          delay, depth, event.u8("rate_nibble", SemanticOperandRole::Modulation));
    }
    case 0xf4: {
      auto event = command("Fine Tune", SequenceSemantic::Pitch);
      return event.invoke<&Playback::fineTune>(event.u8("centered_value", SemanticOperandRole::Pitch));
    }
    case 0xf2:
      if (middle) {
        auto event = command("Control F2", SequenceSemantic::State, CommandPlaybackStatus::SourceOnly, "control");
        static_cast<void>(event.rawBytes("bytes", 1));
        return event.ignore();
      }
      return command("No Operation", SequenceSemantic::Meta, CommandPlaybackStatus::NoOp, "nop").ignore();
    case 0xf8: {
      auto event = command("Return to Saved Segment", SequenceSemantic::Repeat);
      return event.invoke<&Playback::returnSaved>().runtimeControlFlow();
    }
    case 0xf9: {
      auto event = command("Save Segment", SequenceSemantic::Repeat, CommandPlaybackStatus::AffectsControlFlow);
      return event.invoke<&Playback::saveSegment>();
    }
    case 0xef: {
      auto event =
          command("Echo Feedback", SequenceSemantic::State, CommandPlaybackStatus::SourceOnly, "echo-feedback");
      static_cast<void>(event.u8("feedback"));
      if (middle) {
        static_cast<void>(event.u8("unknown"));
      }
      return event.ignore();
    }
    case 0xe3: {
      auto event =
          command("Release/Modulation Flag", SequenceSemantic::State, CommandPlaybackStatus::SourceOnly, "driver-flag");
      static_cast<void>(event.u8("value"));
      return event.ignore();
    }
    default:
      if (!middle) {
        return command("No Operation", SequenceSemantic::Meta, CommandPlaybackStatus::NoOp, "nop").ignore();
      }
      return cursor.unsupported("Invalid Middle-Family Opcode", "invalid").stop();
  }
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, const Layout& layout, const ChannelLayout& channel,
                                       std::optional<AssetId> sequence, std::optional<SourceAnnotationId> parent,
                                       SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const u32 start = channel.streamStarts.empty() ? 0 : channel.streamStarts.front();
  TrackDecodeScope scope{
      .reader = reader,
      .bytecodeEnd = kAramSize,
      .maxCommands = kMaxTrackCommands,
      .sequenceAsset = sequence,
      .parentAnnotation = parent,
      .sourceMap = sourceMap,
  };
  auto session = scope.begin(channel.index, start);
  std::map<u32, DecodedBytecodeCommand> commands;

  for (const u16 streamStart : channel.streamStarts) {
    u32 offset = streamStart;
    for (u32 count = 0; count < kMaxTrackCommands && reader.has(offset, 1); ++count) {
      const u8 opcode = reader.u8At(offset);
      const detail::CommandShape shape = detail::commandShape(layout.variant, layout.lateTraits, opcode);
      if (shape.size == 0 || !reader.has(offset, shape.size)) {
        Cursor invalid(reader, offset, "wolf-team-snes", diagnostics);
        commands.try_emplace(offset, invalid.unsupported("Truncated or Invalid Command", "invalid").stop());
        break;
      }
      DecodedBytecodeCommand decoded = layout.segmented() ? decodeSegmentedCommand(reader, offset, layout, diagnostics)
                                                          : decodeLateCommand(reader, offset, layout, diagnostics);
      commands.try_emplace(offset, std::move(decoded));
      offset += shape.size;
      if (shape.terminatesStream) {
        break;
      }
    }
  }

  for (auto& [offset, command] : commands) {
    session.append(std::move(command), offset);
  }
  return session.finish();
}

[[nodiscard]] RuntimeConfig runtimeConfig(ByteReader reader, const Layout& layout) {
  RuntimeConfig config{
      .variant = layout.variant,
      .lateTraits = layout.lateTraits,
      .headerTempo = reader.u8At(layout.sequenceHeaderAddress + 0x22),
      .timerScale =
          layout.middleSegmented() ? u8{0x40}
                                   : (layout.variant == Variant::Arcus ? reader.u8At(0xe2) : u8{0x40}),
  };

  for (u32 programNumber = 0; programNumber < kInstrumentCount; ++programNumber) {
    s16 pitch = 0;
    if (!layout.segmented()) {
      const u32 patch = layout.instruments.patchTableAddress + programNumber * layout.instruments.entrySize;
      if (reader.has(patch, 1)) {
        pitch = signedByte(reader.u8At(patch));
      }
    } else if (layout.instruments.confirmed) {
      u8 patchIndex = static_cast<u8>(programNumber);
      if (layout.instruments.patchMapAddress && reader.has(*layout.instruments.patchMapAddress + programNumber, 1)) {
        patchIndex = reader.u8At(*layout.instruments.patchMapAddress + programNumber);
      }
      const u32 patch = layout.instruments.patchTableAddress + patchIndex * layout.instruments.entrySize;
      if (reader.has(patch, 1)) {
        const u8 raw = layout.variant == Variant::Arcus
                           ? static_cast<u8>(reader.u8At(patch) + layout.instruments.globalPitchBase)
                           : reader.u8At(patch);
        pitch = signedByte(raw);
      }
    }
    config.instrumentPitch[programNumber] = pitch;
  }

  const u32 pitchTable = layout.segmented() ? kSegmentedPitchTable : findPitchTable(reader, layout.variant);
  const u32 pitchCount = layout.segmented() ? kSegmentedPitchEntries : kLatePitchEntries;
  config.pitchTable.reserve(pitchCount);
  for (u32 index = 0; index < pitchCount; ++index) {
    config.pitchTable.push_back(reader.has(pitchTable + index * 2, 2) ? reader.le16(pitchTable + index * 2) : 0);
  }
  return config;
}

[[nodiscard]] SequenceProgramBehavior behavior(ByteReader reader, const Layout& layout) {
  SequenceProgramBehavior result{
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
      .initialLevel = 1.0,
      .initialExpression = 1.0,
      .initialReverbSend = 0.0,
      .initialStereoBalance = layout.segmented() ? StereoBalance{} : latePan(0x40),
      .initialPitchBendRangeSemitones = static_cast<u8>(layout.segmented() ? 2 : 12),
  };
  const u8 headerTempo = reader.u8At(layout.sequenceHeaderAddress + 0x22);
  u32 target = 0;
  if (layout.variant == Variant::Arcus) {
    target = reader.u8At(0xfa);
  } else if (!layout.segmented()) {
    target = lateTimerTarget(headerTempo, 0x40);
  } else {
    target = timerTargetFromScalar(headerTempo, 0x40, 0x40);
  }
  result.initialTempoMicrosecondsPerQuarter = tempoMicrosecondsPerQuarter(target);
  return result;
}

}  // namespace

const SequenceDialect& sequenceDialect() {
  static const SequenceDialect dialect = SequenceDialect{
      .commandDetailKindPrefix = "wolf-team-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
              .initialLevel = 1.0,
              .initialExpression = 1.0,
              .initialReverbSend = 0.0,
              .initialStereoBalance = StereoBalance{},
              .initialPitchBendRangeSemitones = 12,
          },
  };
  return dialect;
}

TrackProgram decodeSourceTrack(ByteReader reader, const Layout& layout, const ChannelLayout& channel,
                               std::vector<Diagnostic>* diagnostics) {
  return decodeTrack(reader, layout, channel, std::nullopt, std::nullopt, nullptr, diagnostics);
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const SourceRange headerRange = reader.range(layout.sequenceHeaderAddress, layout.headerLength);
  const auto& dialect = sequenceDialect();
  SequenceProgram program = dialect.makeProgram();
  RuntimeConfig runtime = runtimeConfig(reader, layout);
  program.behavior = behavior(reader, layout);

  std::optional<SourceAnnotationId> headerParent;
  if (sourceMap != nullptr) {
    auto header =
        sourceMap->header(fmt::format("Wolf Team SNES {} Sequence Header", variantName(layout.variant)), headerRange)
            .kind("wolf-team-snes-sequence-header")
            .owner(ObjectRefs::sequence(sequenceId));
    headerParent = header.id();
    sourceMap
        ->field("Tempo/Timing", reader.range(layout.sequenceHeaderAddress + 0x22, 1),
                reader.u8At(layout.sequenceHeaderAddress + 0x22))
        .kind("wolf-team-snes-header-tempo")
        .owner(ObjectRefs::sequence(sequenceId))
        .parent(*headerParent);
  }

  for (const ChannelLayout& channel : layout.channels) {
    const u8 activeMask = layout.segmented() ? 0x01 : 0x80;
    if ((channel.status & activeMask) == 0 || channel.streamStarts.empty()) {
      continue;
    }
    RuntimeTrackConfig trackRuntime{.status = channel.status};
    trackRuntime.streamStarts.reserve(channel.streamStarts.size());
    for (const u16 start : channel.streamStarts) {
      trackRuntime.streamStarts.push_back(Address{start});
    }
    runtime.tracks.emplace(channel.index, std::move(trackRuntime));
    if (sourceMap != nullptr && headerParent) {
      const SourceRange statusRange = reader.range(channel.descriptorRange.offset, 1);
      const SourceRange tablePointerRange = reader.range(channel.descriptorRange.offset + 1, 2);
      const u64 pointerBytes = channel.streamStarts.size() * 2;
      const u64 tableLength = pointerBytes + (reader.has(channel.pointerTableAddress + pointerBytes, 2) ? 2 : 0);
      const SourceRange tableRange = reader.range(channel.pointerTableAddress, tableLength);
      sourceMap->field(fmt::format("Channel {} Status", channel.index + 1), statusRange, channel.status)
          .kind("wolf-team-snes-channel-status")
          .owner(ObjectRefs::sequenceTrack(sequenceId, channel.index))
          .parent(*headerParent);
      sourceMap
          ->pointer(fmt::format("Channel {} Stream Table", channel.index + 1), tablePointerRange,
                    SourceTarget{tableRange})
          .kind("wolf-team-snes-channel-pointer")
          .field("destination", tablePointerRange, channel.pointerTableAddress, SourceValueDisplay::Address)
          .owner(ObjectRefs::sequenceTrack(sequenceId, channel.index))
          .parent(*headerParent);
      const SourceAnnotationId table =
          sourceMap->table(fmt::format("Channel {} Stream Pointer Table", channel.index + 1), tableRange)
              .kind("wolf-team-snes-stream-pointer-table")
              .owner(ObjectRefs::sequenceTrack(sequenceId, channel.index))
              .parent(*headerParent)
              .id();
      for (size_t stream = 0; stream < channel.streamStarts.size(); ++stream) {
        const SourceRange pointer = reader.range(channel.pointerTableAddress + stream * 2, 2);
        sourceMap
            ->pointer(fmt::format("Channel {} Stream {} Pointer", channel.index + 1, stream), pointer,
                      SourceTarget{reader.range(channel.streamStarts[stream], 1)})
            .kind("wolf-team-snes-stream-pointer")
            .field("destination", pointer, channel.streamStarts[stream], SourceValueDisplay::Address)
            .owner(ObjectRefs::sequenceTrack(sequenceId, channel.index))
            .parent(table);
      }
    }
    program.tracks.push_back(decodeTrack(reader, layout, channel, sequenceId, headerParent, sourceMap, diagnostics));
  }
  program.runtime = makeCompiledRuntime<TrackState, Playback, ProgramState>(std::move(runtime));
  return SequenceParse{.program = std::move(program), .headerRange = headerRange};
}

}  // namespace vgmtrans::formats::wolf_team_snes
