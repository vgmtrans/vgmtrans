/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HudsonSnes/HudsonSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vgmtrans::formats::hudson_snes {

using namespace core;

namespace {

constexpr std::array<u8, 8> kDurations{0xc0, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x01};
constexpr std::array<u8, 31> kPanTable{0x00, 0x07, 0x0d, 0x14, 0x1a, 0x21, 0x27, 0x2e, 0x34, 0x3a, 0x40,
                                       0x45, 0x4b, 0x50, 0x55, 0x5a, 0x5e, 0x63, 0x67, 0x6b, 0x6e, 0x71,
                                       0x74, 0x77, 0x79, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f};
constexpr std::array<u8, 26> kTempoAdjustment{0x00, 0x02, 0x04, 0x05, 0x07, 0x09, 0x0b, 0x0d, 0x0e,
                                              0x10, 0x12, 0x14, 0x16, 0x17, 0x19, 0x1b, 0x1d, 0x1f,
                                              0x21, 0x22, 0x24, 0x26, 0x28, 0x2a, 0x2b, 0x2d};
constexpr std::array<u8, 91> kV1VolumeTable{
    0x00, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04,
    0x04, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09, 0x09, 0x0a, 0x0a, 0x0b, 0x0b, 0x0c,
    0x0d, 0x0e, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x1a, 0x1b, 0x1d, 0x1e, 0x20, 0x22, 0x24,
    0x26, 0x28, 0x2b, 0x2d, 0x30, 0x33, 0x36, 0x39, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x51, 0x55, 0x5a, 0x60, 0x66, 0x6c,
    0x72, 0x79, 0x80, 0x87, 0x8f, 0x98, 0xa1, 0xaa, 0xb5, 0xbf, 0xcb, 0xd7, 0xe3, 0xf1, 0xff,
};

// Hudson 2.x maps the post-velocity/master volume through this curve before
// applying pan. The same 80-byte table is present in every audited 2.x driver.
constexpr std::array<u8, 80> kV2MixerCurve{
    0x00, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08,
    0x09, 0x09, 0x0a, 0x0a, 0x0b, 0x0b, 0x0c, 0x0d, 0x0e, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x17, 0x18, 0x1a, 0x1b, 0x1d, 0x1e, 0x20, 0x22, 0x24, 0x26, 0x28, 0x2b, 0x2d, 0x30, 0x33,
    0x36, 0x39, 0x3d, 0x40, 0x44, 0x48, 0x4c, 0x51, 0x56, 0x5b, 0x60, 0x66, 0x6c, 0x72, 0x79, 0x80,
};

constexpr u32 kInstrumentWords = 128;
constexpr u32 kWaveformBase = kInstrumentWords;
constexpr u32 kPitchDescriptorBase = kWaveformBase + 128;
constexpr u32 kDrumBase = kPitchDescriptorBase + 128;
constexpr u32 kEchoBase = kDrumBase + 128;
constexpr u32 kVolumeDescriptorBase = kEchoBase + 2;
constexpr u32 kPayloadBase = kVolumeDescriptorBase + 128;
constexpr u32 kMissingInstrument = 0xffffffffu;

struct RuntimeInstrument {
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
};

struct RuntimeDrum {
  u8 program = 0;
  u8 sourceKey = 0;
  u8 volume = 0;
  u8 pan = 15;
};

namespace math {

[[nodiscard]] u8 initialVolume(Version version) {
  return version == Version::V2 ? 0x48 : 100;
}

[[nodiscard]] u8 maximumVolume(Version version) {
  return version == Version::V2 ? 0x4f : 0xff;
}

[[nodiscard]] u8 clippedVolume(Version version, u8 value) {
  return version == Version::V2 && (value & 0x80) != 0 ? 0 : std::min(value, maximumVolume(version));
}

[[nodiscard]] u8 tempoStep(u8 rawTempo, u8 timebaseShift) {
  const u8 tempo = std::max<u8>(rawTempo, 10);
  const u8 decade = std::min<u8>(tempo / 10, static_cast<u8>(kTempoAdjustment.size() - 1));
  u8 step = static_cast<u8>(tempo - kTempoAdjustment[decade]);
  if (tempo % 10 >= 5 && step != 0) {
    --step;
  }
  return std::max<u8>(static_cast<u8>(step >> timebaseShift), 1);
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u8 rawTempo, u8 timebaseShift) {
  // Timer 0 runs at 250 Hz. The tempo accumulator advances by step/256 per
  // timer tick, and one driver music tick represents 2^timebase normalized
  // ticks at the fixed 48 PPQN used by the value dialect.
  const double result = static_cast<double>(kPpqn) * 256.0 * 1'000'000.0 /
                        (250.0 * math::tempoStep(rawTempo, timebaseShift) * (1u << timebaseShift));
  return std::max<u32>(1, static_cast<u32>(std::lround(result)));
}

[[nodiscard]] double noteVelocityGain(Version version, u8 velocity) {
  return version == Version::V2 ? (std::min<u8>(velocity, 127) + 1) / 128.0 : 1.0;
}

[[nodiscard]] double v2MixerGain(u8 volume, u8 velocity) {
  const u32 velocityFactor = std::min<u8>(velocity, 127) + 1u;
  const size_t index = std::min<size_t>((2u * volume * velocityFactor) >> 8, kV2MixerCurve.size() - 1);
  return kV2MixerCurve[index] / 128.0;
}

[[nodiscard]] double levelGain(Version version, u8 volume, u8 velocity = 127) {
  if (version != Version::V2) {
    return volume / static_cast<double>(maximumVolume(version));
  }

  // Note velocity is a separate performance factor. Emit the residual channel
  // gain so their product is the driver's curved post-velocity mixer value.
  return v2MixerGain(volume, velocity) / noteVelocityGain(version, velocity);
}

[[nodiscard]] StereoBalance panGains(u8 raw) {
  const u8 pan = std::min<u8>(raw & 0x1f, 30);
  return StereoBalance{
      .leftGain = kPanTable[pan] / 127.0,
      .rightGain = kPanTable[30 - pan] / 127.0,
  };
}

[[nodiscard]] double driverTickMilliseconds(u8 tempo, u8 timebaseShift) {
  return 1000.0 * 256.0 / (250.0 * tempoStep(tempo, timebaseShift));
}

[[nodiscard]] u32 timelineTicksForMilliseconds(double milliseconds, u8 tempo, u8 timebaseShift) {
  const double tickMilliseconds = driverTickMilliseconds(tempo, timebaseShift) / (1u << timebaseShift);
  return std::max<u32>(1, static_cast<u32>(std::ceil(milliseconds / tickMilliseconds)));
}

[[nodiscard]] u32 pitchScriptDuration(u8 duration) {
  return duration == 0 ? 256 : duration;
}

[[nodiscard]] Envelope envelope(RuntimeInstrument instrument) {
  Envelope result = snesDspEnvelope(instrument.adsr1, instrument.adsr2, instrument.gain);
  // Hudson keys notes off by writing GAIN and clearing ADSR1 bit 7. Preserve
  // that pseudo-release instead of the generic SNES ADSR release estimate.
  result.releaseSeconds = snesDspGainEnvelopeSeconds(instrument.gain, 0x7ff, 0);
  return result;
}

}  // namespace math

[[nodiscard]] std::optional<RuntimeInstrument> runtimeInstrument(const std::vector<u32>& data, u8 program) {
  if (program >= kInstrumentWords || program >= data.size() || data[program] == kMissingInstrument) {
    return std::nullopt;
  }
  return RuntimeInstrument{
      .adsr1 = static_cast<u8>(data[program] >> 16),
      .adsr2 = static_cast<u8>(data[program] >> 8),
      .gain = static_cast<u8>(data[program]),
  };
}

struct ProgramState {
  explicit ProgramState(const SequenceProgram& sequence)
      : version(static_cast<Version>(sequence.config.profile)), timebaseShift(sequence.config.driverState & 3),
        initialEchoMask(static_cast<u8>(sequence.config.driverState >> 16)), data(sequence.config.driverData) {
    echo.voiceMask = initialEchoMask;
    if (data.size() > kEchoBase + 1) {
      echo.leftGain = static_cast<s8>(static_cast<u8>(data[kEchoBase] >> 24)) / 127.0;
      echo.rightGain = static_cast<s8>(static_cast<u8>(data[kEchoBase] >> 16)) / 127.0;
      echo.delayMilliseconds = ((data[kEchoBase] >> 8) & 0x0f) * 16.0;
      echo.feedback = static_cast<s8>(static_cast<u8>(data[kEchoBase])) / 128.0;
      echo.filterIndex = static_cast<u8>(data[kEchoBase + 1]);
      echo.send = std::min(1.0, std::max(std::abs(*echo.leftGain), std::abs(*echo.rightGain)));
    }
  }

  [[nodiscard]] std::optional<RuntimeInstrument> instrument(u8 program) const {
    return runtimeInstrument(data, program);
  }

  [[nodiscard]] u32 waveform(u8 index) const {
    const u32 address = kWaveformBase + index;
    return address < data.size() ? data[address] : 0;
  }

  [[nodiscard]] std::span<const u32> waveformSamples(u8 index) const {
    const u32 descriptor = waveform(index);
    const u32 offset = descriptor >> 16;
    const u32 count = descriptor & 0xff;
    if (count == 0 || offset > data.size() || count > data.size() - offset) {
      return {};
    }
    return std::span<const u32>{data.data() + offset, count};
  }

  [[nodiscard]] std::optional<RuntimeDrum> drum(u8 note) const {
    const u32 address = kDrumBase + note;
    if (address >= data.size() || data[address] == kMissingInstrument) {
      return std::nullopt;
    }
    const u32 value = data[address];
    return RuntimeDrum{
        .program = static_cast<u8>(value >> 24),
        .sourceKey = static_cast<u8>(value >> 16),
        .volume = static_cast<u8>(value >> 8),
        .pan = static_cast<u8>(value),
    };
  }

  [[nodiscard]] std::span<const u32> pitchScript(u8 index) const {
    const u32 descriptorAddress = kPitchDescriptorBase + index;
    if (descriptorAddress >= data.size()) {
      return {};
    }
    const u32 descriptor = data[descriptorAddress];
    const u32 offset = descriptor >> 8;
    const u32 count = descriptor & 0xff;
    if (count == 0 || offset > data.size() || count > data.size() - offset) {
      return {};
    }
    return std::span<const u32>{data.data() + offset, count};
  }

  [[nodiscard]] std::span<const u32> volumeCurve(u8 index) const {
    const u32 descriptorAddress = kVolumeDescriptorBase + index;
    if (descriptorAddress >= data.size()) {
      return {};
    }
    const u32 descriptor = data[descriptorAddress];
    const u32 offset = descriptor >> 8;
    const u32 count = descriptor & 0xff;
    if (count == 0 || offset > data.size() || count > data.size() - offset) {
      return {};
    }
    return std::span<const u32>{data.data() + offset, count};
  }

  Version version = Version::Early;
  u8 timebaseShift = 2;
  u8 initialEchoMask = 0;
  u8 tempo = 120;
  std::array<u8, 256> registers{};
  bool zero = true;
  bool negative = false;
  bool carry = false;
  ReverbPerformanceEvent echo{.voiceMask = 0, .send = 0.0};
  const std::vector<u32>& data;
};

struct TrackState {
  TrackState(const SequenceProgram& sequence, const TrackProgram& sourceTrack)
      : version(static_cast<Version>(sequence.config.profile)), timebaseShift(sequence.config.driverState & 3),
        velocityEnabled((sequence.config.driverState & 0x100) != 0), volume(math::initialVolume(version)),
        initialEcho((sequence.config.driverState & (0x10000u << sourceTrack.sourceTrackNumber)) != 0),
        voiceBit(static_cast<u8>(1u << sourceTrack.sourceTrackNumber)), loopPoint(sourceTrack.startAddress) {
    if (const auto instrument = runtimeInstrument(sequence.config.driverData, 0)) {
      envelope = *instrument;
    }
  }

  Version version = Version::Early;
  u8 timebaseShift = 2;
  s32 octave = 2;
  u8 quantize = 8;
  u8 sourceProgram = 0;
  bool percussion = false;
  bool initialized = false;
  bool velocityEnabled = false;
  u8 velocity = 127;
  u8 volume = 100;
  u8 pan = 15;
  u8 reversePhase = 0;
  bool initialEcho = false;
  u8 voiceBit = 1;
  s8 fineTuning = 0;
  s16 transpose = 0;
  std::optional<RuntimeInstrument> envelope;

  bool previousSlurred = false;
  bool previousWasRest = true;
  bool shortLengthFlip = false;
  PerformanceNoteId lastNote;
  std::optional<double> lastKey;

  u8 vibratoRate = 0;
  u8 vibratoDepth = 0;
  u8 vibratoMode = 0;
  u8 vibratoDelay = 0;
  u8 vibratoType = 0;
  bool tremolo = false;

  bool attackEnvelope = false;
  u8 attackSpeed = 0;
  u8 attackDepth = 0;
  u8 attackDirection = 0;
  u8 pitchScript = 0;
  u8 pitchScriptDelay = 0;
  u8 pitchScriptScale = 2;
  s8 noteVolumeCurve = -1;
  std::optional<u8> currentSourceNote;

  u8 portamentoSpeed = 0;
  std::array<Address, 6> loopStarts{};
  std::array<u32, 6> loopPlays{};
  u8 loopDepth = 0;
  u8 callDepth = 0;
  Address loopPoint;
  bool loopPointOnce = false;

  u8 volumeSlideMagnitude = 0;
  bool volumeSlideDown = false;
  u32 volumeSlideInterval = 0;
  u32 volumeSlideCounter = 0;
  u16 volumeSlideAccumulator = 0;
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
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.sourceProgram});
    if (track.initialEcho) {
      out.reverb(program.echo);
    }
  }

  [[nodiscard]] u32 normalized(u32 driverTicks) const { return driverTicks << track.timebaseShift; }

  [[nodiscard]] u32 effectiveLength(u32 encodedDriverLength, bool alternatingShortest) {
    if (encodedDriverLength != 1 || !alternatingShortest) {
      return encodedDriverLength == 0 ? 256 : encodedDriverLength;
    }
    // The driver alternates the shortest table entry between one and two
    // ticks, giving the documented 1.5-tick average without fractional RAM.
    track.shortLengthFlip = !track.shortLengthFlip;
    return track.shortLengthFlip ? 2 : 1;
  }

  [[nodiscard]] u32 soundingTicks(u32 driverLength, bool noKeyoff) const {
    if (noKeyoff) {
      return normalized(driverLength);
    }
    u32 duration = 1;
    if ((track.quantize & 0x80) != 0) {
      duration = track.quantize & 0x7f;
      if (duration == 0) {
        duration = 256;
      }
    } else if (track.quantize <= 8) {
      duration = std::max<u32>((driverLength * track.quantize + 7) / 8, 1);
    } else {
      duration = std::max<s32>(static_cast<s32>(driverLength) - (track.quantize - 8), 1);
    }
    return normalized(duration);
  }

  [[nodiscard]] double currentVelocity() const { return math::noteVelocityGain(track.version, track.velocity); }

  void applyAttackEnvelope(double key) {
    if (!track.attackEnvelope || track.attackSpeed == 0 || track.attackDepth == 0) {
      return;
    }
    if ((track.attackDirection & 0x80) != 0) {
      const std::span<const u32> samples = program.waveformSamples(track.attackDirection & 0x7f);
      if (!samples.empty()) {
        const double updatesPerSample = 128.0 / track.attackSpeed;
        const double milliseconds = samples.size() * updatesPerSample * 4.0;
        const u32 ticks = math::timelineTicksForMilliseconds(milliseconds, program.tempo, track.timebaseShift);
        const auto offset = [&](u32 raw) { return static_cast<s8>(raw) * track.attackDepth / (256.0 * 127.0); };
        auto automation = out.pitchSlide(track.lastNote, key + offset(samples.front()), key + offset(samples.back()),
                                         PitchSlideTiming::fixedDuration(ticks, milliseconds));
        for (u32 index = 1; index < samples.size(); ++index) {
          const u32 tick = static_cast<u32>(std::lround(static_cast<double>(ticks) * index / samples.size()));
          automation.sample(out.at(vm.tick() + tick), key + offset(samples[index]));
        }
        automation.preferPitchBend();
        return;
      }
    }
    const double initial = (track.attackDirection == 0 ? 1.0 : -1.0) * track.attackDepth / 256.0;
    const double milliseconds = std::max<u32>(1, 128u - (track.attackSpeed & 0x7f)) * 4.0;
    const u32 ticks = math::timelineTicksForMilliseconds(milliseconds, program.tempo, track.timebaseShift);
    out.pitchSlide(track.lastNote, key + initial, key, PitchSlideTiming::fixedDuration(ticks, milliseconds))
        .preferPitchBend();
  }

  void applyPitchScript(double key) {
    const std::span<const u32> script = program.pitchScript(track.pitchScript);
    if (track.pitchScriptDelay == 0 || script.empty() || !track.lastNote.valid()) {
      return;
    }
    const u32 scale = 1u << track.timebaseShift;
    const u32 delay = track.pitchScriptDelay * scale;
    u32 duration = 0;
    for (const u32 step : script) {
      duration += math::pitchScriptDuration(static_cast<u8>(step >> 8)) * scale;
    }
    const double width = std::min<u8>(track.pitchScriptScale, 12);
    const s8 finalRaw = static_cast<s8>(script.back());
    const double finalOffset = finalRaw < 0 ? width * finalRaw / 127.0 : width * finalRaw / 128.0;
    auto slide = out.at(vm.tick() + delay).pitchSlide(track.lastNote, key, key + finalOffset, duration);
    u32 elapsed = 0;
    for (const u32 step : script) {
      elapsed += math::pitchScriptDuration(static_cast<u8>(step >> 8)) * scale;
      const s8 raw = static_cast<s8>(step);
      const double offset = raw < 0 ? width * raw / 127.0 : width * raw / 128.0;
      slide.sample(out.at(vm.tick() + delay + elapsed), key + offset);
    }
    slide.preferPitchBend();
  }

  void applyPortamento(double key, PerformanceNoteId previousNote, std::optional<double> previousKey) {
    if (track.portamentoSpeed == 0 || !previousNote.valid() || !previousKey ||
        std::abs(*previousKey - key) < 0.000001) {
      return;
    }
    const double semitonesPerSecond = 250.0 * track.portamentoSpeed / 127.0;
    const double milliseconds = std::abs(*previousKey - key) / semitonesPerSecond * 1000.0;
    const u32 ticks = math::timelineTicksForMilliseconds(milliseconds, program.tempo, track.timebaseShift);
    out.pitchSlide(track.lastNote, *previousKey, key, PitchSlideTiming::fixedRate(ticks, semitonesPerSecond))
        .preferPortamento();
  }

  [[nodiscard]] Effects note(u32 encodedLength, u8 noteIndex, bool noKeyoff, u8 velocity, bool alternatingShortest) {
    const u32 driverLength = effectiveLength(encodedLength, alternatingShortest);
    const u32 wait = normalized(driverLength);
    bool velocityChanged = false;
    if (track.version == Version::V2 && track.velocityEnabled) {
      velocityChanged = track.velocity != velocity;
      track.velocity = velocity;
    }
    if (noteIndex == 0) {
      if (noKeyoff && track.lastNote.valid() && track.lastKey && !track.previousWasRest) {
        track.lastNote = out.note(NotePerformanceEvent{
            .key = *track.lastKey,
            .linearVelocity = 1.0,
            .durationTicks = wait,
            .extendsPrevious = true,
            .restartsLfoPhase = false,
        });
      } else {
        track.lastNote = {};
        track.lastKey.reset();
        track.previousWasRest = true;
      }
      track.currentSourceNote.reset();
      track.previousSlurred = noKeyoff;
      return Effects::wait(wait);
    }

    const u8 sourceNote = static_cast<u8>(track.octave * 12 + noteIndex - 1);
    if (track.percussion) {
      if (const auto drum = program.drum(sourceNote)) {
        track.volume = std::min<u8>(drum->volume & 0x7f, math::maximumVolume(track.version));
        track.pan = std::min<u8>(drum->pan & 0x1f, 30);
        track.envelope = program.instrument(drum->program);
        emitPan(out);
      }
    }
    track.currentSourceNote = sourceNote;
    if (velocityChanged || track.percussion || track.noteVolumeCurve >= 0) {
      emitLevel(out);
    }
    const double key = sourceNote + (track.percussion ? kDrumKeyBias : 0);
    const u32 duration = soundingTicks(driverLength, noKeyoff);
    const PerformanceNoteId previousNote = track.lastNote;
    const std::optional<double> previousKey = track.lastKey;
    const bool tie =
        track.previousSlurred && previousNote.valid() && previousKey && *previousKey == key && !track.previousWasRest;
    NotePerformanceEvent event{
        .key = key,
        .linearVelocity = currentVelocity(),
        .durationTicks = duration,
        .extendsPrevious = tie,
        .restartsLfoPhase = !tie,
        .restartsVibratoLfoPhase = !tie,
        .restartsTremoloLfoPhase = !tie,
    };
    track.lastNote = out.note(std::move(event));
    applyPortamento(key, previousNote, previousKey);
    applyAttackEnvelope(key);
    applyPitchScript(key);
    track.lastKey = key;
    track.previousWasRest = false;
    track.previousSlurred = noKeyoff;
    return Effects::wait(wait);
  }

  void tempo(u8 raw) {
    program.tempo = raw;
    out.tempo(math::tempoMicrosecondsPerQuarter(raw, track.timebaseShift));
  }

  void octave(u8 value) { track.octave = std::min<u8>(value, 5); }
  void octaveUp() { track.octave = std::min<s32>(track.octave + 1, 5); }
  void octaveDown() { track.octave = std::max<s32>(track.octave - 1, 0); }

  void programChange(u8 value) {
    track.sourceProgram = value;
    track.envelope = program.instrument(value);
    if (!track.percussion) {
      out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = value});
      out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::FutureAttacks);
    }
  }

  [[nodiscard]] u8 effectiveVolume() const {
    if (track.version != Version::V2 || track.noteVolumeCurve < 0 || !track.currentSourceNote) {
      return track.volume;
    }
    const std::span<const u32> curve = program.volumeCurve(static_cast<u8>(track.noteVolumeCurve));
    if (*track.currentSourceNote >= curve.size()) {
      return track.volume;
    }
    const u8 adjusted = static_cast<u8>((track.volume & 0x7f) + static_cast<u8>(curve[*track.currentSourceNote]));
    return math::clippedVolume(track.version, adjusted);
  }

  void emitLevel(PerformanceEmitter output) const {
    output.level(math::levelGain(track.version, effectiveVolume(), track.velocity),
                 ValueQuantization{.levels = static_cast<u16>(math::maximumVolume(track.version) + 1)});
  }

  void setVolume(u8 value, bool stopSlide) {
    track.volume = math::clippedVolume(track.version, value);
    if (stopSlide) {
      track.volumeSlideMagnitude = 0;
    }
    emitLevel(out);
  }

  void volume(u8 value) { setVolume(value, true); }

  void volumeRelative(s8 delta) {
    if (track.version == Version::V2) {
      setVolume(static_cast<u8>((track.volume & 0x7f) + static_cast<u8>(delta)), false);
    } else {
      setVolume(static_cast<u8>(std::clamp<s32>(track.volume + delta, 0, math::maximumVolume(track.version))), false);
    }
  }

  void volumeFromTable(u8 index) { volume(kV1VolumeTable[std::min<size_t>(index, kV1VolumeTable.size() - 1)]); }

  void pan(u8 value) {
    track.pan = std::min<u8>(value & 0x1f, 30);
    emitPan(out);
  }

  void emitPan(PerformanceEmitter output) const {
    const StereoBalance gains = math::panGains(track.pan);
    output.stereoBalance((track.reversePhase & 2) != 0 ? -gains.leftGain : gains.leftGain,
                         (track.reversePhase & 1) != 0 ? -gains.rightGain : gains.rightGain);
  }

  void reversePhase(u8 channels) {
    track.reversePhase = channels & 3;
    emitPan(out);
  }

  void tuning(s8 value) {
    track.fineTuning = value;
    emitTuning();
  }

  void transpose(s8 value) {
    track.transpose = value;
    emitTuning();
  }

  void transposeRelative(s8 value) {
    track.transpose = static_cast<s16>(track.transpose + value);
    emitTuning();
  }

  void emitTuning() { out.tuning(track.transpose * 100.0 + track.fineTuning * (100.0 / 256.0)); }

  [[nodiscard]] LfoPerformanceContext lfoContext(u8 delay = 0) const {
    LfoPerformanceContext context{
        .delayTicks = normalized(delay),
        .delayMilliseconds = delay * math::driverTickMilliseconds(program.tempo, track.timebaseShift),
        .delayIsTempoRelative = true,
        .waveform = LfoWaveform::Triangle,
        .sampleImmediatelyOnNote = true,
    };
    return context;
  }

  [[nodiscard]] double vibratoFrequency() const {
    if (track.vibratoRate == 0) {
      return 0.0;
    }
    if (track.version != Version::V2) {
      return 250.0 / ((track.vibratoType == 0 ? 4.0 : 2.0) * track.vibratoRate);
    }
    if ((track.vibratoMode & 0x80) != 0) {
      const u32 meta = program.waveform(track.vibratoMode & 0x7f);
      const u32 length = std::max<u32>(meta & 0xff, 1);
      return 250.0 * track.vibratoRate / (128.0 * length);
    }
    const u32 period = std::max<u32>(128u - track.vibratoRate, 1);
    return 250.0 / (((track.vibratoMode & 3) == 0 ? 4.0 : 2.0) * period);
  }

  [[nodiscard]] LfoPerformanceContext configuredLfoContext(u8 delay) const {
    auto context = lfoContext(delay);
    context.frequencyHz = vibratoFrequency();
    if (track.version != Version::V2) {
      if (track.vibratoType != 0) {
        context.polarity = LfoPolarity::Positive;
      }
      return context;
    }
    if ((track.vibratoMode & 0x80) != 0) {
      const u32 meta = program.waveform(track.vibratoMode & 0x7f);
      context.waveform = static_cast<LfoWaveform>((meta >> 8) & 0xff);
    } else if ((track.vibratoMode & 3) == 1) {
      context.polarity = LfoPolarity::Positive;
    } else if ((track.vibratoMode & 3) == 2) {
      context.polarity = LfoPolarity::Negative;
    }
    return context;
  }

  void emitVibrato() {
    const auto context = configuredLfoContext(track.vibratoDelay);
    out.vibratoDepth(track.vibratoRate == 0 ? 0.0 : track.vibratoDepth / 256.0, context);
    out.vibratoRate(vibratoFrequency(), context);
    out.vibratoDelayPhysical(normalized(track.vibratoDelay),
                             track.vibratoDelay * math::driverTickMilliseconds(program.tempo, track.timebaseShift));
  }

  void vibrato(u8 rate, u8 depth, u8 mode) {
    track.vibratoRate = rate & 0x7f;
    track.vibratoDepth = depth;
    track.vibratoMode = mode;
    track.tremolo = false;
    emitVibrato();
  }

  void vibratoDelay(u8 delay) {
    track.vibratoDelay = delay;
    if (delay == 0) {
      out.vibratoDepth(0.0, lfoContext());
    } else if (track.tremolo) {
      emitTremolo();
    } else {
      emitVibrato();
    }
  }

  void vibratoType(u8 type) {
    track.vibratoType = type;
    emitVibrato();
  }

  void emitTremolo() {
    auto context = configuredLfoContext(track.vibratoDelay);
    context.polarity = LfoPolarity::Negative;
    context.tremoloGainMode = TremoloGainMode::NoBoost;
    const double pitchUnits = track.vibratoDepth * 127.0 / 256.0;
    const double depth = std::min(1.0, (pitchUnits * 2.0 + 1.0) / 256.0);
    out.vibratoDepth(0.0, context);
    out.tremoloLinearGainDepth(depth, context);
    out.tremoloRate(vibratoFrequency(), context);
    out.tremoloDelayPhysical(normalized(track.vibratoDelay),
                             track.vibratoDelay * math::driverTickMilliseconds(program.tempo, track.timebaseShift));
  }

  void tremolo(u8 delay) {
    track.vibratoDelay = delay;
    track.tremolo = delay != 0;
    if (track.tremolo) {
      emitTremolo();
    } else {
      out.tremoloLinearGainDepth(0.0, lfoContext());
      out.vibratoDepth(0.0, lfoContext());
    }
  }

  void volumeSlide(s8 amount) {
    track.volumeSlideMagnitude = static_cast<u8>(std::abs(static_cast<int>(amount))) & 0x7f;
    track.volumeSlideDown = amount < 0;
    track.volumeSlideAccumulator = 0;
    const u32 driverInterval = track.timebaseShift == 0 ? 24 : (track.timebaseShift == 1 ? 12 : 6);
    track.volumeSlideInterval = normalized(driverInterval);
    track.volumeSlideCounter = track.volumeSlideInterval;
  }

  void pitchAttack(u8 speed, u8 depth, u8 direction) {
    track.attackSpeed = speed & 0x7f;
    track.attackDepth = depth;
    track.attackDirection = direction;
    track.attackEnvelope = track.attackSpeed != 0 && depth != 0;
  }

  void pitchAttackOff() {
    track.attackEnvelope = false;
    out.pitchBend(0.0);
  }

  void pitchScript(u8 index, u8 delay) {
    track.pitchScript = index;
    track.pitchScriptDelay = delay;
  }

  void pitchScriptScale(u8 scale) { track.pitchScriptScale = scale; }

  void volumeCurve(u8 index) { track.noteVolumeCurve = static_cast<s8>(index); }

  void portamento(u8 speed) {
    track.portamentoSpeed = speed == 0 ? 0 : static_cast<u8>(speed + 1);
    out.portamentoEnable(track.portamentoSpeed != 0);
  }

  void percussion(bool enabled) {
    track.percussion = enabled;
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain),
                                      .key = enabled ? kDrumKitKey : track.sourceProgram});
  }

  void echoOn() {
    program.echo.voiceMask = static_cast<u8>(program.echo.voiceMask.value_or(0) | track.voiceBit);
    out.reverb(program.echo);
  }

  void echoOff() {
    program.echo.voiceMask = static_cast<u8>(program.echo.voiceMask.value_or(0) & ~track.voiceBit);
    out.reverb(program.echo);
  }

  void echoOffAll() {
    program.echo.voiceMask = 0;
    out.reverb(program.echo);
  }

  void echoVolume(s8 left, s8 right) {
    program.echo.leftGain = std::clamp(left / 127.0, -1.0, 1.0);
    program.echo.rightGain = std::clamp(right / 127.0, -1.0, 1.0);
    program.echo.send = std::min(1.0, std::max(std::abs(*program.echo.leftGain), std::abs(*program.echo.rightGain)));
    out.reverb(program.echo);
  }

  void echoParameters(u8 delay, s8 feedback, u8 filter) {
    program.echo.delayMilliseconds = (delay & 0x0f) * 16.0;
    program.echo.feedback = feedback / 128.0;
    program.echo.filterIndex = filter;
    out.reverb(program.echo);
  }

  void beginLoop(u8 count, Address start) {
    if (track.loopDepth >= track.loopStarts.size()) {
      return;
    }
    track.loopStarts[track.loopDepth] = start;
    track.loopPlays[track.loopDepth] = count == 0 ? 256 : count;
    ++track.loopDepth;
  }

  [[nodiscard]] Effects endLoop() {
    if (track.loopDepth == 0) {
      return {};
    }
    const u8 slot = track.loopDepth - 1;
    const Effects effects = vm.countedRepeatUntil(slot, track.loopPlays[slot], track.loopStarts[slot]);
    if (!effects.flowOverride) {
      --track.loopDepth;
    }
    return effects;
  }

  void beginCall() { ++track.callDepth; }

  [[nodiscard]] Effects endOrReturn() {
    if (track.callDepth != 0) {
      --track.callDepth;
      return vm.return_();
    }
    return vm.end();
  }

  void setLoopPoint(Address point) { track.loopPoint = point; }

  [[nodiscard]] Effects jumpLoopPoint() { return vm.declaredLoop(track.loopPoint); }

  [[nodiscard]] Effects jumpLoopPointOnce() {
    if (track.loopPointOnce) {
      return {};
    }
    track.loopPointOnce = true;
    return vm.finiteBranch(track.loopPoint);
  }

  void moveImmediate(u8 reg, u8 value) {
    program.registers[reg] = value;
    program.zero = value == 0;
    program.negative = (value & 0x80) != 0;
  }

  void move(u8 destination, u8 source) { moveImmediate(destination, program.registers[source]); }

  void compareImmediate(u8 reg, u8 value) {
    const u8 lhs = program.registers[reg];
    const u8 result = static_cast<u8>(lhs - value);
    program.zero = result == 0;
    program.negative = (result & 0x80) != 0;
    program.carry = lhs >= value;
  }

  void compare(u8 lhs, u8 rhs) { compareImmediate(lhs, program.registers[rhs]); }

  [[nodiscard]] Effects conditionalBranch(u8 opcode, Address destination) {
    // The published dispatch names describe the branch used to *skip* the
    // goto. Consequently the sequence jump conditions are their complements.
    const bool take = opcode == 0x14   ? program.zero
                      : opcode == 0x15 ? !program.zero
                      : opcode == 0x16 ? !program.carry
                      : opcode == 0x17 ? program.carry
                      : opcode == 0x18 ? !program.negative
                                       : program.negative;
    return take ? vm.finiteBranch(destination) : Effects{};
  }

  void replaceEnvelope() {
    if (track.envelope) {
      out.replaceEnvelope(math::envelope(*track.envelope), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
  }

  void attackRate(u8 value) {
    if (track.envelope) {
      track.envelope->adsr1 = static_cast<u8>((track.envelope->adsr1 & 0xf0) | (value & 0x0f));
      replaceEnvelope();
    }
  }

  void decayRate(u8 value) {
    if (track.envelope) {
      track.envelope->adsr1 = static_cast<u8>(0x80 | (track.envelope->adsr1 & 0x0f) | ((value & 7) << 4));
      replaceEnvelope();
    }
  }

  void sustainLevel(u8 value) {
    if (track.envelope) {
      track.envelope->adsr2 = static_cast<u8>((track.envelope->adsr2 & 0x1f) | ((value & 7) << 5));
      replaceEnvelope();
    }
  }

  void sustainRate(u8 value) {
    if (track.envelope) {
      track.envelope->adsr2 = static_cast<u8>((track.envelope->adsr2 & 0xe0) | (value & 0x1f));
      replaceEnvelope();
    }
  }

  void releaseRate(u8 value) {
    if (track.envelope) {
      track.envelope->gain = static_cast<u8>(0xa0 | (value & 0x1f));
      replaceEnvelope();
    }
  }

  void tick() {
    if (track.volumeSlideMagnitude == 0 || track.volumeSlideInterval == 0) {
      return;
    }
    if (--track.volumeSlideCounter != 0) {
      return;
    }
    track.volumeSlideCounter = track.volumeSlideInterval;
    track.volumeSlideAccumulator += track.volumeSlideMagnitude;
    const u8 delta = static_cast<u8>(track.volumeSlideAccumulator >> 3);
    track.volumeSlideAccumulator &= 7;
    if (delta == 0) {
      return;
    }
    const u8 limit = math::maximumVolume(track.version);
    const s32 next = track.volumeSlideDown ? track.volume - delta : track.volume + delta;
    track.volume = static_cast<u8>(std::clamp<s32>(next, 0, limit));
    emitLevel(out);
    if (track.volume == 0 || track.volume == limit) {
      track.volumeSlideMagnitude = 0;
    }
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeSubcommand(Cursor& cursor, Version version) {
  auto event = cursor.command("Subcommand", SequenceSemantic::State);
  const u8 subcommand = event.u8("subcommand", SourceValueDisplay::Hex);
  switch (subcommand) {
    case 0x00:
      event.label("End");
      return event.end();
    case 0x01:
      event.label("Echo Off");
      return event.invoke<&Playback::echoOff>();
    case 0x02:
      event.label("Echo Off (All Channels)");
      return event.invoke<&Playback::echoOffAll>();
    case 0x03:
    case 0x04:
      event.label(subcommand == 3 ? "Percussion On" : "Percussion Off");
      return event.invoke<&Playback::percussion>(subcommand == 3);
    case 0x05:
    case 0x06:
    case 0x07:
      if (version == Version::V1) {
        event.label("Vibrato Shape");
        return event.invoke<&Playback::vibratoType>(static_cast<u8>(subcommand - 5));
      }
      return event.ignore();
    case 0x08:
    case 0x09:
      return event.ignore();
    default:
      break;
  }

  if (version != Version::V2) {
    event.label("Unsupported Subcommand");
    return event.stop();
  }
  switch (subcommand) {
    case 0x0a:
    case 0x0b:
    case 0x0e:
    case 0x0f:
      return event.ignore();
    case 0x0c:
      event.label("Note Velocity Off");
      return event.set<&TrackState::velocityEnabled>(false);
    case 0x0d:
      event.label("Select Note Volume Curve");
      return event.invoke<&Playback::volumeCurve>(event.u8("curve"));
    case 0x10: {
      event.label("Move Immediate");
      const u8 reg = event.u8("register");
      return event.invoke<&Playback::moveImmediate>(reg, event.u8("value"));
    }
    case 0x11: {
      event.label("Move Register");
      const u8 destination = event.u8("destination");
      return event.invoke<&Playback::move>(destination, event.u8("source"));
    }
    case 0x12: {
      event.label("Compare Immediate");
      const u8 reg = event.u8("register");
      return event.invoke<&Playback::compareImmediate>(reg, event.u8("value"));
    }
    case 0x13: {
      event.label("Compare Registers");
      const u8 lhs = event.u8("left");
      return event.invoke<&Playback::compare>(lhs, event.u8("right"));
    }
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19: {
      event.label("Conditional Branch");
      const Address destination = event.addressLe("destination", SemanticOperandRole::JumpTarget);
      return event.invoke<&Playback::conditionalBranch>(subcommand, destination).mayBranchTo(destination);
    }
    case 0x1a:
      event.label("ADSR Attack Rate");
      return event.invoke<&Playback::attackRate>(event.u8("rate"));
    case 0x1b:
      event.label("ADSR Decay Rate");
      return event.invoke<&Playback::decayRate>(event.u8("rate"));
    case 0x1c:
      event.label("ADSR Sustain Level");
      return event.invoke<&Playback::sustainLevel>(event.u8("level"));
    case 0x1d:
      event.label("ADSR Sustain Rate");
      return event.invoke<&Playback::sustainRate>(event.u8("rate"));
    case 0x1e:
      event.label("GAIN Pseudo-Release Rate");
      return event.invoke<&Playback::releaseRate>(event.u8("rate"));
    default:
      event.label("Unsupported Subcommand");
      return event.stop();
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, Version version, u8 timebaseShift,
                                                   bool noteVelocity, u32 noteTable,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, "hudson-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode < 0xd0) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 lengthIndex = event.opcodeValue("duration_index", opcode & 7);
    const bool noKeyoff = event.opcodeValue("no_keyoff", (opcode & 8) != 0);
    const u8 note = event.opcodeValue("note", opcode >> 4, SourceValueDisplay::Default, SemanticOperandRole::NoteKey);
    u32 driverLength = 0;
    if (lengthIndex == 0) {
      driverLength = event.u8("duration", SemanticOperandRole::Duration);
      if (driverLength == 0) {
        driverLength = 256;
      }
    } else {
      const u32 tableIndex = lengthIndex + timebaseShift - 1;
      driverLength = tableIndex < kDurations.size() && reader.has(noteTable + tableIndex, 1)
                         ? reader.u8At(noteTable + tableIndex)
                         : kDurations[std::min<size_t>(lengthIndex - 1, kDurations.size() - 1)];
      event.derived("driver_duration", driverLength, SemanticOperandRole::Duration);
    }
    const u8 velocity = version == Version::V2 && noteVelocity ? event.u8("velocity", SemanticOperandRole::Value) : 127;
    if (note == 0) {
      event.label(noKeyoff ? "Hold" : "Rest");
    }
    return event.invoke<&Playback::note>(driverLength, note, noKeyoff, velocity, lengthIndex != 0 && driverLength == 1);
  }

  switch (opcode) {
    case 0xd0:
      return cursor.noOp("No Operation", "nop");
    case 0xd1: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u8 raw = event.u8("tempo");
      event.derived("microseconds_per_quarter", math::tempoMicrosecondsPerQuarter(raw, timebaseShift),
                    SemanticOperandRole::Value);
      return event.invoke<&Playback::tempo>(raw);
    }
    case 0xd2: {
      auto event = cursor.command("Octave", SequenceSemantic::Pitch);
      return event.invoke<&Playback::octave>(event.u8("octave"));
    }
    case 0xd3:
      return cursor.command("Octave Up", SequenceSemantic::Pitch).invoke<&Playback::octaveUp>();
    case 0xd4:
      return cursor.command("Octave Down", SequenceSemantic::Pitch).invoke<&Playback::octaveDown>();
    case 0xd5: {
      auto event = cursor.command("Quantize", SequenceSemantic::State);
      return event.set<&TrackState::quantize>(event.u8("quantize", SemanticOperandRole::Duration));
    }
    case 0xd6: {
      auto event = cursor.command("Instrument", SequenceSemantic::Instrument);
      return event.invoke<&Playback::programChange>(event.u8("program", SemanticOperandRole::InstrumentProgram));
    }
    case 0xd7:
    case 0xd8:
      if (version == Version::V2) {
        return cursor.noOp("No Operation", "nop");
      } else {
        auto event = cursor.sourceOnly("Ignored Parameter", "nop");
        event.u8("unused");
        return event.ignore();
      }
    case 0xd9: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xda: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0xdb: {
      auto event = cursor.command("Reverse Phase", SequenceSemantic::Pan);
      return event.invoke<&Playback::reversePhase>(event.u8("channels", SourceValueDisplay::Hex));
    }
    case 0xdc: {
      auto event = cursor.command("Relative Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volumeRelative>(event.s8("delta", SemanticOperandRole::Level));
    }
    case 0xdd: {
      auto event = cursor.command("Loop Start", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      event.derived("total_plays", count == 0 ? 256u : count, SemanticOperandRole::Count);
      return event.invoke<&Playback::beginLoop>(count, Address{begin + 2});
    }
    case 0xde:
      return cursor.command("Loop End", SequenceSemantic::Repeat).invoke<&Playback::endLoop>().runtimeControlFlow();
    case 0xdf: {
      auto event = cursor.command("Pattern Call", SequenceSemantic::Call);
      const Address destination = event.addressLe("destination", SemanticOperandRole::CallTarget);
      return event.invoke<&Playback::beginCall>().call(destination);
    }
    case 0xe0: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const Address destination = event.addressLe("destination", SemanticOperandRole::LoopTarget);
      return destination.value < begin ? event.loopCandidate(destination) : event.jump(destination);
    }
    case 0xe1: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      return event.invoke<&Playback::tuning>(event.s8("tuning"));
    }
    case 0xe2: {
      auto event = cursor.command("Vibrato", SequenceSemantic::Modulation);
      const u8 rate = event.u8("rate");
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 mode = version == Version::V2 ? event.u8("mode", SemanticOperandRole::Modulation) : 0;
      return event.invoke<&Playback::vibrato>(rate, depth, mode);
    }
    case 0xe3: {
      auto event = cursor.command("Vibrato Delay", SequenceSemantic::Modulation);
      return event.invoke<&Playback::vibratoDelay>(event.u8("delay", SemanticOperandRole::Duration));
    }
    case 0xe4: {
      auto event = cursor.command("Echo Volume", SequenceSemantic::State);
      const s8 left = event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      return event.invoke<&Playback::echoVolume>(
          left, event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level));
    }
    case 0xe5: {
      auto event = cursor.command("Echo Parameters", SequenceSemantic::State);
      const u8 delay = event.u8("delay");
      const s8 feedback = event.s8("feedback");
      return event.invoke<&Playback::echoParameters>(delay, feedback, event.u8("fir_filter"));
    }
    case 0xe6:
      return cursor.command("Echo On", SequenceSemantic::State).invoke<&Playback::echoOn>();
    case 0xe7: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transpose>(event.s8("semitones"));
    }
    case 0xe8: {
      auto event = cursor.command("Relative Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transposeRelative>(event.s8("semitones"));
    }
    case 0xe9: {
      auto event = cursor.command("Pitch Attack Envelope", SequenceSemantic::Pitch);
      const u8 speed = event.u8("speed");
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::pitchAttack>(speed, depth, event.u8("direction"));
    }
    case 0xea:
      return cursor.command("Pitch Attack Envelope Off", SequenceSemantic::Pitch).invoke<&Playback::pitchAttackOff>();
    case 0xeb:
      return cursor.command("Set Loop Point", SequenceSemantic::Repeat)
          .invoke<&Playback::setLoopPoint>(Address{begin + 1});
    case 0xec:
      return cursor.command("Jump to Loop Point", SequenceSemantic::Repeat)
          .invoke<&Playback::jumpLoopPoint>()
          .requireRuntimeControlFlow();
    case 0xed:
      return cursor.command("Jump to Loop Point Once", SequenceSemantic::Repeat)
          .invoke<&Playback::jumpLoopPointOnce>()
          .runtimeControlFlow();
    case 0xee:
      if (version == Version::V2) {
        return cursor.noOp("No Operation", "nop");
      } else {
        auto event = cursor.command("Indexed Volume", SequenceSemantic::Level);
        return event.invoke<&Playback::volumeFromTable>(event.u8("index", SemanticOperandRole::Level));
      }
    case 0xef:
      if (version == Version::Early) {
        return cursor.noOp("No Operation", "nop");
      } else {
        auto event = cursor.command("Pitch Script", SequenceSemantic::Pitch);
        const u8 script = event.u8("script");
        return event.invoke<&Playback::pitchScript>(script, event.u8("delay", SemanticOperandRole::Duration));
      }
    case 0xf0:
      if (version == Version::Early) {
        return cursor.noOp("No Operation", "nop");
      } else {
        auto event = cursor.command("Pitch Script Range", SequenceSemantic::Pitch);
        return event.invoke<&Playback::pitchScriptScale>(event.u8("semitones"));
      }
    case 0xf1:
      if (version == Version::Early) {
        return cursor.noOp("No Operation", "nop");
      } else {
        auto event = cursor.command("Portamento", SequenceSemantic::Portamento);
        const u8 speed = event.u8("speed");
        event.u8("unused");
        return event.invoke<&Playback::portamento>(speed);
      }
    case 0xf2:
      if (version == Version::V2) {
        auto event = cursor.command("Tremolo", SequenceSemantic::Modulation);
        return event.invoke<&Playback::tremolo>(event.u8("delay", SemanticOperandRole::Duration));
      }
      return cursor.noOp("No Operation", "nop");
    case 0xf3:
      if (version == Version::V2) {
        auto event = cursor.command("Periodic Volume Slide", SequenceSemantic::Level);
        return event.invoke<&Playback::volumeSlide>(event.s8("amount", SemanticOperandRole::Level));
      }
      return cursor.noOp("No Operation", "nop");
    case 0xf4:
    case 0xf5:
    case 0xf6:
    case 0xf7:
    case 0xf8:
    case 0xf9:
    case 0xfa:
    case 0xfb:
    case 0xfc:
    case 0xfd:
      return cursor.noOp("No Operation", "nop");
    case 0xfe:
      return decodeSubcommand(cursor, version);
    case 0xff: {
      auto event = cursor.command("End / Return", SequenceSemantic::End);
      event.invoke<&Playback::endOrReturn>();
      return event.discoverReturn();
    }
    default:
      return cursor.unsupported("Unsupported HudsonSnes Command").stop();
  }
}

[[nodiscard]] LfoWaveform classifyWaveform(const CustomWaveform& waveform) {
  if (waveform.samples.size() < 3) {
    return LfoWaveform::Square;
  }
  std::vector<s8> values = waveform.samples;
  std::ranges::sort(values);
  values.erase(std::ranges::unique(values).begin(), values.end());
  if (values.size() <= 2) {
    return LfoWaveform::Square;
  }
  const bool ascending = std::ranges::is_sorted(waveform.samples);
  const bool descending = std::ranges::is_sorted(waveform.samples, std::greater{});
  if (ascending) {
    return LfoWaveform::SawtoothUp;
  }
  if (descending) {
    return LfoWaveform::SawtoothDown;
  }
  return LfoWaveform::Sine;
}

[[nodiscard]] std::vector<u32> driverData(const ParsedHeader& header) {
  std::vector<u32> result(kPayloadBase, 0);
  result[kEchoBase] = (static_cast<u32>(static_cast<u8>(header.initialEchoLeft)) << 24) |
                      (static_cast<u32>(static_cast<u8>(header.initialEchoRight)) << 16) |
                      (static_cast<u32>(header.initialEchoDelay) << 8) | static_cast<u8>(header.initialEchoFeedback);
  result[kEchoBase + 1] = header.initialEchoFilter;
  std::fill_n(result.begin(), kInstrumentWords, kMissingInstrument);
  std::fill_n(result.begin() + kDrumBase, 128, kMissingInstrument);
  for (const InstrumentRow& instrument : header.recipes.instruments) {
    result[instrument.program] =
        (static_cast<u32>(instrument.adsr1) << 16) | (static_cast<u32>(instrument.adsr2) << 8) | instrument.gain;
  }
  for (const CustomWaveform& waveform : header.recipes.customWaveforms) {
    const u32 count = std::min<size_t>(waveform.samples.size(), 255);
    const u32 offset = result.size();
    result[kWaveformBase + waveform.index] =
        (offset << 16) | (static_cast<u32>(classifyWaveform(waveform)) << 8) | count;
    for (u32 index = 0; index < count; ++index) {
      result.push_back(static_cast<u8>(waveform.samples[index]));
    }
  }
  for (const DrumSlot& drum : header.recipes.drums) {
    result[kDrumBase + drum.note] = (static_cast<u32>(drum.sourceProgram) << 24) |
                                    (static_cast<u32>(drum.sourceKey) << 16) | (static_cast<u32>(drum.volume) << 8) |
                                    drum.pan;
  }
  for (const PitchScript& script : header.recipes.pitchScripts) {
    const u32 count = std::min<size_t>(script.steps.size(), 255);
    const u32 offset = result.size();
    result[kPitchDescriptorBase + script.index] = (offset << 8) | count;
    for (u32 index = 0; index < count; ++index) {
      result.push_back((static_cast<u32>(script.steps[index].duration) << 8) |
                       static_cast<u8>(script.steps[index].target));
    }
  }
  for (const VolumeCurve& curve : header.recipes.volumeCurves) {
    const u32 count = std::min<size_t>(curve.offsets.size(), 255);
    const u32 offset = result.size();
    result[kVolumeDescriptorBase + curve.index] = (offset << 8) | count;
    for (u32 index = 0; index < count; ++index) {
      result.push_back(static_cast<u8>(curve.offsets[index]));
    }
  }
  return result;
}

}  // namespace

const SequenceDialect& sequenceDialect() {
  static const SequenceDialect dialect = makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = "hudson-snes"},
      .commandDetailKindPrefix = "hudson-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .initialLevel = math::levelGain(Version::Early, math::initialVolume(Version::Early)),
              .initialReverbSend = 0.0,
              .initialStereoBalance = math::panGains(15),
              .initialMonoModeChannels = 0,
          },
  });
  return dialect;
}

TrackProgram decodeSourceTrack(ByteReader reader, Version version, u8 timebaseShift, bool noteVelocity, u32 trackNumber,
                               u32 startAddress, std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope tracks{.reader = reader, .maxCommands = 32768};
  return tracks.linear(trackNumber, startAddress, [&](u32 offset) {
    return decodeCommand(reader, offset, version, timebaseShift, noteVelocity, kAramSize, diagnostics);
  });
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  auto header = parseHeader(reader, layout.version, layout.sequenceHeaderAddress);
  if (!header) {
    SequenceProgram empty;
    empty.dialect = sequenceDialect().id;
    empty.timebase = sequenceDialect().timebase;
    return SequenceParse{.program = std::move(empty)};
  }
  SequenceDecodeSession sequence{reader, sequenceDialect(), sequenceId, header->range, sourceMap, 32768};
  for (const auto& [track, start] : header->tracks) {
    sequence.addLinearTrack(track, header->range, start, [&](u32 offset) {
      return decodeCommand(reader, offset, layout.version, header->timebaseShift, header->noteVelocity,
                           layout.noteLengthTableAddress, diagnostics);
    });
  }
  SequenceProgram program = sequence.finish();
  supplementLiveRecipes(reader, layout, program, header->recipes);
  program.config.profile = static_cast<u32>(layout.version);
  program.config.driverState =
      header->timebaseShift | (header->noteVelocity ? 0x100 : 0) | (static_cast<u32>(header->initialEchoMask) << 16);
  program.config.driverData = driverData(*header);
  program.behavior.initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(120, header->timebaseShift);
  program.behavior.initialLevel = math::levelGain(layout.version, math::initialVolume(layout.version));
  program.behavior.initialStereoBalance = math::panGains(15);
  return SequenceParse{
      .program = std::move(program),
      .recipes = std::move(header->recipes),
      .headerRange = header->range,
  };
}

}  // namespace vgmtrans::formats::hudson_snes
