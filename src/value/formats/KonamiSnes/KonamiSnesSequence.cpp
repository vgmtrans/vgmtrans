/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnes.h"

#include "value/base/LevelScale.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceLfo.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::konami_snes {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 8192;

// These are the driver's own lookup tables. Using the original values keeps
// center position and loudness changes faithful; replacing them with smooth
// formulas would noticeably change the exported mix.
constexpr std::array<u8, 21> kPanVolumeLeftV1{0x00, 0x05, 0x0c, 0x14, 0x1e, 0x28, 0x32, 0x3c, 0x46, 0x50, 0x59,
                                              0x62, 0x69, 0x6f, 0x74, 0x78, 0x7b, 0x7d, 0x7e, 0x7e, 0x7f};
constexpr std::array<u8, 21> kPanVolumeRightV1{0x7f, 0x7e, 0x7e, 0x7d, 0x7b, 0x78, 0x74, 0x6f, 0x69, 0x62, 0x59,
                                               0x50, 0x46, 0x3c, 0x32, 0x28, 0x1e, 0x14, 0x0c, 0x05, 0x00};
constexpr std::array<u8, 21> kPanVolumeLeftV2{0x00, 0x0a, 0x18, 0x28, 0x3c, 0x50, 0x64, 0x78, 0x8c, 0xa0, 0xb2,
                                              0xc4, 0xd2, 0xde, 0xe8, 0xf0, 0xf6, 0xfa, 0xfc, 0xfc, 0xfe};
constexpr std::array<u8, 21> kPanVolumeRightV2{0xfe, 0xfc, 0xfc, 0xfa, 0xf6, 0xf0, 0xe8, 0xde, 0xd2, 0xc4, 0xb2,
                                               0xa0, 0x8c, 0x78, 0x64, 0x50, 0x3c, 0x28, 0x18, 0x0a, 0x00};
constexpr std::array<u8, 42> kPanTable{0x00, 0x04, 0x08, 0x0e, 0x14, 0x1a, 0x20, 0x28, 0x30, 0x38, 0x40,
                                       0x48, 0x50, 0x5a, 0x64, 0x6e, 0x78, 0x82, 0x8c, 0x96, 0xa0, 0xa8,
                                       0xb0, 0xb8, 0xc0, 0xc8, 0xd0, 0xd6, 0xdc, 0xe0, 0xe4, 0xe8, 0xec,
                                       0xf0, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0xfe, 0xfe, 0xfe};
constexpr std::array<u8, 128> kVolumeTable{
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x06,
    0x06, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x09, 0x09, 0x0a, 0x0a, 0x0a, 0x0a, 0x0b, 0x0b, 0x0c, 0x0c, 0x0d, 0x0d,
    0x0e, 0x0f, 0x10, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1d, 0x1e, 0x20,
    0x22, 0x23, 0x24, 0x26, 0x28, 0x2a, 0x2c, 0x2d, 0x2f, 0x31, 0x33, 0x35, 0x38, 0x3a, 0x3d, 0x40, 0x43, 0x46, 0x49,
    0x4c, 0x4f, 0x52, 0x56, 0x5a, 0x5e, 0x62, 0x66, 0x6b, 0x6f, 0x73, 0x77, 0x7b, 0x7f};

enum class PitchSlideKind : u8 {
  // Each family stores different extra fields and applies tuning differently.
  V1,
  V2,
  V3,
};

enum class FadeTarget : u8 {
  Tempo,
  Volume,
  Pan,
};

struct DecodedPitchSlide {
  // Source bytes are reduced to the four values playback actually needs. The
  // unused reserved and delta fields remain visible in source annotations.
  PitchSlideKind kind = PitchSlideKind::V1;
  u8 delay = 0;
  u8 length = 0;
  u8 targetNote = 0;
};

struct PersistentPitchEffect {
  enum class Kind : u8 {
    None,
    Portamento,
    Envelope,
  };

  Kind kind = Kind::None;
  u8 delay = 0;
  u8 duration = 0;
  s16 depth = 0;
  std::optional<double> previousKey;
};

[[nodiscard]] constexpr bool isLateVersion(KonamiSnesVersion version) {
  return version == KONAMISNES_V5 || version == KONAMISNES_V6;
}

[[nodiscard]] constexpr PitchSlideKind pitchSlideKind(KonamiSnesVersion version) {
  if (version == KONAMISNES_V1) {
    return PitchSlideKind::V1;
  }
  return isLateVersion(version) ? PitchSlideKind::V3 : PitchSlideKind::V2;
}

[[nodiscard]] constexpr bool isTerminalLowOpcode(KonamiSnesVersion version, u8 opcode) {
  switch (version) {
    case KONAMISNES_V1:
      return opcode >= 0x65 && opcode <= 0x7f;
    case KONAMISNES_V2:
    case KONAMISNES_V3:
    case KONAMISNES_V4:
      return opcode >= 0x62 && opcode <= 0x7f;
    case KONAMISNES_V5:
      return opcode >= 0x63 && opcode <= 0x6f;
    case KONAMISNES_V6:
      return opcode >= 0x65 && opcode <= 0x6f;
    case KONAMISNES_NONE:
      return false;
  }
  return false;
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(KonamiSnesVersion version, u8 tempo) {
  // One driver tick is timerFrequency * 125 microseconds, scaled by the inverse
  // tempo byte. Convert that tick length to one MIDI quarter note (48 ticks).
  if (tempo == 0) {
    // The hardware would effectively stop advancing music. Use a finite, very
    // slow tempo so exporters can still represent the command.
    return 60000000;
  }
  return static_cast<u32>(std::lround(kKonamiSnesPpqn * (125.0 * timerFrequency(version)) * 256.0 / tempo));
}

[[nodiscard]] double sequenceTickSeconds(KonamiSnesVersion version, u8 tempo) {
  return static_cast<double>(tempoMicrosecondsPerQuarter(version, tempo)) /
         (1'000'000.0 * static_cast<double>(kKonamiSnesPpqn));
}

[[nodiscard]] u32 vibratoDelayTicks(KonamiSnesVersion version, u8 delay, u8 tempo) {
  // Vibrato delay is measured by a different counter from note duration.
  // Convert both to seconds first, then express the result in sequence ticks.
  const double tickSeconds = sequenceTickSeconds(version, tempo);
  if (tickSeconds <= 0.0 || !std::isfinite(tickSeconds)) {
    return 0;
  }
  const double ticks = vibrato::delaySeconds(version, delay, tempo) / tickSeconds;
  if (ticks <= 0.0 || !std::isfinite(ticks)) {
    return 0;
  }
  return static_cast<u32>(std::min<double>(std::lround(ticks), std::numeric_limits<u32>::max()));
}

[[nodiscard]] double tuningCents(s8 tuning) {
  return tuning * (400.0 / 256.0);
}

[[nodiscard]] constexpr u8 volumeTableValue(KonamiSnesVersion version, u8 index) {
  // Goemon 3 shipped with $23 at index 94; the next driver corrected it to
  // $20. Preserve the original byte instead of silently repairing the ROM.
  return version == KONAMISNES_V5 && index == 94 ? u8{0x23} : kVolumeTable[index];
}

[[nodiscard]] constexpr u8 panTableValue(KonamiSnesVersion version, u8 index) {
  // The same Goemon 3 table contains $46 at index 10, corrected to $40 in V6.
  return version == KONAMISNES_V5 && index == 10 ? u8{0x46} : kPanTable[index];
}

[[nodiscard]] constexpr bool usesEncodedEnvelopeParameters(KonamiSnesVersion version) {
  return version >= KONAMISNES_V1 && version <= KONAMISNES_V3;
}

[[nodiscard]] constexpr u8 releaseGain(u8 amount) {
  if (amount < 100) {
    return amount;
  }
  const u8 base = amount >= 200 ? 200 : 100;
  return static_cast<u8>((amount >= 200 ? 0xa0 : 0x80) | ((amount - base) & 0x1f));
}

[[nodiscard]] constexpr bool usesSoftwareRelease(KonamiSnesVersion version, u8 amount) {
  return amount != 0 && (version <= KONAMISNES_V2 || (version == KONAMISNES_V3 && amount < 100));
}

[[nodiscard]] std::optional<double> dspReleaseSeconds(KonamiSnesVersion version, u8 amount) {
  if (version >= KONAMISNES_V3 && amount >= 100) {
    return snesDspGainEnvelopeSeconds(releaseGain(amount), 0x7ff, 0);
  }
  return std::nullopt;
}

struct RawEnvelopeRegisters {
  u8 adsr1 = 0;
  std::optional<u8> adsr2;
  std::optional<u8> gain;
};

struct RuntimeInstrument {
  RawEnvelopeRegisters envelope;
  u8 savedCompanion = 0;
  u8 pan = 0;
  u8 volume = 0;
};

constexpr size_t kPercussionEnvelopeBase = 0x100;
constexpr size_t kInstrumentEnvelopeCount = kPercussionEnvelopeBase + 0x60;
constexpr size_t kRuntimeInstrumentWords = 2;
constexpr u32 kUnknownRuntimeInstrument = std::numeric_limits<u32>::max();

[[nodiscard]] std::optional<RuntimeInstrument> runtimeInstrument(const std::vector<u32>& data, size_t index,
                                                                 KonamiSnesVersion version) {
  const size_t offset = index * kRuntimeInstrumentWords;
  if (offset + 1 >= data.size() || data[offset] == kUnknownRuntimeInstrument) {
    return std::nullopt;
  }
  const u32 envelope = data[offset];
  const u32 mix = data[offset + 1];
  const u8 adsr1 = static_cast<u8>(envelope >> 16);
  const u8 adsr2 = static_cast<u8>(envelope >> 8);
  const u8 gain = static_cast<u8>(envelope);
  const u8 companion = usesLegacyInstrumentLayout(version) ? gain : ((adsr1 & 0x80) != 0 ? adsr2 : gain);
  return RuntimeInstrument{
      .envelope =
          RawEnvelopeRegisters{
              .adsr1 = adsr1,
              // Early rows load both physical registers. Late rows load only
              // the register selected by ADSR1; the other remains untouched.
              .adsr2 = usesLegacyInstrumentLayout(version) || (adsr1 & 0x80) != 0 ? std::optional{adsr2} : std::nullopt,
              .gain = usesLegacyInstrumentLayout(version) || (adsr1 & 0x80) == 0 ? std::optional{gain} : std::nullopt,
          },
      .savedCompanion = companion,
      .pan = static_cast<u8>(mix >> 8),
      .volume = static_cast<u8>(mix),
  };
}

[[nodiscard]] u8 clampPan(KonamiSnesVersion version, u8 pan) {
  return std::min(pan, version <= KONAMISNES_V2 ? u8{20} : u8{40});
}

struct PanGains {
  double left = 0.0;
  double right = 0.0;
};

// Preserve the driver's version-specific table as exact channel gains. The
// normalized position below is only metadata for the automation intent.
[[nodiscard]] PanGains panGains(KonamiSnesVersion version, u8 rawPan) {
  const u8 pan = clampPan(version, rawPan);
  u8 left = 0;
  u8 right = 0;
  if (version == KONAMISNES_V1) {
    left = kPanVolumeLeftV1[pan];
    right = kPanVolumeRightV1[pan];
  } else if (version == KONAMISNES_V2) {
    left = kPanVolumeLeftV2[pan];
    right = kPanVolumeRightV2[pan];
  } else {
    left = panTableValue(version, pan);
    right = panTableValue(version, static_cast<u8>(40 - pan));
  }
  // V1 divides by $7f. Later engines keep the high byte of an 8x8 product.
  const double fullScale = version == KONAMISNES_V1 ? 127.0 : 256.0;
  return PanGains{
      .left = left / fullScale,
      .right = right / fullScale,
  };
}

[[nodiscard]] double stereoPositionFromPan(KonamiSnesVersion version, u8 rawPan) {
  const PanGains gains = panGains(version, rawPan);
  const double total = gains.left + gains.right;
  return total == 0.0 ? 0.0 : std::clamp((gains.right / total) * 2.0 - 1.0, -1.0, 1.0);
}

struct LfoState {
  void configure(u8 delayValue, u8 rateValue, u8 depthValue) {
    delay = delayValue;
    rate = rateValue;
    depth = depthValue;
    depthState.resetDepth(static_cast<s32>(depth) << 8);
  }

  [[nodiscard]] u16 currentDepthFixed() const { return static_cast<u16>(depthState.currentDepth()); }

  u8 delay = 0;
  u8 rate = 0;
  u8 depth = 0;
  SequenceLfoDepthFadeState depthState;
};

struct RuntimeConfig {
  KonamiSnesVersion version = KONAMISNES_NONE;
  bool indexedEchoFilter = false;
  std::vector<u32> instruments;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config)
      : instruments(config.instruments), version(config.version), indexedEchoFilter(config.indexedEchoFilter) {}

  [[nodiscard]] std::optional<RuntimeInstrument> instrument(size_t index) const {
    return runtimeInstrument(instruments, index, version);
  }

  void setEcho(u8 mask, u8 left, u8 right) {
    const double leftGain = static_cast<s8>(left) / 127.0;
    const double rightGain = static_cast<s8>(right) / 127.0;
    echo.voiceMask = mask;
    echo.send = std::min(std::max(std::abs(leftGain), std::abs(rightGain)), 1.0);
    echo.leftGain = std::clamp(leftGain, -1.0, 1.0);
    echo.rightGain = std::clamp(rightGain, -1.0, 1.0);
  }

  void setEchoParameters(u8 delay, u8 feedback, u8 filter) {
    echo.delayMilliseconds = static_cast<double>(delay & 0x0f) * 16.0;
    echo.feedback = static_cast<s8>(feedback) / 128.0;
    echo.filterIndex = indexedEchoFilter ? filter : 2;
  }

  void setEchoVoice(u8 voiceBit, bool enabled) {
    u8 mask = echo.voiceMask.value_or(0);
    if (enabled) {
      mask |= voiceBit;
    } else {
      mask &= static_cast<u8>(~voiceBit);
    }
    echo.voiceMask = mask;
  }

  ReverbPerformanceEvent echo{.voiceMask = 0};
  const std::vector<u32>& instruments;
  KonamiSnesVersion version = KONAMISNES_NONE;
  bool indexedEchoFilter = false;
};

struct TrackEnvelopeState {
  void selectInstrument(KonamiSnesVersion version, const std::optional<RuntimeInstrument>& instrument) {
    if (!instrument) {
      registers.reset();
      savedCompanion.reset();
    } else if (usesLegacyInstrumentLayout(version)) {
      registers = instrument->envelope;
      savedCompanion = instrument->savedCompanion;
    } else {
      // A late seven-byte row writes only ADSR2 or GAIN. Preserve the inactive
      // physical register because ED can later switch ADSR1 without writing it.
      RawEnvelopeRegisters selected = registers.value_or(RawEnvelopeRegisters{});
      selected.adsr1 = instrument->envelope.adsr1;
      if ((selected.adsr1 & 0x80) != 0) {
        selected.adsr2 = instrument->savedCompanion;
      } else {
        selected.gain = instrument->savedCompanion;
      }
      registers = selected;
      savedCompanion = instrument->savedCompanion;
    }
    // V1-V2 keep their software release accumulator step across instrument
    // loads. V3 and later explicitly clear their release control.
    if (version >= KONAMISNES_V3) {
      releaseAmount = 0;
    }
    dspReleasePendingRestore = false;
  }

  std::optional<RawEnvelopeRegisters> registers;
  std::optional<u8> savedCompanion;
  u8 releaseAmount = 0;
  bool dspReleasePendingRestore = false;
};

// Only values that persist from one executed command to the next live here.
struct TrackState {
  TrackState(const TrackProgram& track, const RuntimeConfig& config)
      : version(config.version), voiceBit(static_cast<u8>(1u << std::min<u32>(track.sourceTrackNumber, 7))) {
    const auto initialInstrument = runtimeInstrument(config.instruments, 0, version);
    envelope.selectInstrument(version, initialInstrument);
    instrumentVolume = initialInstrument ? initialInstrument->volume : 0;
    pan.reset(version <= KONAMISNES_V2 ? 10 : 20);
    volume.reset(0);
    tempoState.reset(kKonamiSnesDefaultTempo);
  }

  [[nodiscard]] u8 noteDuration(u8 length) const {
    if (isHeldDuration(version, noteDurationRate)) {
      return length;
    }
    // V1 stores a percentage from 0-100. Later versions store a 7-bit fraction
    // of the note length. The driver always gives a sounding note at least one
    // tick, even when integer rounding would produce zero.
    const u8 duration = version == KONAMISNES_V1 ? static_cast<u8>((length * noteDurationRate) / 100)
                                                 : static_cast<u8>((length * (noteDurationRate << 1)) >> 8);
    return std::max<u8>(duration, 1);
  }

  [[nodiscard]] double totalTuningCents() const {
    // Loop pitch changes are stored in 1/32-semitone units and accumulate
    // independently for the two nested loop slots.
    return sequenceTuningCents + static_cast<double>(loopPitchDelta + loopPitchDelta2) * (100.0 / 32.0);
  }

  [[nodiscard]] double noteSemitones(u8 key, bool includeTuning = true) const {
    double semitones = (key & 0x7f) + transpose;
    if (includeTuning) {
      semitones += totalTuningCents() / 100.0;
    }
    return semitones;
  }

  KonamiSnesVersion version = KONAMISNES_NONE;
  u8 voiceBit = 1;

  // Note bytes can omit length or duration rate and reuse these values.
  u8 noteLength = 0;
  u8 noteDurationRate = 0;
  u8 defaultNoteVolume = 0;
  u8 currentNoteVolume = 0;
  u8 instrumentVolume = 0;
  s32 transpose = 0;

  // The driver provides two independent counted-loop slots. Each remembers
  // where to return and how much volume and pitch have accumulated on replays.
  Address loopReturnAddress;
  Address loopReturnAddress2;
  s16 loopVolumeDelta = 0;
  s16 loopPitchDelta = 0;
  s16 loopVolumeDelta2 = 0;
  s16 loopPitchDelta2 = 0;

  // Early drivers use adjacent end markers as alternate endings. These fields
  // remember whether playback should return to the shared section or skip to
  // the next ending when it reaches the next marker.
  Address voltaLoopStart;
  Address voltaLoopEnd;
  bool voltaEndMeansPlayFromStart = false;
  bool voltaEndMeansPlayNextVolta = false;

  // Instrument mode and the previous note decide whether a new source command
  // starts a note, extends a slur, or addresses the drum kit.
  bool percussion = false;
  bool instrumentPanEnabled = true;
  bool inSubroutine = false;
  u8 instrument = 0;
  std::optional<u8> previousNoteKey;
  u8 previousDurationRate = 0;
  bool previousWasNote = false;
  bool previousNoteWasPercussion = false;
  double sequenceTuningCents = 0.0;
  double lastEmittedTuningCents = std::numeric_limits<double>::quiet_NaN();
  u8 tempo = kKonamiSnesDefaultTempo;

  // V1-V3 can release a note by subtracting an 8.8 attenuation value in the
  // software mixer instead of keying off the DSP voice.
  u8 softwareReleaseTicks = 0;
  u8 softwareReleaseGateTicks = 0;
  u16 softwareReleaseAttenuation = 0;

  // These controls retain fractional fade progress between note ticks. Their
  // raw values are converted to tempo, gain, or pan only when a tick changes.
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> pan;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> volume;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> tempoState;
  // The driver restarts this reusable depth fade for every note, independently
  // of the oscillator settings that remain active on the track.
  LfoState vibrato;
  u8 vibratoPhaseStep = 0;

  // Pitch transitions use absolute key-space values; MIDI lowering chooses the
  // required bend range relative to the emitted note.
  std::optional<double> notePitch;
  PerformanceNoteId pitchNote;
  PersistentPitchEffect pitchEffect;
  TrackEnvelopeState envelope;
};

// History-dependent driver behavior stays close to the opcode switch below.
struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void note(u8 key, u8 sourceVelocity) {
    if (track.percussion) {
      loadInstrument(program.instrument(kPercussionEnvelopeBase + key));
      out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
      emitPersistentRelease();
    }

    track.currentNoteVolume = sourceVelocity;
    track.defaultNoteVolume = sourceVelocity;
    if (sourceVelocity == 0) {
      // Zero clears the pending attack. V1-V2 also write zero volume; later
      // mixers return before touching VOL(L/R), leaving their previous values.
      track.previousWasNote = false;
      stopSoftwareRelease();
      if (track.version <= KONAMISNES_V2) {
        emitLevel(out);
      } else {
        // A running volume fade still advances in driver RAM, but its audible
        // automation stops when the late mixer begins returning early.
        track.volume.interruptAutomationAt(vm.tick());
      }
      return;
    }

    restorePostReleaseEnvelope();
    const u8 duration = track.noteDuration(track.noteLength);
    beginSoftwareRelease(duration);
    emitLevel(out);
    applyEffectiveTuning();
    const bool continuesVoice = !track.percussion && !track.previousNoteWasPercussion && track.previousWasNote &&
                                isLegatoDuration(track.version, track.previousDurationRate);
    const bool repeatsHeldKey = continuesVoice && track.previousNoteKey == key;
    const PerformanceNoteId previousPitchNote = track.pitchNote;
    const std::optional<double> realizedPitch =
        previousPitchNote.valid() ? out.currentPitchTransitionKey(previousPitchNote) : std::nullopt;
    resetPitchForNote(key);
    if (track.vibrato.depthState.restartFade(track.vibrato.delay)) {
      emitVibratoDepth(track.vibrato.depthState.fadeOutput(out), true);
    }
    NotePerformanceEvent note{
        .key = track.percussion ? static_cast<double>(key) : track.noteSemitones(key, false),
        // Konami's note, track, instrument, and nonlinear table stages form one
        // coupled level calculation, emitted separately by emitLevel().
        .linearVelocity = 1.0,
        .durationTicks = duration,
        .extendsPrevious = repeatsHeldKey,
        // Even without a DSP key-on, every source note resets the driver's
        // per-note LFO state.
        .restartsLfoPhase = true,
        .restartsVibratoLfoPhase = true,
        .restartsTremoloLfoPhase = true,
    };
    track.pitchNote = continuesVoice && !repeatsHeldKey ? out.continueVoice(previousPitchNote, std::move(note))
                                                        : out.note(std::move(note));
    applyPitchEffectToNote(key, continuesVoice ? previousPitchNote : PerformanceNoteId{}, realizedPitch);
    track.previousNoteKey = key;
    track.previousDurationRate = track.noteDurationRate;
    track.previousWasNote = true;
    track.previousNoteWasPercussion = track.percussion;
    track.envelope.dspReleasePendingRestore = track.version >= KONAMISNES_V3 && track.envelope.releaseAmount >= 100 &&
                                              !isHeldDuration(track.version, track.noteDurationRate);
  }

  void rest() {
    track.currentNoteVolume = 0;
    track.previousWasNote = false;
    stopSoftwareRelease();
    if (track.version >= KONAMISNES_V3) {
      track.volume.interruptAutomationAt(vm.tick());
    }
  }

  void tie() {
    // Eligibility belongs to the preceding note's gate state, not the new
    // duration byte carried by E1. A rest therefore cannot revive an old voice.
    if (!track.previousWasNote || !track.previousNoteKey ||
        !isHeldDuration(track.version, track.previousDurationRate)) {
      track.previousWasNote = false;
      return;
    }
    const u8 duration = track.noteDuration(track.noteLength);
    beginSoftwareRelease(duration);
    const double key = track.previousNoteWasPercussion ? static_cast<double>(*track.previousNoteKey)
                                                      : track.noteSemitones(*track.previousNoteKey, false);
    track.pitchNote = out.note(NotePerformanceEvent{
        .key = key,
        .linearVelocity = 1.0,
        .durationTicks = duration,
        .extendsPrevious = true,
        .restartsLfoPhase = false,
    });
    track.previousDurationRate = track.noteDurationRate;
    track.envelope.dspReleasePendingRestore = track.version >= KONAMISNES_V3 && track.envelope.releaseAmount >= 100 &&
                                              !isHeldDuration(track.version, track.noteDurationRate);
  }

  void selectInstrument(u32 bank, u32 programNumber, const std::optional<RuntimeInstrument>& instrument) {
    loadInstrument(instrument);
    out.instrument(bank, programNumber, true);
    emitPersistentRelease();
    emitLevel(out);
  }

  void emitPersistentRelease() {
    if (const auto release = dspReleaseSeconds(track.version, track.envelope.releaseAmount)) {
      out.updateEnvelope(Envelope{.releaseSeconds = *release}, EnvelopeFields::Release,
                         VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
  }

  void percussionOn() {
    if (!track.percussion) {
      out.instrument(0x7f, 0, true);
      track.percussion = true;
    }
  }

  void percussionOff() {
    // The driver only clears its interpretation flag. It deliberately leaves
    // the last drum sample, envelope, pan, and attenuation in the voice.
    track.percussion = false;
  }

  void programChange(u8 programNumber) {
    applyEffectiveTuning(true);
    track.instrument = programNumber;
    selectInstrument(programNumber >> 7, programNumber & 0x7f, program.instrument(programNumber));
  }

  void programChangeAndVolume(u8 volumeValue, u8 programNumber) {
    // FC stores volume first, then tail-calls the ordinary instrument loader.
    // Defer output until both operands are live so no partial mix leaks out.
    track.volume.setCurrentAt(vm.tick(), volumeValue);
    applyEffectiveTuning(true);
    track.instrument = programNumber;
    selectInstrument(programNumber >> 7, programNumber & 0x7f, program.instrument(programNumber));
  }

  void volume(u8 rawVolume) {
    track.volume.setCurrentAt(vm.tick(), rawVolume);
    emitLevel(out);
  }

  void pan(u8 rawPan) {
    const u8 value = clampPan(track.version, rawPan);
    track.pan.setCurrentAt(vm.tick(), value);
    const PanGains gains = panGains(track.version, value);
    out.stereoBalance(gains.left, gains.right);
  }

  void tuning(s8 value) {
    track.sequenceTuningCents = tuningCents(value);
    applyEffectiveTuning();
  }

  void setDefaults(u8 rate, u8 volume) {
    track.noteDurationRate = rate;
    track.defaultNoteVolume = static_cast<u8>(volume & 0x7f);
  }

  void setEchoVoice(bool enabled) {
    program.setEchoVoice(track.voiceBit, enabled);
    out.reverb(program.echo);
  }

  void replaceCurrentEnvelope() {
    const auto envelope = currentEnvelope();
    if (!envelope) {
      return;
    }
    out.replaceEnvelope(*envelope, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void setEnvelope(u8 adsr1, u8 companion, u8 releaseAmount) {
    RawEnvelopeRegisters& registers =
        track.envelope.registers.emplace(track.envelope.registers.value_or(RawEnvelopeRegisters{}));
    registers.adsr1 = adsr1;
    if ((adsr1 & 0x80) != 0) {
      // FA in ADSR mode does not alter the saved GAIN companion.
      registers.adsr2 = companion;
    } else {
      registers.gain = companion;
      track.envelope.savedCompanion = companion;
    }
    track.envelope.releaseAmount = releaseAmount;
    track.envelope.dspReleasePendingRestore = false;
    replaceCurrentEnvelope();
  }

  void setRelease(u8 amount) {
    track.envelope.releaseAmount = amount;
    if (const auto release = dspReleaseSeconds(track.version, amount)) {
      out.updateEnvelope(Envelope{.releaseSeconds = *release}, EnvelopeFields::Release,
                         VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    } else if (const auto envelope = currentEnvelope()) {
      out.updateEnvelope(Envelope{.releaseSeconds = envelope->releaseSeconds}, EnvelopeFields::Release,
                         VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    } else {
      out.restoreEnvelope(EnvelopeFields::Release, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
  }

  void setAdsr1(u8 adsr1) {
    // Late drivers ignore this command while their saved ADSR1 selects GAIN.
    if (!track.envelope.registers || (track.envelope.registers->adsr1 & 0x80) == 0) {
      return;
    }
    track.envelope.registers->adsr1 = adsr1;
    replaceCurrentEnvelope();
  }

  void setAdsr2(u8 adsr2) {
    if (!track.envelope.registers || (track.envelope.registers->adsr1 & 0x80) == 0) {
      return;
    }
    track.envelope.registers->adsr2 = adsr2;
    replaceCurrentEnvelope();
  }

  void tempo(u8 value) {
    track.tempo = value;
    track.tempoState.setCurrentAt(vm.tick(), value);
    out.tempo(tempoMicrosecondsPerQuarter(track.version, value));
  }

  void configureVibrato(u8 delay, u8 rate, u8 depth, u8 builtInFade) {
    track.vibrato.depthState.interruptFadeAutomationAt(vm.tick());
    track.vibrato.configure(delay, rate, depth);
    track.vibratoPhaseStep = vibrato::usesEarlyCounter(track.version) ? vibrato::earlyPhaseStep(rate, track.tempo) : 0;
    if (builtInFade != 0) {
      // In later versions a large first argument means "fade in over N ticks"
      // rather than "wait N ticks before starting."
      // Depth uses eight fractional bits so short fades can still make smooth
      // progress when the target byte is small.
      track.vibrato.depthState.configureLinearFade(builtInFade);
      beginVibratoFadeAutomation(builtInFade);
    }
    emitVibratoDelay();
    emitVibratoDepth(out, true);
    emitVibratoRate();
  }

  void setVibratoFade(u8 length) {
    track.vibrato.depthState.configureLinearFade(length);
    beginVibratoFadeAutomation(length);
  }

  void echo(u8 channels, u8 volumeLeft, u8 volumeRight) {
    program.setEcho(channels, volumeLeft, volumeRight);
    out.reverb(program.echo);
  }

  void echoParameters(u8 delay, u8 feedback, u8 filter) {
    program.setEchoParameters(delay, feedback, filter);
    if (program.echo.voiceMask != 0) {
      out.reverb(program.echo);
    }
  }

  void configurePortamento(u8 duration) {
    track.pitchEffect.kind = PersistentPitchEffect::Kind::Portamento;
    track.pitchEffect.duration = duration;
  }

  void configurePitchEnvelope(u8 delay, u8 duration, s16 depth) {
    track.pitchEffect.kind = PersistentPitchEffect::Kind::Envelope;
    track.pitchEffect.delay = delay;
    track.pitchEffect.duration = duration;
    track.pitchEffect.depth = depth;
  }

  void beginPitchSlide(PitchSlideKind kind, u8 delay, u8 length, u8 targetNote) {
    // A slide before any melodic note has no pitch to bend from. Keep the
    // default bend range but do not invent a starting note.
    if (!track.notePitch || length == 0) {
      out.pitchBendRange(2);
      return;
    }
    const double target = track.noteSemitones(targetNote, kind == PitchSlideKind::V1);
    if (!track.pitchNote.valid()) {
      return;
    }

    auto transitionOut = out.at(vm.tick() + delay);
    transitionOut.retargetPitchSlide(track.pitchNote, *track.notePitch, target, length);
  }

  void beginFade(FadeTarget target, bool stepBased, u8 destination, u8 ticks, s8 step) {
    const u8 clampedDestination = target == FadeTarget::Pan ? clampPan(track.version, destination) : destination;
    // Versions 1-4 provide a duration and let the driver calculate the step.
    // Versions 5-6 provide the signed step directly and stop at the target.
    const SequenceFixedPointMotionPlan<s32> motion =
        stepBased
            ? SequenceFixedPointMotion<s32>::toRawTargetByFixedStep(clampedDestination, static_cast<s32>(step) * 16)
            : SequenceFixedPointMotion<s32>::toRawTarget(clampedDestination, ticks);
    auto* state = &track.tempoState;
    auto automationTarget = PerformanceAutomationTarget::Tempo;
    double targetValue = 0.0;
    switch (target) {
      case FadeTarget::Tempo:
        targetValue = tempoMicrosecondsPerQuarter(track.version, clampedDestination);
        break;
      case FadeTarget::Volume:
        state = &track.volume;
        automationTarget = PerformanceAutomationTarget::Level;
        targetValue = mixedLevel(clampedDestination);
        break;
      case FadeTarget::Pan:
        state = &track.pan;
        automationTarget = PerformanceAutomationTarget::Pan;
        targetValue = stereoPositionFromPan(track.version, clampedDestination);
        break;
    }
    if (target == FadeTarget::Volume && track.currentNoteVolume == 0 && track.version >= KONAMISNES_V3) {
      // Preserve only the raw motion until a nonzero note makes the mixer write
      // VOL(L/R) again.
      state->interruptAutomationAt(vm.tick());
      static_cast<void>(state->begin(motion));
    } else {
      auto automation =
          stepBased ? out.step(automationTarget, targetValue, ticks) : out.fade(automationTarget, targetValue, ticks);
      static_cast<void>(state->begin(std::move(automation), motion));
    }
  }

  [[nodiscard]] Effects loopEnd(u8 slot, u8 times, s8 volumeDelta, s8 pitchDelta) {
    const Address destination = slot == 0 ? track.loopReturnAddress : track.loopReturnAddress2;
    if (destination.value == 0) {
      // A malformed end without a matching start falls through, matching the
      // driver's harmless behavior instead of jumping to address zero.
      return {};
    }
    if (times == 0) {
      // Zero means the song's repeating loop, not a 256-pass counted loop.
      // Volume and pitch changes belong only to finite repeats.
      return vm.declaredLoop(destination);
    }

    Effects effects = vm.countedRepeatUntil(slot, times, destination);
    s16& accumulatedVolume = slot == 0 ? track.loopVolumeDelta : track.loopVolumeDelta2;
    s16& accumulatedPitch = slot == 0 ? track.loopPitchDelta : track.loopPitchDelta2;
    if (!effects.flowOverride) {
      // Leaving the loop removes its accumulated changes so later notes start
      // from the values that were active before the loop.
      accumulatedVolume = 0;
      accumulatedPitch = 0;
    } else {
      // Apply the change once per replay, after the first pass has completed.
      accumulatedVolume += volumeDelta;
      accumulatedPitch += pitchDelta;
    }
    applyEffectiveTuning();
    emitLevel(out);
    return effects;
  }

  void beginVoltaLoop(Address start) {
    track.voltaLoopStart = start;
    track.voltaLoopEnd = {};
    track.voltaEndMeansPlayFromStart = false;
    track.voltaEndMeansPlayNextVolta = false;
  }

  [[nodiscard]] Effects voltaEnd(Address next) {
    if (track.voltaEndMeansPlayFromStart) {
      // The second marker closes the first ending and replays the shared part.
      track.voltaEndMeansPlayFromStart = false;
      track.voltaEndMeansPlayNextVolta = true;
      track.voltaLoopEnd = next;
      return vm.finiteBranch(track.voltaLoopStart);
    }

    if (track.voltaEndMeansPlayNextVolta) {
      // On replay, the first marker skips the ending that already played.
      track.voltaEndMeansPlayFromStart = true;
      track.voltaEndMeansPlayNextVolta = false;
      return vm.finiteBranch(track.voltaLoopEnd);
    }

    // The first marker begins the first ending; playback simply continues.
    track.voltaEndMeansPlayFromStart = true;
    return {};
  }

  [[nodiscard]] Effects endOrReturn() {
    // Opcode 0xff serves two roles. A call marks the track as being inside a
    // pattern; the next 0xff returns from it, while a top-level 0xff ends music.
    if (track.inSubroutine) {
      track.inSubroutine = false;
      return vm.return_();
    }
    return vm.end();
  }

  void tick() {
    // Commands start motion, but only note/rest time advances it. Emit changes
    // on each elapsed music tick so MIDI and event simulation see the ramp.
    static_cast<void>(track.tempoState.tickRaw([&](s32 rawTempo) {
      const u8 value = static_cast<u8>(std::clamp<s32>(rawTempo, 0, 0xff));
      if (value == track.tempo) {
        return;
      }
      track.tempo = value;
      track.tempoState.output(out).tempo(tempoMicrosecondsPerQuarter(track.version, value));
    }));
    static_cast<void>(track.volume.tickRaw([&](s32 rawVolume) {
      const auto value = static_cast<u8>(std::clamp<s32>(rawVolume, 0, 0xff));
      emitLevel(track.volume.output(out), value);
    }));
    static_cast<void>(track.pan.tickRaw([&](s32 rawPan) {
      const auto value = static_cast<u8>(std::clamp<s32>(rawPan, 0, 0xff));
      const PanGains gains = panGains(track.version, value);
      track.pan.output(out).stereoBalance(gains.left, gains.right);
    }));
    const auto fadeTick = track.vibrato.depthState.tickFade();
    if (fadeTick.shouldApply()) {
      emitVibratoDepth(track.vibrato.depthState.fadeOutput(out));
    }
    tickSoftwareRelease();
  }

private:
  [[nodiscard]] double mixedLevel(u8 trackVolume) const {
    const int effectiveNote = std::clamp<int>(track.currentNoteVolume + track.loopVolumeDelta + track.loopVolumeDelta2 -
                                                  static_cast<int>(track.softwareReleaseAttenuation >> 8),
                                              0, 127);
    const unsigned product = static_cast<unsigned>(effectiveNote) * trackVolume;
    const unsigned combined = product >> (track.version == KONAMISNES_V1 ? 7 : 8);
    const u8 attenuated = static_cast<u8>(std::clamp<int>(static_cast<int>(combined) - track.instrumentVolume, 0, 127));
    const u8 dspVolume = track.version == KONAMISNES_V1 ? attenuated : volumeTableValue(track.version, attenuated);
    return static_cast<double>(dspVolume) / 127.0;
  }

  void beginSoftwareRelease(u8 gateTicks) {
    track.softwareReleaseTicks =
        usesSoftwareRelease(track.version, track.envelope.releaseAmount) ? track.noteLength : 0;
    track.softwareReleaseGateTicks = gateTicks;
    track.softwareReleaseAttenuation = 0;
  }

  void stopSoftwareRelease() {
    track.softwareReleaseTicks = 0;
    track.softwareReleaseGateTicks = 0;
    track.softwareReleaseAttenuation = 0;
  }

  void tickSoftwareRelease() {
    // Early drivers suppress the last update before the next sequence event.
    if (track.softwareReleaseTicks == 0 || --track.softwareReleaseTicks == 0) {
      return;
    }
    if (track.softwareReleaseGateTicks != 0 && --track.softwareReleaseGateTicks != 0) {
      return;
    }
    const u8 amount = track.envelope.releaseAmount;
    const u8 previousAttenuation = static_cast<u8>(track.softwareReleaseAttenuation >> 8);
    track.softwareReleaseAttenuation = static_cast<u16>(track.softwareReleaseAttenuation + (amount << 4));
    const u8 attenuation = static_cast<u8>(track.softwareReleaseAttenuation >> 8);
    // The DSP voice remains keyed on until the update after software volume
    // reaches zero.
    static_cast<void>(out.setPreviousNoteEnd(vm.tick() + 1));
    if (attenuation >= track.currentNoteVolume) {
      emitLevel(out);
      track.softwareReleaseTicks = 0;
      return;
    }

    if (attenuation != previousAttenuation) {
      emitLevel(out);
    }
  }

  void emitLevel(PerformanceEmitter output) const {
    emitLevel(output, static_cast<u8>(std::clamp<s32>(track.volume.currentRaw(), 0, 0xff)));
  }

  void emitLevel(PerformanceEmitter output, u8 trackVolume) const {
    if (track.currentNoteVolume == 0 && track.version >= KONAMISNES_V3) {
      return;
    }
    output.level(LevelScale::linearFromLinear(mixedLevel(trackVolume)), LevelPrecisionHint::FourteenBit);
  }

  void loadInstrument(const std::optional<RuntimeInstrument>& instrument) {
    track.envelope.selectInstrument(track.version, instrument);
    track.instrumentVolume = instrument ? instrument->volume : 0;
    if (instrument && track.instrumentPanEnabled) {
      pan(instrument->pan);
    }
  }

  [[nodiscard]] std::optional<Envelope> currentEnvelope() const {
    if (!track.envelope.registers) {
      return std::nullopt;
    }
    const RawEnvelopeRegisters& registers = *track.envelope.registers;
    if ((registers.adsr1 & 0x80) != 0 && !registers.adsr2) {
      return std::nullopt;
    }
    if ((registers.adsr1 & 0x80) == 0 && !registers.gain) {
      return std::nullopt;
    }
    Envelope envelope = snesDspEnvelope(registers.adsr1, registers.adsr2.value_or(0), registers.gain.value_or(0));
    if (const auto release = dspReleaseSeconds(track.version, track.envelope.releaseAmount)) {
      envelope.releaseSeconds = *release;
    }
    return envelope;
  }

  void restorePostReleaseEnvelope() {
    if (!track.envelope.dspReleasePendingRestore) {
      return;
    }
    track.envelope.dspReleasePendingRestore = false;
    if (!track.envelope.registers || (track.envelope.registers->adsr1 & 0x80) != 0 || !track.envelope.savedCompanion) {
      return;
    }
    // DSP decrease-GAIN temporarily replaces the live GAIN register at key-off.
    // A later attack restores the saved companion, but only in GAIN mode.
    if (track.envelope.registers->gain != track.envelope.savedCompanion) {
      track.envelope.registers->gain = track.envelope.savedCompanion;
      replaceCurrentEnvelope();
    }
  }

  void applyPitchEffectToNote(u8 key, PerformanceNoteId continuedNote, std::optional<double> realizedPitch) {
    auto& effect = track.pitchEffect;
    if (track.percussion) {
      effect.previousKey.reset();
      return;
    }

    const double target = track.noteSemitones(key, false);
    if (effect.duration != 0) {
      switch (effect.kind) {
        case PersistentPitchEffect::Kind::Portamento:
          if (effect.previousKey) {
            const double start = realizedPitch.value_or(*effect.previousKey);
            if (track.version <= KONAMISNES_V2) {
              // Early engines count down a linear, tempo-relative glide.
              out.pitchSlide(track.pitchNote, start, target, PitchSlideTiming::fromTicks(effect.duration))
                  .continueFrom(continuedNote);
            } else {
              beginProportionalPortamento(start, target, effect.duration, continuedNote);
            }
          }
          break;
        case PersistentPitchEffect::Kind::Envelope:
          // F1's delay and duration counters advance with the sequence.
          out.at(vm.tick() + effect.delay)
              .pitchSlide(track.pitchNote, target - effect.depth, target, PitchSlideTiming::fromTicks(effect.duration));
          break;
        case PersistentPitchEffect::Kind::None:
          break;
      }
    }
    effect.previousKey = target;
  }

  void beginProportionalPortamento(double startKey, double targetKey, u8 speed, PerformanceNoteId continuedNote) {
    constexpr double pitchScale = 256.0;
    const s32 start = static_cast<s32>(std::lround(startKey * pitchScale));
    const s32 target = static_cast<s32>(std::lround(targetKey * pitchScale));
    if (speed == 0 || start == target) {
      return;
    }
    std::vector<s32> curve{start};
    while (curve.back() != target) {
      const s32 difference = target - curve.back();
      // At 250 Hz the driver moves speed/256 of the remaining 8.8 pitch
      // distance, but always advances by at least one unit.
      const s32 step = std::max<s32>((std::abs(difference) * speed) >> 8, 1);
      curve.push_back(curve.back() + (difference < 0 ? -step : step));
    }

    const u32 updates = static_cast<u32>(curve.size() - 1);
    const double milliseconds = updates * (1000.0 / kKonamiSnesTimerHz);
    const double tickMilliseconds = sequenceTickSeconds(track.version, track.tempo) * 1000.0;
    const u32 timelineTicks = std::max<u32>(static_cast<u32>(std::lround(milliseconds / tickMilliseconds)), 1);
    auto transition = out.pitchSlide(track.pitchNote, curve.front() / pitchScale, curve.back() / pitchScale,
                                     PitchSlideTiming::fixedDuration(timelineTicks, milliseconds));
    transition.continueFrom(continuedNote);

    // MIDI cannot place the 250 Hz hardware updates between sequence ticks.
    // Sample the exact integer curve at the nearest representable positions,
    // preserving both its strongly front-loaded shape and its total duration.
    for (u32 tick = 0; tick <= timelineTicks; ++tick) {
      const size_t sample = static_cast<size_t>(std::lround(static_cast<double>(tick) * updates / timelineTicks));
      transition.sample(out.at(vm.tick() + tick), curve[sample] / pitchScale);
    }
  }

  void applyEffectiveTuning(bool force = false) {
    const double cents = track.totalTuningCents();
    const bool emitted = std::isfinite(track.lastEmittedTuningCents);
    const bool nonZero = std::abs(cents) > 0.001;
    const bool changed = emitted && std::abs(track.lastEmittedTuningCents - cents) > 0.001;
    // Avoid redundant zero-tuning events, but force a repeat when a new sample
    // must inherit tuning that was already sent for the previous instrument.
    if ((!emitted && nonZero) || changed || (force && (emitted || nonZero))) {
      out.tuning(cents);
      track.lastEmittedTuningCents = cents;
    }
  }

  void resetPitchForNote(u8 key) {
    // The subsequent note emission cancels the preceding transition. Drum
    // notes have no melodic base for a later standalone slide.
    track.pitchNote = {};
    if (track.percussion) {
      track.notePitch.reset();
      return;
    }
    track.notePitch = track.noteSemitones(key);
  }

  void emitVibratoDepth(PerformanceEmitter output, bool force = false) {
    const bool active = vibratoActive();
    double depthSemitones = 0.0;
    if (active) {
      const double currentCents =
          vibrato::currentDepthCents(track.version, track.vibrato.depth, track.vibrato.currentDepthFixed());
      // The driver value is already the farthest pitch moves from the note.
      // Convert cents to semitones without reducing that distance again.
      depthSemitones = currentCents / 100.0;
    }
    track.vibrato.depthState.emitPhysicalDepth(
        depthSemitones, [&](double value) { output.vibratoDepth(value, vibratoLfoContext()); }, force, 0.0001);
  }

  [[nodiscard]] bool vibratoActive() const {
    if (track.vibrato.depth == 0) {
      return false;
    }
    return vibrato::usesEarlyCounter(track.version) ? vibrato::foldedPhaseStep(track.vibratoPhaseStep) != 0
                                                    : track.vibrato.rate != 0;
  }

  [[nodiscard]] LfoPerformanceContext vibratoLfoContext() const {
    return LfoPerformanceContext{
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        // The source phase resets to zero on a note. A phase step above $80
        // traverses that same triangle backward, so it starts center/falling.
        .initialPhaseCycles = vibrato::usesEarlyCounter(track.version) && track.vibratoPhaseStep > 0x80 ? 0.5 : 0.0,
    };
  }

  void beginVibratoFadeAutomation(u8 length) {
    if (length == 0) {
      track.vibrato.depthState.interruptFadeAutomationAt(vm.tick());
      return;
    }
    const double targetCents = vibrato::maxDepthCents(track.version, track.vibrato.depth);
    track.vibrato.depthState.bindFade(
        out.noteEnvelope(PerformanceAutomationTarget::VibratoDepth, targetCents / 100.0, length,
                         vibratoDelayTicks(track.version, track.vibrato.delay, track.tempo)));
  }

  void emitVibratoRate() {
    if (vibrato::usesEarlyCounter(track.version)) {
      const u16 factor = vibratoActive() ? static_cast<u16>(vibrato::foldedPhaseStep(track.vibratoPhaseStep)) << 8 : 0;
      out.vibratoRate(factor == 0 ? 0.0 : vibrato::baseHz(track.version) * factor, vibratoLfoContext());
      return;
    }
    const u16 factor = vibrato::rateFactor(track.version, track.vibrato.rate, track.tempo);
    out.vibratoRate(vibratoActive() && factor != 0 ? vibrato::baseHz(track.version) * factor : 0.0,
                    vibratoLfoContext());
  }

  void emitVibratoDelay() {
    if (!vibratoActive()) {
      // Clear both the simulation delay and the synth controller when vibrato
      // is disabled by either a zero rate or zero depth.
      out.vibratoDelay(0, 0);
      return;
    }
    if (vibrato::usesEarlyCounter(track.version)) {
      out.vibratoDelayTicks(vibratoDelayTicks(track.version, track.vibrato.delay, kKonamiSnesDefaultTempo));
      return;
    }
    out.vibratoDelayPhysical(vibratoDelayTicks(track.version, track.vibrato.delay, track.tempo),
                             vibrato::delaySeconds(track.version, track.vibrato.delay, track.tempo) * 1000.0);
  }
};

using KonamiCursor = CompilerCursor<TrackState, Playback>;

// Pitch slides can be standalone commands or an immediate suffix of a note or
// rest. This reads the version-specific suffix once into the containing event.
[[nodiscard]] DecodedPitchSlide readPitchSlide(KonamiCursor::Event& event, KonamiSnesVersion version) {
  DecodedPitchSlide slide{
      .kind = pitchSlideKind(version),
      .delay = event.u8("slide_delay"),
      .length = event.u8("slide_length", SemanticOperandRole::Duration),
      .targetNote = event.u8("target_note", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey),
  };
  // The driver carries a precalculated delta in later layouts. Playback derives
  // the same motion from target and length, but the original field is still
  // read and annotated so source inspection remains complete.
  if (slide.kind == PitchSlideKind::V2 && slide.length != 0) {
    event.u8("reserved", SourceValueDisplay::Hex);
    event.s16le("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
  } else if (slide.kind == PitchSlideKind::V3) {
    event.s16le("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
  }
  return slide;
}

[[nodiscard]] std::optional<DecodedPitchSlide> readInlinePitchSlide(KonamiCursor::Event& event,
                                                                    KonamiSnesVersion version) {
  if (event.peekU8() != 0xf3) {
    return std::nullopt;
  }
  event.u8("pitch_slide_opcode", SourceValueDisplay::Hex);
  return readPitchSlide(event, version);
}

void appendPitchSlide(KonamiCursor::Event& event, const DecodedPitchSlide& slide) {
  event.invoke<&Playback::beginPitchSlide>(slide.kind, slide.delay, slide.length, slide.targetNote);
}

[[nodiscard]] DecodedBytecodeCommand unknownCommand(KonamiCursor& cursor, u8 argumentCount) {
  // Known driver versions assign these slots different meanings. Preserve the
  // correct byte length without guessing at behavior that is not understood.
  auto event = cursor.sourceOnly("Unknown Event", "unknown");
  for (u8 index = 0; index < argumentCount; ++index) {
    event.u8(fmt::format("arg{}", index + 1), SourceValueDisplay::Hex);
  }
  return event.ignore();
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, KonamiSnesVersion version,
                                                   std::vector<Diagnostic>* diagnostics) {
  KonamiCursor cursor(reader, begin, "konami-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  const u8 opcode = cursor.opcode();
  // Notes use the opcode itself as the key. Opcodes below 0x60 include a new
  // length byte; opcodes 0x80-0xdf reuse the previous length with bit 7 set.
  if (opcode <= 0x5f || (opcode >= 0x80 && opcode <= 0xdf)) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 key = event.opcodeValue("key", static_cast<u8>(opcode & 0x7f), SourceValueDisplay::MidiNote,
                                     SemanticOperandRole::NoteKey);
    if ((opcode & 0x80) == 0) {
      event.set<&TrackState::noteLength>(event.u8("length", SemanticOperandRole::Duration));
    }
    u8 velocity = event.u8("velocity_or_duration", SemanticOperandRole::Level);
    // A clear high bit means this byte is a new duration rate and another byte
    // follows for velocity. A set high bit means it is velocity by itself.
    if ((velocity & 0x80) == 0) {
      const u8 rate = event.derived("duration_rate", velocity, SemanticOperandRole::Duration);
      event.set<&TrackState::noteDurationRate>(rate);
      velocity = event.u8("velocity", SemanticOperandRole::Level);
    }
    velocity &= 0x7f;
    event.invoke<&Playback::note>(key, velocity);
    if (const auto slide = readInlinePitchSlide(event, version)) {
      event.label("Note with Pitch Slide");
      appendPitchSlide(event, *slide);
    }
    return event.wait<&TrackState::noteLength>();
  }

  if (opcode >= 0x70 && opcode <= 0x7f && isLateVersion(version)) {
    // Later engines reserve this opcode range for a signed four-bit tuning
    // adjustment. Values 8-15 therefore represent -8 through -1.
    auto event = cursor.command("Instant Tuning", SequenceSemantic::Pitch);
    s8 tuning = opcode & 0x0f;
    if (tuning >= 8) {
      tuning -= 16;
    }
    event.opcodeValue("tuning", tuning);
    return event.invoke<&Playback::tuning>(tuning);
  }

  if (isTerminalLowOpcode(version, opcode)) {
    return cursor.command("Stop", SequenceSemantic::End).end();
  }

  switch (opcode) {
    case 0x60:
      return cursor.command("Percussion On", SequenceSemantic::Program).invoke<&Playback::percussionOn>();
    case 0x61:
      return cursor.command("Percussion Off", SequenceSemantic::Program).invoke<&Playback::percussionOff>();
    case 0x62:
      if (version == KONAMISNES_V5 || version == KONAMISNES_V6) {
        auto event = cursor.command("Release Rate", SequenceSemantic::Envelope);
        const u8 amount = event.u8("amount");
        if (amount >= 100) {
          event.derived("dsp_release_gain", releaseGain(amount), SourceValueDisplay::Hex);
        } else {
          event.derived("normal_key_off", true);
        }
        return event.invoke<&Playback::setRelease>(amount);
      }
      if (version == KONAMISNES_V1) {
        auto event = cursor.command("Default Duration", SequenceSemantic::State);
        return event.set<&TrackState::noteDurationRate>(
            event.u8("duration_rate", SemanticOperandRole::Duration));
      }
      return unknownCommand(cursor, 0);
    case 0x63: {
      if (version == KONAMISNES_V1) {
        auto event = cursor.command("Default Note Volume", SequenceSemantic::State);
        return event.set<&TrackState::defaultNoteVolume>(
            static_cast<u8>(event.u8("volume", SemanticOperandRole::Level) & 0x7f));
      }
      if (version == KONAMISNES_V6) {
        return cursor.command("Echo Voice On", SequenceSemantic::State).invoke<&Playback::setEchoVoice>(true);
      }
      return unknownCommand(cursor, 0);
    }
    case 0x64: {
      if (version == KONAMISNES_V1) {
        auto event = cursor.command("Default Duration And Note Volume", SequenceSemantic::State);
        const u8 rate = event.u8("duration_rate", SemanticOperandRole::Duration);
        const u8 volume = event.u8("volume", SemanticOperandRole::Level);
        return event.invoke<&Playback::setDefaults>(rate, volume);
      }
      if (version == KONAMISNES_V6) {
        return cursor.command("Echo Voice Off", SequenceSemantic::State).invoke<&Playback::setEchoVoice>(false);
      }
      return unknownCommand(cursor, 0);
    }
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6a:
    case 0x6b:
    case 0x6c:
    case 0x6d:
    case 0x6e:
    case 0x6f:
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7a:
    case 0x7b:
    case 0x7c:
    case 0x7d:
    case 0x7e:
    case 0x7f:
      return unknownCommand(cursor, 0);
    case 0xe0: {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      event.set<&TrackState::noteLength>(event.u8("length", SemanticOperandRole::Duration));
      if (const auto slide = readInlinePitchSlide(event, version)) {
        appendPitchSlide(event, *slide);
      }
      event.invoke<&Playback::rest>();
      return event.wait<&TrackState::noteLength>();
    }
    case 0xe1: {
      auto event = cursor.command("Tie", SequenceSemantic::Note);
      event.set<&TrackState::noteLength>(event.u8("length", SemanticOperandRole::Duration));
      const u8 rate = event.u8("duration_rate", SemanticOperandRole::Duration);
      event.set<&TrackState::noteDurationRate>(rate);
      event.invoke<&Playback::tie>();
      return event.wait<&TrackState::noteLength>();
    }
    case 0xe2: {
      auto event = cursor.command("Program", SequenceSemantic::Program);
      const u8 program = event.u8("raw");
      event.derived("bank", program >> 7, SemanticOperandRole::InstrumentBank);
      event.derived("program", program & 0x7f, SemanticOperandRole::InstrumentProgram);
      return event.invoke<&Playback::programChange>(program);
    }
    case 0xe3: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const u8 raw = event.u8("pan", SemanticOperandRole::Pan);
      const bool instrumentPanOff = version <= KONAMISNES_V2 ? raw == 0x15 : raw == 0x2a;
      const bool instrumentPanOn = version <= KONAMISNES_V2 ? raw == 0x16 : raw == 0x2c;
      // The two values immediately beyond the normal pan range toggle an
      // internal instrument-pan feature; they are not audible pan positions.
      event.derived("instrument_pan_off", instrumentPanOff);
      event.derived("instrument_pan_on", instrumentPanOn);
      if (instrumentPanOff || instrumentPanOn) {
        return event.set<&TrackState::instrumentPanEnabled>(instrumentPanOn);
      }
      return event.invoke<&Playback::pan>(raw);
    }
    case 0xe4: {
      auto event = cursor.command("Vibrato", SequenceSemantic::Modulation);
      const u8 arg1 = event.u8("delay_or_fade", SemanticOperandRole::Modulation);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 delay = event.derived("delay", vibrato::delayFromArg1(version, arg1));
      const u8 fade = event.derived("built_in_fade", vibrato::inlineFadeLength(version, arg1));
      return event.invoke<&Playback::configureVibrato>(delay, rate, depth, fade);
    }
    case 0xe5: {
      auto event = cursor.sourceOnly("Random Pitch");
      event.u8("rate");
      event.u16be("pitch_mask", SourceValueDisplay::Hex);
      return event.ignore();
    }
    case 0xe6: {
      auto event = cursor.command("Loop Start", SequenceSemantic::Loop);
      // Loop start has no operand. Its return address is simply the byte after
      // this opcode, recorded now so loop end can jump back during playback.
      const Address destination = event.derived("loop_start", event.nextAddress(), SourceValueDisplay::Address,
                                                SemanticOperandRole::LoopTarget);
      return event.set<&TrackState::loopReturnAddress>(destination);
    }
    case 0xe7:
    case 0xe9: {
      auto event = cursor.command(opcode == 0xe7 ? "Loop End" : "Loop End #2", SequenceSemantic::Repeat);
      const u8 slot = event.derived("slot", static_cast<u8>(opcode == 0xe7 ? 0 : 1));
      const u8 times = event.u8("times", SemanticOperandRole::Count);
      const s8 volumeDelta = event.s8("volume_delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 pitchDelta = event.s8("pitch_delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      return event.invokeFlow<&Playback::loopEnd>(slot, times, volumeDelta, pitchDelta);
    }
    case 0xe8: {
      auto event = cursor.command("Loop Start #2", SequenceSemantic::Loop);
      const Address destination = event.derived("loop_start", event.nextAddress(), SourceValueDisplay::Address,
                                                SemanticOperandRole::LoopTarget);
      return event.set<&TrackState::loopReturnAddress2>(destination);
    }
    case 0xea: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u8 tempo = event.u8("raw");
      event.derived("tempo", tempoBeatsPerMinute(tempoMicrosecondsPerQuarter(version, tempo)),
                    SourceValueDisplay::BeatsPerMinute);
      return event.invoke<&Playback::tempo>(tempo);
    }
    case 0xeb:
    case 0xef:
    case 0xf8: {
      const FadeTarget target =
          opcode == 0xeb ? FadeTarget::Tempo : (opcode == 0xef ? FadeTarget::Volume : FadeTarget::Pan);
      const SequenceSemantic semantic =
          opcode == 0xeb ? SequenceSemantic::Tempo : (opcode == 0xef ? SequenceSemantic::Level : SequenceSemantic::Pan);
      auto event =
          cursor.command(opcode == 0xeb ? "Tempo Fade" : (opcode == 0xef ? "Volume Fade" : "Pan Fade"), semantic);
      // The command stays two operands long in every version, but their meaning
      // changes from duration/target to target/signed-step in versions 5-6.
      if (isLateVersion(version)) {
        const u8 destination = event.u8("target");
        const s8 step = event.s8("step");
        return event.invoke<&Playback::beginFade>(target, true, destination, u8{0}, step);
      }
      const u8 ticks = event.u8("length", SemanticOperandRole::Duration);
      const u8 destination = event.u8("target");
      return event.invoke<&Playback::beginFade>(target, false, destination, ticks, s8{0});
    }
    case 0xec: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(
          event.s8("semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case 0xed:
      if (isLateVersion(version)) {
        auto event = cursor.command("ADSR(1)", SequenceSemantic::Envelope);
        return event.invoke<&Playback::setAdsr1>(event.u8("adsr1", SourceValueDisplay::Hex));
      }
      return unknownCommand(cursor, 3);
    case 0xee: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xf0: {
      auto event = cursor.command("Portamento", SequenceSemantic::Pitch);
      return event.invoke<&Playback::configurePortamento>(event.u8("speed", SemanticOperandRole::Duration));
    }
    case 0xf1: {
      auto event = cursor.command("Pitch Envelope", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      if (isLateVersion(version)) {
        const u8 length = event.u8("length", SemanticOperandRole::Duration);
        const s8 offset = event.s8("offset", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
        // This shapes the exact late curve; fixed-duration playback currently
        // retains its endpoints and timing while that curve remains deferred.
        event.s16le("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
        return event.invoke<&Playback::configurePitchEnvelope>(delay, length, static_cast<s16>(offset));
      }
      const u8 speed = event.u8("speed", SemanticOperandRole::Duration);
      const u8 depth = event.u8("depth", SemanticOperandRole::Pitch);
      return event.invoke<&Playback::configurePitchEnvelope>(delay, speed, static_cast<s16>(depth));
    }
    case 0xf2: {
      auto event = cursor.command("Tuning", SequenceSemantic::Pitch);
      return event.invoke<&Playback::tuning>(
          event.s8("tuning", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case 0xf3: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Pitch);
      appendPitchSlide(event, readPitchSlide(event, version));
      return event;
    }
    case 0xf4: {
      auto event = cursor.command("Echo", SequenceSemantic::State);
      const u8 channels = event.u8("channels", SourceValueDisplay::Hex);
      const u8 volumeLeft = event.u8("volume_left");
      const u8 volumeRight = event.u8("volume_right");
      return event.invoke<&Playback::echo>(channels, volumeLeft, volumeRight);
    }
    case 0xf5: {
      auto event = cursor.command("Echo Parameters", SequenceSemantic::State);
      const u8 delay = event.u8("delay");
      const u8 feedback = event.u8("feedback");
      const u8 filter = event.u8("filter_or_ignored", SourceValueDisplay::Hex);
      return event.invoke<&Playback::echoParameters>(delay, feedback, filter);
    }
    case 0xf6: {
      auto event = cursor.command("Loop With Volta Start", SequenceSemantic::Repeat);
      const Address start = event.derived("loop_start", event.nextAddress(), SourceValueDisplay::Address,
                                          SemanticOperandRole::RepeatTarget);
      return event.invoke<&Playback::beginVoltaLoop>(start);
    }
    case 0xf7: {
      auto event = cursor.command("Loop With Volta End", SequenceSemantic::Repeat);
      return event.invokeFlow<&Playback::voltaEnd>(event.nextAddress());
    }
    case 0xf9: {
      auto event = cursor.command("Vibrato Fade", SequenceSemantic::Modulation);
      return event.invoke<&Playback::setVibratoFade>(event.u8("length", SemanticOperandRole::Duration));
    }
    case 0xfa: {
      auto event = cursor.command("ADSR / GAIN", SequenceSemantic::Envelope);
      const u8 arg1 = event.u8(usesEncodedEnvelopeParameters(version) ? "attack_decay_parameter" : "adsr1",
                               SourceValueDisplay::Hex);
      const u8 arg2 = event.u8(usesEncodedEnvelopeParameters(version) ? "sustain_parameter" : "adsr2_or_gain",
                               SourceValueDisplay::Hex);
      const u8 releaseAmount = event.u8("release_amount");
      const u8 adsr1 = usesEncodedEnvelopeParameters(version) ? snesDspKonamiAdsr1(arg1) : arg1;
      const u8 adsr2OrGain =
          usesEncodedEnvelopeParameters(version) && (adsr1 & 0x80) != 0 ? snesDspKonamiAdsr2(arg2) : arg2;
      event.derived("dsp_adsr1", adsr1, SourceValueDisplay::Hex);
      if ((adsr1 & 0x80) != 0) {
        event.derived("dsp_adsr2", adsr2OrGain, SourceValueDisplay::Hex);
      } else {
        event.derived("dsp_gain", adsr2OrGain, SourceValueDisplay::Hex);
      }
      if (version >= KONAMISNES_V3 && releaseAmount >= 100) {
        event.derived("dsp_release_gain", releaseGain(releaseAmount), SourceValueDisplay::Hex);
      }
      return event.invoke<&Playback::setEnvelope>(adsr1, adsr2OrGain, releaseAmount);
    }
    case 0xfb:
      if (isLateVersion(version)) {
        auto event = cursor.command("ADSR(2)", SequenceSemantic::Envelope);
        return event.invoke<&Playback::setAdsr2>(event.u8("adsr2", SourceValueDisplay::Hex));
      }
      if (version == KONAMISNES_V4) {
        auto event = cursor.sourceOnly("Linear Pitch Envelope");
        event.u8("delta_fraction");
        event.s8("delta_integer", SourceValueDisplay::SignedDecimal);
        return event.ignore();
      } else {
        auto event = cursor.sourceOnly("Pitch Modulation");
        event.u8("voice_mask", SourceValueDisplay::Hex);
        return event.ignore();
      }
    case 0xfc:
      // Konami repeatedly reassigned opcode 0xfc. Keep each version next to the
      // others so its changing operand length and behavior are easy to compare.
      if (version == KONAMISNES_V1) {
        auto event = cursor.command("Conditional Jump", SequenceSemantic::Jump);
        const Address destination = event.addressLe("destination", SemanticOperandRole::JumpTarget);
        const Address alternate = event.addressLe("alternate_destination", SemanticOperandRole::JumpTarget);
        event.loopCandidate(destination);
        return event.discoverTarget(alternate);
      }
      if (version >= KONAMISNES_V2 && version <= KONAMISNES_V4) {
        auto event = cursor.sourceOnly("Linear Pitch Envelope");
        event.u8("delta_fraction");
        event.s8("delta_integer", SourceValueDisplay::SignedDecimal);
        return event.ignore();
      }
      if (version >= KONAMISNES_V5) {
        auto event = cursor.command("Program And Volume", SequenceSemantic::Program);
        const u8 volume = event.u8("volume", SemanticOperandRole::Level);
        const u8 program = event.u8("raw");
        event.derived("bank", program >> 7, SemanticOperandRole::InstrumentBank);
        event.derived("program", program & 0x7f, SemanticOperandRole::InstrumentProgram);
        return event.invoke<&Playback::programChangeAndVolume>(volume, program);
      }
      return unknownCommand(cursor, 2);
    case 0xfd: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      // Backward jumps normally form the song loop. The shared VM decides
      // whether to preserve, replay, or stop when it reaches that loop.
      return event.loopCandidate(event.addressLe("destination", SemanticOperandRole::LoopTarget));
    }
    case 0xfe: {
      auto event = cursor.command("Pattern Play", SequenceSemantic::Call);
      const Address destination = event.addressLe("destination", SemanticOperandRole::CallTarget);
      // The format uses the same 0xff opcode for return and track end, so retain
      // enough history to choose the right meaning when 0xff executes.
      event.set<&TrackState::inSubroutine>(true);
      return event.call(destination);
    }
    case 0xff: {
      auto event = cursor.command("End", SequenceSemantic::End);
      event.invoke<&Playback::endOrReturn>();
      return event.discoverReturn();
    }
    default:
      return cursor.unsupported("Unknown Opcode", "unknown").stop();
  }
}

[[nodiscard]] SequenceDialect makeDialect(KonamiSnesVersion version) {
  const PanGains initialBalance = panGains(version, version <= KONAMISNES_V2 ? 10 : 20);
  return SequenceDialect{
      .commandDetailKindPrefix = "konami-snes",
      .timebase = Timebase{.ppqn = kKonamiSnesPpqn},
      .behavior =
          SequenceProgramBehavior{
              .inferLoopsFromRepeatedState = false,
              .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
              .initialLevel = 0.0,
              .initialReverbSend = 0.0,
              .initialStereoBalance = StereoBalance{initialBalance.left, initialBalance.right},
              .initialPitchBendRangeSemitones = 2,
              .initialTempoMicrosecondsPerQuarter = tempoMicrosecondsPerQuarter(version, kKonamiSnesDefaultTempo),
          },
  };
}

}  // namespace

const SequenceDialect& konamiSnesSequenceDialect(KonamiSnesVersion version) {
  static const SequenceDialect none = makeDialect(KONAMISNES_NONE);
  static const SequenceDialect v1 = makeDialect(KONAMISNES_V1);
  static const SequenceDialect v2 = makeDialect(KONAMISNES_V2);
  static const SequenceDialect v3 = makeDialect(KONAMISNES_V3);
  static const SequenceDialect v4 = makeDialect(KONAMISNES_V4);
  static const SequenceDialect v5 = makeDialect(KONAMISNES_V5);
  static const SequenceDialect v6 = makeDialect(KONAMISNES_V6);
  switch (version) {
    case KONAMISNES_V1:
      return v1;
    case KONAMISNES_V2:
      return v2;
    case KONAMISNES_V3:
      return v3;
    case KONAMISNES_V4:
      return v4;
    case KONAMISNES_V5:
      return v5;
    case KONAMISNES_V6:
      return v6;
    case KONAMISNES_NONE:
      return none;
  }
  return none;
}

SequenceRuntime konamiSnesSequenceRuntime(KonamiSnesVersion version, bool indexedEchoFilter) {
  return makeCompiledRuntime<TrackState, Playback, ProgramState>(
      RuntimeConfig{.version = version, .indexedEchoFilter = indexedEchoFilter});
}

TrackProgram decodeKonamiSnesSourceTrack(ByteReader reader, KonamiSnesVersion version, u32 sourceTrackNumber,
                                         u32 startAddress, SourceMapBuilder* sourceMap,
                                         std::vector<Diagnostic>* diagnostics,
                                         std::optional<SourceAnnotationId> parentAnnotation,
                                         std::optional<AssetId> sequenceAsset) {
  // The shared track walker follows calls and branch targets discovered by each
  // command, then stores every reachable block in one source-free program.
  const TrackDecodeScope tracks{
      .reader = reader,
      .maxCommands = kMaxTrackCommands,
      .sequenceAsset = sequenceAsset,
      .parentAnnotation = parentAnnotation,
      .sourceMap = sourceMap,
  };
  return tracks.linear(sourceTrackNumber, startAddress,
                       [&](u32 offset) { return decodeCommand(reader, offset, version, diagnostics); });
}

SourceRange konamiSnesSequenceHeaderRange(ByteReader reader, const KonamiSnesLayout& layout) {
  // There is no track-count byte. Track data begins immediately after the
  // pointer list, so the first pointer that lands inside the possible 16-byte
  // header reveals how many two-byte entries precede it.
  u32 trackCount = kKonamiSnesMaxTracks;
  for (u32 trackNumber = 0; trackNumber < kKonamiSnesMaxTracks; ++trackNumber) {
    const u32 pointerOffset = layout.sequenceHeaderAddress + trackNumber * 2;
    if (!reader.has(pointerOffset, 2)) {
      trackCount = trackNumber;
      break;
    }
    const u16 trackAddress = reader.le16(pointerOffset);
    if (trackAddress >= layout.sequenceHeaderAddress &&
        trackAddress - layout.sequenceHeaderAddress < kKonamiSnesMaxTracks * 2) {
      trackCount = (trackAddress - layout.sequenceHeaderAddress) / 2;
      break;
    }
  }
  return reader.range(layout.sequenceHeaderAddress, trackCount * 2);
}

SequenceProgram decodeKonamiSnesSequence(ByteReader reader, const KonamiSnesLayout& layout, AssetId sequenceId,
                                         SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const SourceRange headerRange = konamiSnesSequenceHeaderRange(reader, layout);
  const u32 trackCount = headerRange.size / 2;

  const auto& dialect = konamiSnesSequenceDialect(layout.version);
  SequenceDecodeSession sequence{
      reader, dialect, sequenceId, headerRange, sourceMap, kMaxTrackCommands,
  };
  const auto decode = [&](u32 offset) { return decodeCommand(reader, offset, layout.version, diagnostics); };
  for (u32 trackNumber = 0; trackNumber < trackCount; ++trackNumber) {
    const u32 pointerOffset = layout.sequenceHeaderAddress + trackNumber * 2;
    const u16 trackAddress = reader.le16(pointerOffset);
    if (trackAddress == 0 || !reader.has(trackAddress, 1)) {
      continue;
    }
    sequence.addLinearTrack(trackNumber, reader.range(pointerOffset, 2), trackAddress, decode);
  }

  RuntimeConfig runtime{
      .version = layout.version,
      .indexedEchoFilter = layout.indexedEchoFilter,
      .instruments = std::vector<u32>(kInstrumentEnvelopeCount * kRuntimeInstrumentWords, kUnknownRuntimeInstrument),
  };
  for (const auto& instrument : parseKonamiSnesInstrumentInfos(reader, layout)) {
    const size_t index = instrument.percussion ? kPercussionEnvelopeBase + instrument.percussionNote : instrument.index;
    const size_t offset = index * kRuntimeInstrumentWords;
    runtime.instruments[offset] =
        (static_cast<u32>(instrument.adsr1) << 16) | (static_cast<u32>(instrument.adsr2) << 8) | instrument.gain;
    runtime.instruments[offset + 1] = (static_cast<u32>(instrument.pan) << 8) | instrument.volume;
  }
  return sequence.finish(makeCompiledRuntime<TrackState, Playback, ProgramState>(std::move(runtime)));
}

}  // namespace vgmtrans::formats::konami_snes
