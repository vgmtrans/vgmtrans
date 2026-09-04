/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NeverlandSnes/NeverlandSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::neverland_snes {

using namespace core;

namespace {

constexpr std::array<u8, 128> kSineMagnitude{
    0x00, 0x03, 0x06, 0x09, 0x0d, 0x10, 0x13, 0x16, 0x19, 0x1c, 0x1f, 0x22, 0x25, 0x28, 0x2b, 0x2e,
    0x31, 0x34, 0x37, 0x3a, 0x3c, 0x3f, 0x42, 0x44, 0x47, 0x4a, 0x4c, 0x4f, 0x51, 0x53, 0x56, 0x58,
    0x5a, 0x5d, 0x5f, 0x61, 0x63, 0x65, 0x67, 0x68, 0x6a, 0x6c, 0x6d, 0x6f, 0x71, 0x72, 0x73, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7c, 0x7d, 0x7d, 0x7e, 0x7e, 0x7f, 0x7f, 0x7f, 0x7f,
    0x7f, 0x7f, 0x7f, 0x7f, 0x7e, 0x7e, 0x7d, 0x7d, 0x7c, 0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x77, 0x76,
    0x75, 0x73, 0x72, 0x71, 0x6f, 0x6d, 0x6c, 0x6a, 0x68, 0x67, 0x65, 0x63, 0x61, 0x5f, 0x5d, 0x5a,
    0x58, 0x56, 0x53, 0x51, 0x4f, 0x4c, 0x4a, 0x47, 0x44, 0x42, 0x3f, 0x3c, 0x3a, 0x37, 0x34, 0x31,
    0x2e, 0x2b, 0x28, 0x25, 0x22, 0x1f, 0x1c, 0x19, 0x16, 0x13, 0x10, 0x0d, 0x09, 0x06, 0x03, 0x00,
};
constexpr std::array<u16, 12> kPitchTable{
    kPitchTableC8, 0x237b, 0x2597, 0x27d3, 0x2a31, 0x2cb3, 0x2f5c, 0x322d, 0x3529, 0x3852, 0x3bab, 0x3f38,
};

namespace math {

[[nodiscard]] u32 timerTarget(u8 raw) { return raw == 0 ? 256 : raw; }

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u8 raw) {
  // Timer 0 runs at 8 kHz and one timer overflow is one sequence tick.
  return timerTarget(raw) * 6000u;
}

[[nodiscard]] double gain(u8 raw) { return std::min(raw / 127.0, 1.0); }

[[nodiscard]] StereoBalance balance(u8 pan, bool invertLeft = false, bool invertRight = false) {
  const double right = std::min(pan / 128.0, 1.0);
  const double left = 1.0 - right;
  return {
      .leftGain = invertLeft ? -left : left,
      .rightGain = invertRight ? -right : right,
  };
}

[[nodiscard]] double signedDspGain(s8 raw) { return raw / 128.0; }

[[nodiscard]] double echoGain(Version version, u8 raw, u8 master) {
  if (version == Version::Original) {
    return signedDspGain(static_cast<s8>(raw));
  }
  const u8 dspValue = static_cast<u8>((static_cast<u16>(raw) * master) / 128u);
  return signedDspGain(static_cast<s8>(dspValue));
}

[[nodiscard]] double pitchScale(u8 raw) {
  if (raw == 0) {
    return 2.0;
  }
  return raw / 128.0;
}

[[nodiscard]] double tuningCents(u8 raw) { return 1200.0 * std::log2(pitchScale(raw)); }

[[nodiscard]] u8 normalizedNote(u8 raw) { return raw < 12 ? static_cast<u8>(raw + 96) : raw; }

[[nodiscard]] double melodicKey(u8 raw) { return normalizedNote(raw) + 24.0; }

[[nodiscard]] double programUnityKey(ByteReader reader, u16 table, u8 program) {
  const u32 address = table + program * 4u;
  const u16 tuning = reader.has(address, 4) ? reader.be16(address + 2) : u16{0x0100};
  return instrumentUnityKey(tuning);
}

[[nodiscard]] double percussionKey(ByteReader reader, u16 table, const PercussionPatch& patch) {
  return programUnityKey(reader, table, patch.program) +
         12.0 * std::log2(std::max<u16>(patch.pitch, 1) / 4096.0);
}

[[nodiscard]] double driverPitch(ByteReader reader, u16 table, u8 program, u8 note, u8 scale) {
  const u8 normalized = normalizedNote(note);
  const u8 octave = normalized / 12;
  double pitch = kPitchTable[normalized % 12];
  if (octave < 8) {
    pitch /= static_cast<double>(1u << (8 - octave));
  }
  const u32 address = table + program * 4u;
  const u16 tuning = reader.has(address, 4) ? reader.be16(address + 2) : u16{0x0100};
  return pitch * instrumentPitchScale(tuning) * pitchScale(scale);
}

[[nodiscard]] u8 lfoMagnitude(u8 phase, u8 depth) {
  return static_cast<u8>(kSineMagnitude[phase & 0x7f] * depth / 256u);
}

[[nodiscard]] double vibratoRatio(u8 phase, u8 depth, u8 strength) {
  const u8 magnitude = static_cast<u8>((2u * lfoMagnitude(phase, depth) * strength) / 256u);
  const double amount = magnitude / 256.0;
  return (phase & 0x80) != 0 ? 1.0 - amount : 1.0 + amount;
}

[[nodiscard]] ModulationRange vibratoRange(u8 depth, u8 strength) {
  return {
      .minimum = 12.0 * std::log2(vibratoRatio(0xc0, depth, strength)),
      .maximum = 12.0 * std::log2(vibratoRatio(0x40, depth, strength)),
  };
}

[[nodiscard]] std::vector<double> vibratoSamples(u8 depth, u8 strength, double normalization) {
  std::vector<double> samples;
  samples.reserve(256);
  for (u32 phase = 0; phase < 256; ++phase) {
    const double semitones =
        12.0 * std::log2(vibratoRatio(static_cast<u8>(phase), depth, strength));
    samples.push_back(normalization == 0.0 ? 0.0 : semitones / normalization);
  }
  return samples;
}

[[nodiscard]] std::vector<double> tremoloSamples(u8 depth) {
  std::vector<double> samples;
  samples.reserve(256);
  const u8 peak = lfoMagnitude(0x40, depth);
  for (u32 phase = 0; phase < 256; ++phase) {
    const double normalized = peak == 0 ? 0.0 : lfoMagnitude(static_cast<u8>(phase), depth) / static_cast<double>(peak);
    samples.push_back((phase & 0x80) != 0 ? -normalized : normalized);
  }
  return samples;
}

[[nodiscard]] double tremoloDepth(u8 depth, u8 strength) {
  // The mixer multiplies twice at eight-bit precision: first by 2*strength,
  // then by 2*the depth-scaled sine sample.
  return 4.0 * strength * lfoMagnitude(0x40, depth) / 65536.0;
}

}  // namespace math

struct RepeatFrame {
  Address start;
  u16 playlistAddress = 0;
  u8 transpose = 0;
};

struct RuntimeConfig {
  ByteReader reader;
  Layout layout;
};

struct EchoState {
  double left = 0.0;
  double right = 0.0;
  s8 feedback = 0;
  u8 delay = 0;
  u8 filter = 0;
  u8 voiceMask = 0;
  bool reverseLeft = false;
  bool reverseRight = false;
};

struct ProgramState {
  ProgramState(const SequenceProgram&, const RuntimeConfig& config) : tempoTarget(config.layout.initialTempo) {
    echo.delay = config.layout.initialEchoDelay;
    echo.left = echo.right = math::echoGain(config.layout.version, config.layout.initialEchoVolume,
                                            config.layout.initialMasterVolume);
    echo.feedback = static_cast<s8>(config.layout.initialEchoFeedback);
    echo.filter = config.layout.initialEchoFilter;
  }

  u8 tempoTarget;
  EchoState echo;
};

struct TrackState {
  TrackState(const TrackProgram& source, const RuntimeConfig& config)
      : reader(config.reader),
        layout(config.layout),
        percussion(config.layout.tracks[std::min<u32>(source.sourceTrackNumber, kTrackCount - 1)].percussion),
        voiceBit(static_cast<u8>(1u << std::min<u32>(source.sourceTrackNumber, 7))) {}

  ByteReader reader;
  Layout layout;
  bool percussion = false;
  u8 voiceBit = 1;
  u16 playlistAddress = 0;
  u8 transpose = 0;
  u8 pan = 0x40;
  bool invertLeft = false;
  bool invertRight = false;
  u8 program = 0;
  u8 pitchScale = 0x80;

  u8 savedWait = 0;
  u8 savedDuration = 0;
  u8 savedVelocity = 0;
  PerformanceNoteId lastNote;
  double lastKey = 0.0;
  u64 gateEnd = 0;
  double baseDspPitch = 0.0;

  u8 modulationStrength = 0;
  u8 vibratoDepth = 0x20;
  u8 vibratoRate = 0x20;
  u8 tremoloDepth = 0x10;
  u8 tremoloRate = 0x20;

  std::array<RepeatFrame, 2> repeats;
  u8 repeatDepth = 0;

  s8 driftRate = 0;
  s16 drift = 0;
  u32 driftClock = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] const PercussionPatch* percussionPatch(u8 key) const {
    const auto found = std::ranges::find(track.layout.percussion, key, &PercussionPatch::key);
    return found == track.layout.percussion.end() ? nullptr : &*found;
  }

  void emitBalance() const {
    const StereoBalance gains = math::balance(track.pan, track.invertLeft, track.invertRight);
    out.stereoBalance(gains.leftGain, gains.rightGain);
  }

  void emitEcho() const {
    const double left = program.echo.reverseLeft ? -program.echo.left : program.echo.left;
    const double right = program.echo.reverseRight ? -program.echo.right : program.echo.right;
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = program.echo.voiceMask,
        .send = program.echo.voiceMask == 0 ? 0.0 : std::clamp(std::max(std::abs(left), std::abs(right)), 0.0, 1.0),
        .leftGain = left,
        .rightGain = right,
        .delayMilliseconds = (program.echo.delay & 0x0f) * 16.0,
        .feedback = math::signedDspGain(program.echo.feedback),
        .filterIndex = program.echo.filter,
    });
  }

  [[nodiscard]] LfoPerformanceContext lfoContext(std::vector<double> samples, bool restart = false) const {
    return LfoPerformanceContext{
        .shape = LfoShape{.waveform = LfoWaveform::Sine, .samples = std::move(samples)},
        .polarity = LfoPolarity::Bipolar,
        .initialPhaseCycles = 0.0,
        .noteRestartInitialPhaseCycles = 0.0,
        .sampleImmediatelyOnNote = true,
        .restartMode = restart ? LfoRestartMode::Phase : LfoRestartMode::None,
        .phaseRunsAtZeroDepth = false,
        .tremoloGainMode = TremoloGainMode::BipolarAroundNominal,
    };
  }

  void emitVibrato(bool restart = false) const {
    const ModulationRange range = math::vibratoRange(track.vibratoDepth, track.modulationStrength);
    const double depth = std::max(std::abs(range.minimum), std::abs(range.maximum));
    auto context = lfoContext(math::vibratoSamples(track.vibratoDepth, track.modulationStrength, depth), restart);
    context.frequencyHz = track.vibratoRate / 8.0;
    context.pitchRangeSemitones = range;
    out.vibratoDepth(depth, context);
    context.restartMode = LfoRestartMode::None;
    out.vibratoRate(*context.frequencyHz, context);
  }

  void emitTremolo(bool restart = false) const {
    auto context = lfoContext(math::tremoloSamples(track.tremoloDepth), restart);
    context.frequencyHz = track.tremoloRate / 8.0;
    out.tremoloLinearGainDepth(math::tremoloDepth(track.tremoloDepth, track.modulationStrength), context);
    context.restartMode = LfoRestartMode::None;
    out.tremoloRate(*context.frequencyHz, context);
  }

  void emitDrift() const {
    if (!track.layout.hasPitchDrift || track.baseDspPitch <= 0.0) {
      return;
    }
    const double shifted = track.baseDspPitch + track.drift;
    if (shifted > 0.0) {
      out.pitchBend(12.0 * std::log2(shifted / track.baseDspPitch));
    }
  }

  void selectProgram(u8 value) {
    track.program = value;
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = value},
                   InstrumentEnvelopeMode::UseInstrumentEnvelope);
    // The driver writes both ADSR registers immediately, including for an
    // already sounding voice.
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void transpose(u8 semitones) { track.transpose = semitones; }

  void enterSection(u16 playlistAddress) { track.playlistAddress = playlistAddress; }

  [[nodiscard]] Effects sectionEnd() {
    track.transpose = 0;
    return vm.jump(Address{static_cast<u16>(track.playlistAddress + 2)});
  }

  [[nodiscard]] Effects note(u8 rawKey, u8 wait, u8 duration, u8 velocity, bool save) {
    if (save) {
      track.savedWait = wait;
      track.savedDuration = duration;
      track.savedVelocity = velocity;
    } else {
      wait = track.savedWait;
      duration = track.savedDuration;
      velocity = track.savedVelocity;
    }

    const u8 effective = static_cast<u8>(rawKey + track.transpose);
    double key = math::melodicKey(effective);
    if (duration == 0 && (!track.percussion || track.layout.version == Version::Original)) {
      if (track.lastNote.valid()) {
        static_cast<void>(out.setNoteEnd(track.lastNote, vm.tick() + wait));
      }
      // A zero gate is a pure wait on melodic voices. It also forces the next
      // same-key note to attack instead of extending the current voice.
      track.gateEnd = vm.tick();
      return Effects::wait(wait);
    }
    if (track.percussion) {
      const PercussionPatch* patch = percussionPatch(effective);
      if (patch == nullptr) {
        // The early driver has already installed the delta-time before its
        // percussion search; the later driver installs it only on a match.
        return track.layout.version == Version::Original ? Effects::wait(wait) : Effects{};
      }
      selectProgram(patch->program);
      track.pan = patch->pan;
      emitBalance();
      key = math::percussionKey(track.reader, track.layout.instrumentTableAddress, *patch);
      track.baseDspPitch = patch->pitch * math::pitchScale(track.pitchScale);
    } else {
      track.baseDspPitch = math::driverPitch(track.reader, track.layout.instrumentTableAddress, track.program,
                                             effective, track.pitchScale);
    }
    emitDrift();

    const bool extend = track.lastNote.valid() && std::abs(track.lastKey - key) < 0.000001 &&
                        vm.tick() < track.gateEnd;
    NotePerformanceEvent event{
        .key = key,
        .linearVelocity = math::gain(velocity),
        .durationTicks = duration,
        .extendsPrevious = extend,
        .restartsEnvelope = !extend,
        .restartsLfoPhase = false,
        .restartsVibratoLfoPhase = false,
        .restartsTremoloLfoPhase = false,
    };
    track.lastNote = out.note(std::move(event));
    track.lastKey = key;
    track.gateEnd = vm.tick() + duration;
    return Effects::wait(wait);
  }

  [[nodiscard]] Effects volume(u8 wait, u8 value) {
    out.level(math::gain(value), ValueQuantization{.levels = 128});
    return Effects::wait(wait);
  }

  [[nodiscard]] Effects pan(u8 wait, u8 value) {
    track.pan = value;
    emitBalance();
    return Effects::wait(wait);
  }

  [[nodiscard]] Effects delay(u8 wait) { return Effects::wait(wait); }

  [[nodiscard]] Effects tempo(u8 wait, u8 value) {
    program.tempoTarget = value;
    out.tempo(math::tempoMicrosecondsPerQuarter(value));
    return Effects::wait(wait);
  }

  [[nodiscard]] Effects tuning(u8 wait, u8 value) {
    track.pitchScale = value;
    out.tuning(math::tuningCents(value));
    emitDrift();
    return Effects::wait(wait);
  }

  [[nodiscard]] Effects programChange(u8 wait, u8 value) {
    selectProgram(value);
    return Effects::wait(wait);
  }

  [[nodiscard]] Effects modulation(u8 wait, u8 strength) {
    const bool restart = track.modulationStrength == 0;
    track.modulationStrength = strength;
    emitVibrato(restart);
    emitTremolo(restart);
    return Effects::wait(wait);
  }

  [[nodiscard]] Effects repeatStart(Address start) {
    if (track.repeatDepth < track.repeats.size()) {
      track.repeats[track.repeatDepth++] = RepeatFrame{
          .start = start,
          .playlistAddress = track.playlistAddress,
          .transpose = track.transpose,
      };
    }
    return {};
  }

  void restoreRepeat(const RepeatFrame& repeat) {
    track.playlistAddress = repeat.playlistAddress;
    track.transpose = repeat.transpose;
  }

  [[nodiscard]] Effects repeatEnd(u8 count) {
    if (track.repeatDepth == 0) {
      return {};
    }
    RepeatFrame& repeat = track.repeats[track.repeatDepth - 1];
    if (count == 0) {
      restoreRepeat(repeat);
      return vm.declaredLoop(repeat.start);
    }
    if (count == 1) {
      --track.repeatDepth;
      return {};
    }
    RepeatCounter counter = vm.repeatCounter(static_cast<u8>(track.repeatDepth - 1));
    if (counter.firstVisit()) {
      counter.start(count);
    }
    if (counter.consumeReplay()) {
      restoreRepeat(repeat);
      return vm.finiteBranch(repeat.start);
    }
    counter.finish();
    --track.repeatDepth;
    return {};
  }

  void echoDelay(u8 value) {
    program.echo.delay = value;
    emitEcho();
  }

  void echoFeedback(u8 value) {
    program.echo.feedback = static_cast<s8>(value);
    emitEcho();
  }

  void echoFilter(u8 value) {
    program.echo.filter = value;
    emitEcho();
  }

  void echoEnabled(bool enabled) {
    program.echo.voiceMask = enabled ? static_cast<u8>(program.echo.voiceMask | track.voiceBit)
                                     : static_cast<u8>(program.echo.voiceMask & static_cast<u8>(~track.voiceBit));
    emitEcho();
  }

  void echoVolume(u8 value, u8 channel) {
    const double gain = math::echoGain(track.layout.version, value, track.layout.initialMasterVolume);
    if (channel != 1) {
      program.echo.left = gain;
    }
    if (channel != 0) {
      program.echo.right = gain;
    }
    emitEcho();
  }

  void echoReverse(u8 channel) {
    if (channel == 0 || channel == 2) {
      program.echo.reverseLeft = channel == 0;
    }
    if (channel == 1 || channel == 2) {
      program.echo.reverseRight = channel == 1;
    }
    emitEcho();
  }

  void dryPhase(u8 mode) {
    if (mode == 0) {
      track.invertLeft = true;
    } else if (mode == 1) {
      track.invertRight = true;
    } else {
      track.invertLeft = false;
      track.invertRight = false;
    }
    emitBalance();
  }

  void updateEnvelope(Envelope envelope, EnvelopeFields field, EnvelopeFields originalSibling) {
    constexpr VoiceEnvelopeScope scope = VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks;
    if (track.layout.version == Version::Original) {
      out.restoreEnvelope(originalSibling, scope);
    }
    out.updateEnvelope(EnvelopeUpdate::set(std::move(envelope), field), scope);
  }

  void attack(u8 raw) {
    updateEnvelope(Envelope{.attackSeconds = snesDspAdsrAttackSeconds(raw & 0x0f)}, EnvelopeFields::Attack,
                   EnvelopeFields::Decay);
  }

  void decay(u8 raw) {
    updateEnvelope(Envelope{.decaySeconds = snesDspAdsrDecaySeconds(raw & 7)}, EnvelopeFields::Decay,
                   EnvelopeFields::Attack);
  }

  void sustain(u8 raw) {
    updateEnvelope(Envelope{.sustainAmplitude = ((raw & 7) + 1) / 8.0}, EnvelopeFields::Sustain,
                   EnvelopeFields::SecondDecay);
  }

  void sustainRate(u8 raw) {
    updateEnvelope(Envelope{.secondDecaySeconds = snesDspAdsrSustainSeconds(raw & 0x1f)},
                   EnvelopeFields::SecondDecay, EnvelopeFields::Sustain);
  }

  void setVibratoDepth(u8 value) {
    track.vibratoDepth = value;
    emitVibrato();
  }

  void setVibratoRate(u8 value) {
    track.vibratoRate = value;
    auto context = lfoContext({}, false);
    context.frequencyHz = value / 8.0;
    out.vibratoRate(*context.frequencyHz, context);
  }

  void setTremoloDepth(u8 value) {
    track.tremoloDepth = value;
    emitTremolo();
  }

  void setTremoloRate(u8 value) {
    track.tremoloRate = value;
    auto context = lfoContext({}, false);
    context.frequencyHz = value / 8.0;
    out.tremoloRate(*context.frequencyHz, context);
  }

  void pitchDrift(u8 value, bool negative) {
    track.driftRate = static_cast<s8>(negative ? -static_cast<int>(value & 0x7f) : value);
  }

  void pitchDriftOff() { track.driftRate = 0; }

  void tick() {
    if (!track.layout.hasPitchDrift || track.driftRate == 0) {
      return;
    }
    track.driftClock += math::timerTarget(program.tempoTarget);
    bool changed = false;
    while (track.driftClock >= 125) {
      track.driftClock -= 125;
      track.drift = static_cast<s16>(track.drift + track.driftRate);
      changed = true;
    }
    if (changed) {
      emitDrift();
    }
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Address sectionAddress(const Layout& layout, u16 encoded) {
  return Address{layout.version == Version::Modern ? static_cast<u16>(layout.sequenceBaseAddress + encoded) : encoded};
}

[[nodiscard]] std::set<u32> playlistOffsets(ByteReader reader, u16 start) {
  std::set<u32> result;
  u16 cursor = start;
  for (u32 commands = 0; commands < 4096 && reader.has(cursor, 1); ++commands) {
    result.insert(cursor);
    const u8 value = reader.u8At(cursor);
    if (value == 0xff) {
      break;
    }
    cursor = static_cast<u16>(cursor + ((value & 0x80) != 0 ? 1 : 2));
  }
  return result;
}

[[nodiscard]] DecodedBytecodeCommand decodePlaylist(ByteReader reader, u32 begin, const Layout& layout,
                                                    std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, "neverland-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode == 0xff) {
    return cursor.command("Song End", SequenceSemantic::End).end();
  }
  if ((opcode & 0x80) != 0) {
    auto event = cursor.command("Section Transpose", SequenceSemantic::Pitch);
    const u8 semitones = opcode & 0x7f;
    event.opcodeValue("semitones", semitones, SourceValueDisplay::Default, SemanticOperandRole::Pitch);
    return event.invoke<&Playback::transpose>(semitones);
  }
  auto event = cursor.command("Section", SequenceSemantic::Call);
  const u16 encoded = static_cast<u16>((opcode << 8) | event.u8("offset_low", SourceValueDisplay::Hex));
  event.opcodeValue("offset_high", opcode, SourceValueDisplay::Hex);
  const Address destination = sectionAddress(layout, encoded);
  event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::CallTarget);
  return event.invoke<&Playback::enterSection>(static_cast<u16>(begin))
      .jump(destination)
      .discoverTarget(Address{static_cast<u16>(begin + 2)});
}

template <auto Handler, class... Args>
[[nodiscard]] DecodedBytecodeCommand valueSubcommand(Cursor& cursor, std::string_view label,
                                                     SequenceSemantic semantic, Args... args) {
  auto event = cursor.command(label, semantic);
  static_cast<void>(event.u8("command", SourceValueDisplay::Hex));
  return event.invoke<Handler>(event.u8("value", SourceValueDisplay::Hex), args...);
}

template <auto Handler, class... Args>
[[nodiscard]] DecodedBytecodeCommand fixedSubcommand(Cursor& cursor, std::string_view label,
                                                     SequenceSemantic semantic, Args... args) {
  auto event = cursor.command(label, semantic);
  static_cast<void>(event.u8("command", SourceValueDisplay::Hex));
  static_cast<void>(event.u8("value", SourceValueDisplay::Hex));
  return event.invoke<Handler>(args...);
}

[[nodiscard]] DecodedBytecodeCommand sourceSubcommand(Cursor& cursor, std::string_view label,
                                                      SequenceSemantic semantic, std::string_view category) {
  auto event = cursor.command(label, semantic, CommandPlaybackStatus::SourceOnly, category);
  static_cast<void>(event.u8("command", SourceValueDisplay::Hex));
  static_cast<void>(event.u8("value", SourceValueDisplay::Hex));
  return event;
}

[[nodiscard]] DecodedBytecodeCommand decodeSubcommand(Cursor& cursor, Version version, bool pitchDrift, u8 command) {
  switch (command) {
    case 0x00: return valueSubcommand<&Playback::echoDelay>(cursor, "Echo Delay", SequenceSemantic::State);
    case 0x01: return valueSubcommand<&Playback::echoFeedback>(cursor, "Echo Feedback", SequenceSemantic::State);
    case 0x02: return valueSubcommand<&Playback::echoFilter>(cursor, "Echo Filter", SequenceSemantic::State);
    case 0x03: return fixedSubcommand<&Playback::echoEnabled>(cursor, "Echo On", SequenceSemantic::State, true);
    case 0x04: return fixedSubcommand<&Playback::echoEnabled>(cursor, "Echo Off", SequenceSemantic::State, false);
    default: break;
  }

  if (version == Version::Original) {
    switch (command) {
      case 0x0b: return valueSubcommand<&Playback::attack>(cursor, "Attack Rate", SequenceSemantic::Envelope);
      case 0x0c: return valueSubcommand<&Playback::decay>(cursor, "Decay Rate", SequenceSemantic::Envelope);
      case 0x0d: return valueSubcommand<&Playback::sustain>(cursor, "Sustain Level", SequenceSemantic::Envelope);
      case 0x0e: return valueSubcommand<&Playback::sustainRate>(cursor, "Sustain Rate", SequenceSemantic::Envelope);
      case 0x0f:
        return valueSubcommand<&Playback::echoVolume>(cursor, "Echo Volume Right", SequenceSemantic::State, u8{1});
      case 0x10:
        return valueSubcommand<&Playback::echoVolume>(cursor, "Echo Volume Left", SequenceSemantic::State, u8{0});
      case 0x11: return sourceSubcommand(cursor, "DSP Noise On", SequenceSemantic::State, "noise");
      case 0x13:
        return sourceSubcommand(cursor, "DSP Pitch Modulation On", SequenceSemantic::State, "pitch-modulation");
      case 0x14:
        return sourceSubcommand(cursor, "DSP Noise Frequency", SequenceSemantic::State, "noise-frequency");
      case 0x15: return sourceSubcommand(cursor, "DSP Noise Off", SequenceSemantic::State, "noise");
      case 0x16:
        return valueSubcommand<&Playback::echoVolume>(cursor, "Echo Volume", SequenceSemantic::State, u8{2});
      case 0x17:
        return sourceSubcommand(cursor, "DSP Pitch Modulation Off", SequenceSemantic::State, "pitch-modulation");
      default: break;
    }
  } else {
    switch (command) {
      case 0x05:
      case 0x06:
        if (pitchDrift) {
          return valueSubcommand<&Playback::pitchDrift>(cursor, command == 5 ? "Pitch Drift Up" : "Pitch Drift Down",
                                                       SequenceSemantic::Pitch, command == 6);
        }
        break;
      case 0x07:
        if (pitchDrift) {
          return fixedSubcommand<&Playback::pitchDriftOff>(cursor, "Pitch Drift Off", SequenceSemantic::Pitch);
        }
        break;
      case 0x08: return valueSubcommand<&Playback::attack>(cursor, "Attack Rate", SequenceSemantic::Envelope);
      case 0x09: return valueSubcommand<&Playback::decay>(cursor, "Decay Rate", SequenceSemantic::Envelope);
      case 0x0a: return valueSubcommand<&Playback::sustain>(cursor, "Sustain Level", SequenceSemantic::Envelope);
      case 0x0b: return valueSubcommand<&Playback::sustainRate>(cursor, "Sustain Rate", SequenceSemantic::Envelope);
      case 0x0c:
        return sourceSubcommand(cursor, "Unused Envelope Rate", SequenceSemantic::State, "unused-envelope-rate");
      case 0x0d:
        return valueSubcommand<&Playback::echoVolume>(cursor, "Echo Volume", SequenceSemantic::State, u8{2});
      case 0x0e:
        return valueSubcommand<&Playback::echoVolume>(cursor, "Echo Volume Right", SequenceSemantic::State, u8{1});
      case 0x0f:
        return valueSubcommand<&Playback::echoVolume>(cursor, "Echo Volume Left", SequenceSemantic::State, u8{0});
      case 0x10:
        return sourceSubcommand(cursor, "DSP Noise Frequency", SequenceSemantic::State, "noise-frequency");
      case 0x11: return sourceSubcommand(cursor, "DSP Noise On", SequenceSemantic::State, "noise");
      case 0x12: return sourceSubcommand(cursor, "DSP Noise Off", SequenceSemantic::State, "noise");
      case 0x13:
        return sourceSubcommand(cursor, "DSP Pitch Modulation On", SequenceSemantic::State, "pitch-modulation");
      case 0x14:
        return sourceSubcommand(cursor, "DSP Pitch Modulation Off", SequenceSemantic::State, "pitch-modulation");
      case 0x15:
        return valueSubcommand<&Playback::setTremoloDepth>(cursor, "Tremolo Depth", SequenceSemantic::Modulation);
      case 0x16:
        return valueSubcommand<&Playback::setVibratoDepth>(cursor, "Vibrato Depth", SequenceSemantic::Modulation);
      case 0x17:
        return valueSubcommand<&Playback::setTremoloRate>(cursor, "Tremolo Rate", SequenceSemantic::Modulation);
      case 0x18:
        return valueSubcommand<&Playback::setVibratoRate>(cursor, "Vibrato Rate", SequenceSemantic::Modulation);
      case 0x19:
        return fixedSubcommand<&Playback::dryPhase>(cursor, "Invert Left Dry Phase", SequenceSemantic::Pan, u8{0});
      case 0x1a:
        return fixedSubcommand<&Playback::dryPhase>(cursor, "Invert Right Dry Phase", SequenceSemantic::Pan, u8{1});
      case 0x1b:
        return fixedSubcommand<&Playback::dryPhase>(cursor, "Normal Dry Phase", SequenceSemantic::Pan, u8{2});
      case 0x1c:
        return fixedSubcommand<&Playback::echoReverse>(cursor, "Invert Left Echo Phase", SequenceSemantic::State,
                                                      u8{0});
      case 0x1d:
        return fixedSubcommand<&Playback::echoReverse>(cursor, "Invert Right Echo Phase", SequenceSemantic::State,
                                                      u8{1});
      case 0x1e:
        return fixedSubcommand<&Playback::echoReverse>(cursor, "Normal Echo Phase", SequenceSemantic::State, u8{2});
      default: break;
    }
  }
  return sourceSubcommand(cursor, "Reserved Extended Command", SequenceSemantic::Meta, "reserved");
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, const Layout& layout,
                                                   std::vector<Diagnostic>* diagnostics,
                                                   ReferencedPrograms* references = nullptr) {
  Cursor cursor(reader, begin, "neverland-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode < 0xf0) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const bool save = opcode < 0x80;
    const u8 key = opcode & 0x7f;
    event.opcodeValue("key", key, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const u8 wait = save ? event.u8("wait", SemanticOperandRole::Duration) : 0;
    const u8 duration = save ? event.u8("duration", SemanticOperandRole::Duration) : 0;
    const u8 velocity = save ? event.u8("volume", SemanticOperandRole::Level) : 0;
    return event.invokeFlow<&Playback::note>(key, wait, duration, velocity, save);
  }

  switch (opcode) {
    case 0xf0: {
      auto event = cursor.command(layout.version == Version::Modern ? "Modulation" : "Reserved Wait",
                                  layout.version == Version::Modern ? SequenceSemantic::Modulation
                                                                    : SequenceSemantic::Rest);
      const u8 wait = event.u8("wait", SemanticOperandRole::Duration);
      const u8 value = event.u8(layout.version == Version::Modern ? "strength" : "unused",
                                layout.version == Version::Modern ? SemanticOperandRole::Modulation
                                                                  : SemanticOperandRole::Value);
      return layout.version == Version::Modern ? event.invokeFlow<&Playback::modulation>(wait, value)
                                               : event.invokeFlow<&Playback::delay>(wait);
    }
    case 0xf1: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      const u8 wait = event.u8("wait", SemanticOperandRole::Duration);
      return event.invokeFlow<&Playback::volume>(wait, event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xf2: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const u8 wait = event.u8("wait", SemanticOperandRole::Duration);
      return event.invokeFlow<&Playback::pan>(wait, event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0xf3:
      if (layout.version == Version::Modern) {
        auto event = cursor.command("Delay", SequenceSemantic::Rest);
        return event.invokeFlow<&Playback::delay>(event.u8("wait", SemanticOperandRole::Duration));
      }
      return cursor.ignored("Reserved", 1, "reserved");
    case 0xf4:
      if (layout.version == Version::Modern) {
        auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
        const u8 wait = event.u8("wait", SemanticOperandRole::Duration);
        return event.invokeFlow<&Playback::tempo>(wait, event.u8("timer_target"));
      }
      return cursor.ignored("Reserved", 1, "reserved");
    case 0xf5:
    case 0xf8:
    case 0xf9:
    case 0xfa:
      return cursor.ignored("Reserved", layout.version == Version::Original ? 1 : 0, "reserved");
    case 0xf6: {
      auto event = cursor.command("Pitch Scale", SequenceSemantic::Pitch);
      const u8 wait = event.u8("wait", SemanticOperandRole::Duration);
      const u8 scale = event.u8("scale", SemanticOperandRole::Pitch);
      if (layout.version == Version::Original) {
        static_cast<void>(event.u8("unused"));
      }
      return event.invokeFlow<&Playback::tuning>(wait, scale);
    }
    case 0xf7: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      const u8 wait = event.u8("wait", SemanticOperandRole::Duration);
      const u8 program = event.u8("program", SemanticOperandRole::InstrumentProgram);
      if (references != nullptr) {
        references->insert(program);
      }
      return event.invokeFlow<&Playback::programChange>(wait, program);
    }
    case 0xfb:
      return cursor.command("Repeat Start", SequenceSemantic::Repeat)
          .invokeFlow<&Playback::repeatStart>(Address{static_cast<u16>(begin + 1)});
    case 0xfc: {
      auto event = cursor.command("Repeat End", SequenceSemantic::Repeat);
      return event.invokeFlow<&Playback::repeatEnd>(event.u8("count", SemanticOperandRole::Count));
    }
    case 0xfd:
      return cursor.command("Section End", SequenceSemantic::Return)
          .invoke<&Playback::sectionEnd>()
          .end();
    case 0xfe:
      return cursor.command("Channel End", SequenceSemantic::End).end();
    case 0xff:
      return decodeSubcommand(cursor, layout.version, layout.hasPitchDrift,
                              reader.has(begin + 1, 1) ? reader.u8At(begin + 1) : u8{0xff});
    default:
      return cursor.unsupported("Invalid Command").stop();
  }
}

[[nodiscard]] TrackProgram decodeTrack(const TrackDecodeScope& scope, ByteReader reader, const Layout& layout,
                                       u32 trackNumber, u32 playlistAddress, std::vector<Diagnostic>* diagnostics,
                                       ReferencedPrograms* references) {
  const std::set<u32> playlist = playlistOffsets(reader, static_cast<u16>(playlistAddress));
  return scope.decode(trackNumber, playlistAddress, [&](u32 offset) {
    return playlist.contains(offset) ? decodePlaylist(reader, offset, layout, diagnostics)
                                     : decodeCommand(reader, offset, layout, diagnostics, references);
  });
}

}  // namespace

SequenceProgramConfig sequenceConfig(const Layout& layout) {
  const bool original = layout.version == Version::Original;
  return SequenceProgramConfig{
      .commandKindPrefix = "neverland-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kCommandLimit,
              .inferLoopsFromRepeatedState = false,
              .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
              .initialLevel = math::gain(original ? 0x60 : (layout.hasPitchDrift ? 0x7f : 0x70)),
              .initialMasterLevel = math::signedDspGain(static_cast<s8>(layout.initialMasterVolume)),
              .initialReverbSend = 0.0,
              .initialStereoBalance = math::balance(0x40),
              .initialMonoModeChannels = 0,
              .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(layout.initialTempo),
          },
  };
}

SequenceRuntime sequenceRuntime(ByteReader reader, const Layout& layout) {
  return makeCompiledRuntime<Cursor, ProgramState>(RuntimeConfig{.reader = reader, .layout = layout});
}

TrackProgram decodeSourceTrack(ByteReader reader, const Layout& layout, u32 trackNumber, u32 playlistAddress,
                               std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope scope{.reader = reader, .maxCommands = kCommandLimit};
  return decodeTrack(scope, reader, layout, trackNumber, playlistAddress, diagnostics, nullptr);
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const u32 headerSize = layout.version == Version::Modern ? 0x50 : 0x40;
  const SourceRange header = reader.range(layout.sequenceBaseAddress, headerSize);
  const SequenceProgramConfig config = sequenceConfig(layout);
  SequenceDecodeSession sequence{reader, config, sequenceId, header, sourceMap, kCommandLimit, kAramSize};
  ReferencedPrograms references;
  for (u32 track = 0; track < kTrackCount; ++track) {
    const TrackLayout& source = layout.tracks[track];
    if (!source.active) {
      continue;
    }
    if (source.percussion) {
      for (const PercussionPatch& patch : layout.percussion) {
        references.insert(patch.program);
      }
    } else {
      references.insert(0);
    }
    const std::set<u32> playlist = playlistOffsets(reader, source.playlistAddress);
    sequence.addTrack(
        track, source.pointerRange, source.playlistAddress,
        [&](u32 offset) {
          return playlist.contains(offset)
                     ? decodePlaylist(reader, offset, layout, diagnostics)
                     : decodeCommand(reader, offset, layout, diagnostics, &references);
        },
        layout.version == Version::Modern ? static_cast<u16>(source.playlistAddress - layout.sequenceBaseAddress)
                                          : source.playlistAddress);
  }
  SequenceProgram program = sequence.finish(sequenceRuntime(reader, layout));
  return SequenceParse{.program = std::move(program), .references = std::move(references), .headerRange = header};
}

}  // namespace vgmtrans::formats::neverland_snes
