/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MP2k/MP2k.h"

#include "value/formats/MP2k/MP2kEnvelope.h"

#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::mp2k {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 262144;
constexpr size_t kToneCount = 128;
constexpr size_t kToneWaveBase = kToneCount;
constexpr size_t kToneEnvelopeBase = kToneCount * 2;
constexpr size_t kReverbIndex = kToneCount * 3;
constexpr size_t kDriverDataSize = kReverbIndex + 1;
constexpr ValueQuantization kMp2kLevelQuantization{.levels = 128};
constexpr std::array<u8, 49> kClockTable{
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    28, 30, 32, 36, 40, 42, 44, 48, 52, 54, 56, 60, 64, 66, 68, 72, 76, 78, 80, 84, 88, 90, 92, 96,
};

struct ToneState {
  u8 type = 0;
  u8 key = 0;
  u8 length = 0;
  u8 panSweep = 0;
  u32 wave = 0;
  u8 attack = 0;
  u8 decay = 0;
  u8 sustain = 0;
  u8 release = 0;
};

struct ProgramState {
  explicit ProgramState(const SequenceProgram& program) {
    const auto& data = program.config.driverData;
    const auto word = [&](size_t index) { return index < data.size() ? data[index] : u32{0}; };
    const size_t toneCount = std::min(kToneCount, data.size());
    tones.reserve(toneCount);
    for (size_t index = 0; index < toneCount; ++index) {
      const u32 packed = word(index);
      const u32 envelope = word(kToneEnvelopeBase + index);
      tones.push_back(ToneState{
          .type = static_cast<u8>(packed),
          .key = static_cast<u8>(packed >> 8),
          .length = static_cast<u8>(packed >> 16),
          .panSweep = static_cast<u8>(packed >> 24),
          .wave = word(kToneWaveBase + index),
          .attack = static_cast<u8>(envelope),
          .decay = static_cast<u8>(envelope >> 8),
          .sustain = static_cast<u8>(envelope >> 16),
          .release = static_cast<u8>(envelope >> 24),
      });
    }
    if (data.size() > kReverbIndex) {
      reverbSend = data[kReverbIndex] / 127.0;
    }
  }

  std::vector<ToneState> tones;
  double reverbSend = 0.0;
  std::array<u8, 256> memory{};
};

struct LfoState {
  u8 speed = 22;
  u8 delay = 0;
  u8 depth = 0;
  u8 type = 0;
  u8 phase = 0;
  u8 delayRemaining = 0;
  u64 tick = 0;
  bool clocked = false;
  bool emitted = false;
};

struct ActiveNote {
  PerformanceNoteId id;
  u64 endTick = 0;
};

struct TrackState {
  explicit TrackState(const TrackProgram& program)
      : usesModulation(trackUsesSemantic(program, SequenceSemantic::Modulation)) {}

  bool usesModulation = false;
  s32 transpose = 0;
  u8 bendRange = 2;
  u8 program = 0;
  ToneState tone{.type = 1};
  u8 previousKey = 0;
  u8 previousVelocity = 0;
  u8 patternDepth = 0;
  u8 pseudoEchoVolume = 0;
  u8 pseudoEchoLength = 0;
  u8 volume = 0;
  u8 pan = 64;
  u32 sampleStart = 0;
  std::array<u8, 256> soundRegisters{};
  LfoState lfo;
  std::map<u8, PerformanceNoteId> tiedNotes;
  std::vector<ActiveNote> activeNotes;
};

[[nodiscard]] s32 arithmeticShiftRight(s32 value, u32 bits) {
  if (value >= 0 || bits == 0) {
    return value >> bits;
  }
  return -static_cast<s32>((static_cast<u32>(-value) + (u32{1} << bits) - 1) >> bits);
}

[[nodiscard]] double levelFrom7BitLinear(u8 value) {
  return value / 127.0;
}

[[nodiscard]] double panPosition(u8 value) {
  // TrkVolPitSet forms y = 2 * (value - 64), then uses (127-y)/256
  // and (y+128)/256 as its left/right factors. The position describes their
  // ratio; Playback::pan retains the separate 255/256 total gain.
  const s32 y = std::clamp((static_cast<s32>(value) - 64) * 2, -128, 127);
  return (2.0 * y + 1.0) / 255.0;
}

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& programState;

  [[nodiscard]] bool cgbTone() const { return (track.tone.type & 7) != 0; }

  [[nodiscard]] s32 modulationValue() const {
    if (track.lfo.speed == 0 || track.lfo.depth == 0 || track.lfo.delayRemaining != 0) {
      return 0;
    }
    const u8 phase = track.lfo.phase;
    const s32 triangle = phase < 0x40 || phase >= 0xc0 ? static_cast<s8>(phase) : 0x80 - phase;
    return arithmeticShiftRight(track.lfo.depth * triangle, 6);
  }

  [[nodiscard]] u8 cgbEnvelopeGoal(u8 velocity, bool modulated) const {
    u32 combined = static_cast<u32>(track.volume) * 64 >> 5;
    const s32 modulation = modulated ? modulationValue() : 0;
    if (modulated && track.lfo.type == 1) {
      combined = combined * static_cast<u32>(modulation + 128) >> 7;
    }
    s32 position = (static_cast<s32>(track.pan) - 64) * 2;
    if (modulated && track.lfo.type == 2) {
      position += modulation;
    }
    position = std::clamp(position, -128, 127);
    const u32 rightTrack = combined * static_cast<u32>(position + 128) >> 8;
    const u32 leftTrack = combined * static_cast<u32>(127 - position) >> 8;
    const u32 right = std::min<u32>(255, static_cast<u32>(velocity) * 128 * rightTrack >> 14);
    const u32 left = std::min<u32>(255, static_cast<u32>(velocity) * 127 * leftTrack >> 14);
    return static_cast<u8>(std::min<u32>(15, (left + right) >> 4));
  }

  [[nodiscard]] double cgbOutputLevel(u8 envelope) const {
    if ((track.tone.type & 7) != 3) {
      return std::min<u8>(envelope, 15) / 15.0;
    }
    constexpr std::array<double, 16> waveLevels{
        0.0, 0.0, 0.25, 0.25, 0.25, 0.25, 0.5, 0.5, 0.5, 0.5, 0.75, 0.75, 0.75, 0.75, 1.0, 1.0,
    };
    return waveLevels[std::min<u8>(envelope, 15)];
  }

  void emitCgbLevel() { out.level(cgbOutputLevel(cgbEnvelopeGoal(127, false)), kMp2kLevelQuantization); }

  void emitLevel() {
    if (cgbTone()) {
      emitCgbLevel();
    } else {
      out.level(levelFrom7BitLinear(track.volume), kMp2kLevelQuantization);
    }
  }

  void emitCgbEnvelope(u8 goal) {
    const double peak = cgbOutputLevel(goal);
    const u8 sustainGoal = static_cast<u8>((static_cast<u32>(goal) * track.tone.sustain + 15) >> 4);
    const auto stageSeconds = [goal](u8 rate) {
      const u8 period = rate & 7;
      return period == 0 ? 0.0 : static_cast<double>(goal) * period / 64.0;
    };
    out.updateEnvelope(EnvelopeUpdate::replace(Envelope{
        .attackSeconds = stageSeconds(track.tone.attack),
        .decaySeconds = stageSeconds(track.tone.decay),
        .releaseSeconds = stageSeconds(track.tone.release),
        .sustainAmplitude = peak == 0.0 ? 0.0 : cgbOutputLevel(sustainGoal) / peak,
    }));
  }

  void pan(u8 value) {
    track.pan = value;
    out.pan(panPosition(value), 255.0 / 256.0);
    if (cgbTone()) {
      emitCgbLevel();
    }
  }

  void syncLfo() {
    const u64 now = vm.tick();
    if (!track.lfo.clocked) {
      track.lfo.tick = now;
      track.lfo.clocked = true;
      return;
    }
    u64 elapsed = now - track.lfo.tick;
    track.lfo.tick = now;
    if (track.lfo.speed == 0 || track.lfo.depth == 0) {
      return;
    }
    const u64 delayed = std::min<u64>(elapsed, track.lfo.delayRemaining);
    track.lfo.delayRemaining -= static_cast<u8>(delayed);
    elapsed -= delayed;
    track.lfo.phase = static_cast<u8>(track.lfo.phase + elapsed * track.lfo.speed);
  }

  [[nodiscard]] LfoPerformanceContext lfoContext(bool restartPhase = false) const {
    // MPlayMain increments the byte accumulator before calculating its first
    // triangle sample. A note-triggered delay consumes exactly N source ticks
    // before sampling this already-advanced phase on tick N.
    const double resetPhase = track.lfo.speed / 256.0;
    return LfoPerformanceContext{
        .cyclesPerTick = track.lfo.speed / 256.0,
        .delayTicks = track.lfo.delay,
        .delayIsTempoRelative = true,
        .waveform = LfoWaveform::Triangle,
        .initialPhaseCycles = resetPhase,
        .delayAppliesOnNoteRestartOnly = true,
        .restartPhase = restartPhase,
        .phaseRunsAtZeroDepth = false,
        .delayRunsWhileInactive = false,
        .tremoloGainMode = TremoloGainMode::BipolarAroundNominal,
    };
  }

  void emitLfoDepth(u8 type, u8 depth, bool restartPhase = false) {
    auto context = lfoContext(restartPhase);
    switch (type) {
      case 0:
        out.vibratoDepth(depth / 16.0, std::move(context));
        break;
      case 1:
        out.tremoloLinearGainDepth(depth / 128.0, std::move(context));
        break;
      case 2:
        out.panLfoDepth(2.0 * depth / 255.0, std::move(context));
        break;
      default:
        break;
    }
  }

  void emitLfoRate(u8 type, bool restartPhase = false) {
    const double cycles = track.lfo.speed / 256.0;
    auto context = lfoContext(restartPhase);
    switch (type) {
      case 0:
        out.vibratoRateCyclesPerTick(cycles, std::move(context));
        break;
      case 1:
        out.tremoloRateCyclesPerTick(cycles, std::move(context));
        break;
      case 2:
        out.panLfoRateCyclesPerTick(cycles, std::move(context));
        break;
      default:
        break;
    }
  }

  bool initializeLfo() {
    if (!track.usesModulation || track.lfo.emitted) {
      return false;
    }
    track.lfo.emitted = true;
    emitLfoRate(track.lfo.type);
    emitLfoDepth(track.lfo.type, track.lfo.depth);
    return true;
  }

  void lfoSpeed(u8 speed) {
    syncLfo();
    const bool restart = track.lfo.speed == 0 || speed == 0;
    track.lfo.speed = speed;
    if (speed == 0) {
      track.lfo.phase = 0;
    }
    if (!initializeLfo()) {
      emitLfoRate(track.lfo.type, restart);
    }
  }

  void lfoDelay(u8 delay) {
    syncLfo();
    track.lfo.delay = delay;
    if (!initializeLfo()) {
      emitLfoRate(track.lfo.type);
      emitLfoDepth(track.lfo.type, track.lfo.depth);
    }
  }

  void modulationDepth(u8 depth) {
    syncLfo();
    const bool restart = depth == 0;
    track.lfo.depth = depth;
    if (depth == 0) {
      track.lfo.phase = 0;
    }
    if (!initializeLfo()) {
      emitLfoDepth(track.lfo.type, depth, restart);
    }
  }

  void modulationType(u8 type) {
    syncLfo();
    const bool alreadyEmitted = track.lfo.emitted;
    if (alreadyEmitted) {
      emitLfoDepth(track.lfo.type, 0);
    }
    track.lfo.type = type;
    if (!initializeLfo() && alreadyEmitted) {
      emitLfoRate(type);
      emitLfoDepth(type, track.lfo.depth);
    }
  }

  void program(u8 number) {
    track.program = number;
    track.tone = number < programState.tones.size() ? programState.tones[number] : ToneState{};
    out.instrument(0, number, InstrumentEnvelopeMode::UseInstrumentEnvelope);
    out.reverb(cgbTone() ? 0.0 : programState.reverbSend);
    emitLevel();
  }

  void tempo(u8 raw) {
    if (raw == 0) {
      return;
    }
    const double bpm = raw * 2.0 * kGbaMixerFrameRate / 60.0;
    out.tempo(static_cast<u32>(std::llround(60000000.0 / bpm)));
  }

  void volume(u8 value) {
    track.volume = value;
    emitLevel();
  }

  void pitchBend(u8 raw) { out.pitchBend((static_cast<s32>(raw) - 64) * track.bendRange / 64.0); }

  void tune(u8 raw) { out.tuning((static_cast<s32>(raw) - 64) * 100.0 / 64.0); }

  void closeNote(PerformanceNoteId id, u64 tick) {
    static_cast<void>(out.setNoteEnd(id, tick));
    if (const auto found = std::ranges::find(track.activeNotes, id, &ActiveNote::id);
        found != track.activeNotes.end()) {
      found->endTick = tick;
    }
  }

  void note(u8 key, u8 velocity, u32 duration, bool tie) {
    syncLfo();
    initializeLfo();
    track.previousKey = key;
    track.previousVelocity = velocity;
    std::erase_if(track.activeNotes, [&](const ActiveNote& note) { return note.endTick <= vm.tick(); });
    const double playedKey = std::clamp<s32>(static_cast<s32>(key) + track.transpose, 0, 127);
    if (track.lfo.delay != 0) {
      track.lfo.phase = 0;
      track.lfo.delayRemaining = track.lfo.delay;
    }
    double noteVelocity = levelFrom7BitLinear(velocity);
    if (cgbTone()) {
      const u8 referenceGoal = cgbEnvelopeGoal(127, false);
      const u8 noteGoal = cgbEnvelopeGoal(velocity, true);
      const double reference = cgbOutputLevel(referenceGoal);
      emitCgbLevel();
      emitCgbEnvelope(noteGoal);
      noteVelocity = reference == 0.0 ? 0.0 : cgbOutputLevel(noteGoal) / reference;
      if (track.lfo.type == 1) {
        noteVelocity /= (modulationValue() + 128) / 128.0;
      }
    }
    std::optional<double> maximumDurationMilliseconds;
    const u8 cgbType = track.tone.type & 7;
    if (cgbType != 0 && track.tone.length != 0) {
      const u32 counter = cgbType == 3 ? 256u - track.tone.length : 64u - (track.tone.length & 0x3f);
      maximumDurationMilliseconds = counter * 1000.0 / 256.0;
    }
    const auto note = out.note(NotePerformanceEvent{
        .key = playedKey,
        .linearVelocity = noteVelocity,
        .durationTicks = tie ? std::numeric_limits<u32>::max() : duration,
        .maximumDurationMilliseconds = maximumDurationMilliseconds,
        .restartsLfoPhase = track.lfo.delay != 0,
    });
    if (tie) {
      if (const auto found = track.tiedNotes.find(key); found != track.tiedNotes.end()) {
        closeNote(found->second, vm.tick());
      }
      track.tiedNotes.insert_or_assign(key, note);
    }
    const u64 endTick = tie || vm.tick() > std::numeric_limits<u64>::max() - duration ? std::numeric_limits<u64>::max()
                                                                                      : vm.tick() + duration;
    track.activeNotes.push_back(ActiveNote{.id = note, .endTick = endTick});
  }

  void endTie(u8 key) {
    if (const auto found = track.tiedNotes.find(key); found != track.tiedNotes.end()) {
      closeNote(found->second, vm.tick());
      track.tiedNotes.erase(found);
    }
  }

  void toneWave(u32 wave) { track.tone.wave = wave; }

  void toneType(u8 type) { track.tone.type = type; }

  void pseudoEchoVolume(u8 volume) { track.pseudoEchoVolume = volume; }

  void pseudoEchoLength(u8 length) { track.pseudoEchoLength = length; }

  void toneLength(u8 length) { track.tone.length = length; }

  void tonePanSweep(u8 panSweep) { track.tone.panSweep = panSweep; }

  void sampleStart(u32 start) { track.sampleStart = start; }

  void port(u8 address, u8 value) { track.soundRegisters[address] = value; }

  void attack(u8 value) {
    track.tone.attack = value;
    out.updateEnvelope(EnvelopeUpdate::set(
        Envelope{.attackSeconds = cgbTone() ? cgbEnvelopeSeconds(value) : directAttackSeconds(value)},
        EnvelopeFields::Attack));
  }

  void decay(u8 value) {
    track.tone.decay = value;
    out.updateEnvelope(
        EnvelopeUpdate::set(Envelope{.decaySeconds = cgbTone() ? cgbEnvelopeSeconds(value) : directDecaySeconds(value)},
                            EnvelopeFields::Decay));
  }

  void sustain(u8 value) {
    track.tone.sustain = value;
    out.updateEnvelope(
        EnvelopeUpdate::set(Envelope{.sustainAmplitude = cgbTone() ? std::min<u8>(value, 15) / 15.0 : value / 255.0},
                            EnvelopeFields::Sustain));
  }

  void release(u8 value) {
    track.tone.release = value;
    out.updateEnvelope(EnvelopeUpdate::set(
        Envelope{.releaseSeconds = cgbTone() ? cgbEnvelopeSeconds(value) : directReleaseSeconds(value)},
        EnvelopeFields::Release));
  }

  [[nodiscard]] Effects repeat(u8 count, Address destination) {
    return count == 0 ? vm.declaredLoop(destination) : vm.countedRepeatUntil(0, count, destination);
  }

  [[nodiscard]] Effects pattern(Address destination) {
    if (track.patternDepth == 3) {
      return finish();
    }
    ++track.patternDepth;
    return vm.call(destination);
  }

  [[nodiscard]] Effects patternEnd() {
    if (track.patternDepth == 0) {
      return vm.fallthrough();
    }
    --track.patternDepth;
    return vm.return_();
  }

  [[nodiscard]] Effects finish() {
    for (const auto& note : track.activeNotes) {
      if (note.endTick > vm.tick()) {
        static_cast<void>(out.setNoteEnd(note.id, vm.tick()));
      }
    }
    track.activeNotes.clear();
    track.tiedNotes.clear();
    return vm.end();
  }

  [[nodiscard]] Effects memAccess(u8 operation, u8 address, u8 data, Address destination) {
    u8& target = programState.memory[address];
    const u8 other = programState.memory[data];
    switch (operation) {
      case 0:
        target = data;
        return {};
      case 1:
        target = static_cast<u8>(target + data);
        return {};
      case 2:
        target = static_cast<u8>(target - data);
        return {};
      case 3:
        target = other;
        return {};
      case 4:
        target = static_cast<u8>(target + other);
        return {};
      case 5:
        target = static_cast<u8>(target - other);
        return {};
      default:
        break;
    }
    bool take = false;
    const u8 compared = operation < 12 ? data : other;
    switch ((operation - 6) % 6) {
      case 0:
        take = target == compared;
        break;
      case 1:
        take = target != compared;
        break;
      case 2:
        take = target > compared;
        break;
      case 3:
        take = target >= compared;
        break;
      case 4:
        take = target <= compared;
        break;
      case 5:
        take = target < compared;
        break;
    }
    return operation <= 17 && take ? vm.finiteBranch(destination) : Effects{};
  }
};

using Mp2kCursor = CompilerCursor<TrackState, Playback>;

struct DecodeState {
  u8 runningStatus = 0xcf;
  u8 key = 0;
  u8 velocity = 0;
};

struct DecodeContext {
  ByteReader reader;
  u32 end = 0;
  std::vector<Diagnostic>* diagnostics = nullptr;
};

[[nodiscard]] std::optional<Address> pointer(Mp2kCursor::Event& event, ByteReader reader, std::string_view name,
                                             SemanticOperandRole role) {
  const auto encoded = event.rawU32le("encoded_pointer", SourceValueDisplay::Address);
  if (!encoded.valid) {
    return std::nullopt;
  }
  if ((encoded.value & 0xfe000000) != 0x08000000) {
    event.warning("MP2k command pointer is outside GBA ROM");
    return std::nullopt;
  }
  const Address result{encoded.value & 0x01ffffff};
  if (result.value >= reader.size()) {
    return std::nullopt;
  }
  return event.resolvedValue(name, encoded, result, SourceValueDisplay::Address, role);
}

[[nodiscard]] u8 parameter(Mp2kCursor& cursor, Mp2kCursor::Event& event, bool running, std::string_view name,
                           SourceValueDisplay display = SourceValueDisplay::Default,
                           SemanticOperandRole role = SemanticOperandRole::Value) {
  return running ? event.opcodeValue(name, cursor.opcode(), display, role) : event.u8(name, display, role);
}

[[nodiscard]] u8 optionalParameter(Mp2kCursor& cursor, Mp2kCursor::Event& event, bool running, u8 previous,
                                   std::string_view name, SourceValueDisplay display,
                                   SemanticOperandRole role = SemanticOperandRole::Value) {
  if (running) {
    return parameter(cursor, event, true, name, display, role);
  }
  const auto next = event.peekU8();
  return next && *next < 0x80 ? event.u8(name, display, role) : previous;
}

[[nodiscard]] DecodedBytecodeCommand decodeStatus(const DecodeContext& context, u32 begin, DecodeState& state,
                                                  u8 status, bool running) {
  Mp2kCursor cursor(context.reader, begin, context.end, "mp2k", context.diagnostics);

  if (status >= 0xcf) {
    const bool tie = status == 0xcf;
    auto event = cursor.command(tie ? "Tie" : "Note", SequenceSemantic::Note);
    const u32 duration = tie ? 0 : kClockTable[status - 0xcf];
    const u8 key = optionalParameter(cursor, event, running, state.key, "key", SourceValueDisplay::MidiNote,
                                     SemanticOperandRole::NoteKey);
    u8 velocity = state.velocity;
    u32 gate = duration;
    if (event.peekU8() && *event.peekU8() < 0x80) {
      velocity = event.u8("velocity", SemanticOperandRole::Level);
      if (event.peekU8() && *event.peekU8() < 0x80) {
        gate += event.u8("gate_extension", SemanticOperandRole::Duration);
      }
    }
    state.key = key;
    state.velocity = velocity;
    return event.invoke<&Playback::note>(key, velocity, gate, tie);
  }

  switch (status) {
    case 0xbd: {
      auto event = cursor.command("Program", SequenceSemantic::Program);
      return event.invoke<&Playback::program>(parameter(cursor, event, running, "program", SourceValueDisplay::Default,
                                                        SemanticOperandRole::InstrumentProgram));
    }
    case 0xbe: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(parameter(cursor, event, running, "volume"));
    }
    case 0xbf: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(parameter(cursor, event, running, "pan"));
    }
    case 0xc0: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      return event.invoke<&Playback::pitchBend>(parameter(cursor, event, running, "bend"));
    }
    case 0xc1: {
      auto event = cursor.command("Pitch Bend Range", SequenceSemantic::Pitch);
      const u8 range = parameter(cursor, event, running, "semitones");
      return event.set<&TrackState::bendRange>(range).emitPitchBendRange(range);
    }
    case 0xc2: {
      auto event = cursor.command("LFO Speed", SequenceSemantic::Modulation);
      return event.invoke<&Playback::lfoSpeed>(parameter(cursor, event, running, "speed"));
    }
    case 0xc3: {
      auto event = cursor.command("LFO Delay", SequenceSemantic::Modulation);
      return event.invoke<&Playback::lfoDelay>(
          parameter(cursor, event, running, "ticks", SourceValueDisplay::Default, SemanticOperandRole::Duration));
    }
    case 0xc4: {
      auto event = cursor.command("Modulation Depth", SequenceSemantic::Modulation);
      return event.invoke<&Playback::modulationDepth>(parameter(cursor, event, running, "depth"));
    }
    case 0xc5: {
      auto event = cursor.command("Modulation Type", SequenceSemantic::Modulation);
      return event.invoke<&Playback::modulationType>(parameter(cursor, event, running, "type"));
    }
    case 0xc8: {
      auto event = cursor.command("Tune", SequenceSemantic::Pitch);
      return event.invoke<&Playback::tune>(parameter(cursor, event, running, "tune"));
    }
    case 0xcd: {
      auto event = cursor.command("Extended Command", SequenceSemantic::State);
      const u8 sub = parameter(cursor, event, running, "subcommand", SourceValueDisplay::Hex);
      switch (sub) {
        case 0:
        case 3:
          return event.invoke<&Playback::finish>();
        case 1:
          return event.invoke<&Playback::toneWave>(event.u32le("wave", SourceValueDisplay::Address));
        case 2:
          return event.invoke<&Playback::toneType>(event.u8("type", SourceValueDisplay::Hex));
        case 4:
          return event.invoke<&Playback::attack>(event.u8("attack"));
        case 5:
          return event.invoke<&Playback::decay>(event.u8("decay"));
        case 6:
          return event.invoke<&Playback::sustain>(event.u8("sustain"));
        case 7:
          return event.invoke<&Playback::release>(event.u8("release"));
        case 8:
          return event.invoke<&Playback::pseudoEchoVolume>(event.u8("pseudo_echo_volume"));
        case 9:
          return event.invoke<&Playback::pseudoEchoLength>(event.u8("pseudo_echo_length"));
        case 10:
          return event.invoke<&Playback::toneLength>(event.u8("length"));
        case 11:
          return event.invoke<&Playback::tonePanSweep>(event.u8("pan_sweep", SourceValueDisplay::Hex));
        case 12:
          return event.wait(event.u16le("ticks", SourceValueDisplay::Default, SemanticOperandRole::Duration));
        case 13:
          return event.invoke<&Playback::sampleStart>(event.u32le("sample_start"));
        default:
          event.warning("Unknown MP2k extended command stopped playback");
          return event.stop();
      }
    }
    case 0xcc: {
      auto event = cursor.command("Sound Register Write", SequenceSemantic::State);
      const u8 address = parameter(cursor, event, running, "register_offset", SourceValueDisplay::Hex);
      return event.invoke<&Playback::port>(address, event.u8("value", SourceValueDisplay::Hex));
    }
    case 0xce: {
      auto event = cursor.command("End Tie", SequenceSemantic::Note);
      const u8 key = optionalParameter(cursor, event, running, state.key, "key", SourceValueDisplay::MidiNote,
                                       SemanticOperandRole::NoteKey);
      return event.invoke<&Playback::endTie>(key);
    }
    default:
      return cursor.command("Undefined MP2k Command", SequenceSemantic::End).invoke<&Playback::finish>();
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(const DecodeContext& context, u32 begin, DecodeState& state) {
  const u8 opcode = context.reader.u8At(begin);
  if (opcode < 0x80) {
    return decodeStatus(context, begin, state, state.runningStatus, true);
  }
  if (opcode <= 0xb0) {
    Mp2kCursor cursor(context.reader, begin, context.end, "mp2k", context.diagnostics);
    return cursor.command("Wait", SequenceSemantic::Rest).wait(kClockTable[opcode - 0x80]);
  }
  if (opcode >= 0xbd) {
    state.runningStatus = opcode;
    return decodeStatus(context, begin, state, opcode, false);
  }

  Mp2kCursor cursor(context.reader, begin, context.end, "mp2k", context.diagnostics);
  switch (opcode) {
    case 0xb1:
      return cursor.command("End", SequenceSemantic::End).invoke<&Playback::finish>();
    case 0xb2: {
      auto event = cursor.command("Goto", SequenceSemantic::Jump);
      const auto destination = pointer(event, context.reader, "destination", SemanticOperandRole::JumpTarget);
      return destination ? event.loopCandidate(*destination) : event.stop();
    }
    case 0xb3: {
      auto event = cursor.command("Pattern", SequenceSemantic::Call);
      const auto destination = pointer(event, context.reader, "destination", SemanticOperandRole::CallTarget);
      return destination
                 ? event.invoke<&Playback::pattern>(*destination).mayBranchTo(*destination).requireRuntimeControlFlow()
                 : event.stop();
    }
    case 0xb4: {
      auto event = cursor.command("Pattern End", SequenceSemantic::Return);
      event.invoke<&Playback::patternEnd>();
      // The driver ignores PEND outside a pattern, so discovery must retain
      // both the physical continuation and a caller's return address.
      event.discoverTarget(event.nextAddress());
      return event.discoverReturn();
    }
    case 0xb5: {
      auto event = cursor.command("Repeat", SequenceSemantic::Repeat);
      const u8 count = event.u8("count");
      const auto destination = pointer(event, context.reader, "destination", SemanticOperandRole::JumpTarget);
      return destination
                 ? event.invoke<&Playback::repeat>(count, *destination).mayBranchTo(*destination).runtimeControlFlow()
                 : event.stop();
    }
    case 0xb9: {
      auto event = cursor.command("Memory Access", SequenceSemantic::State);
      const u8 operation = event.u8("operation");
      const u8 address = event.u8("address");
      const u8 data = event.u8("data");
      Address destination{};
      if (operation >= 6 && operation <= 17) {
        const auto parsed = pointer(event, context.reader, "destination", SemanticOperandRole::JumpTarget);
        if (!parsed) {
          return event.stop();
        }
        destination = *parsed;
        event.mayBranchTo(destination).runtimeControlFlow();
      }
      return event.invoke<&Playback::memAccess>(operation, address, data, destination);
    }
    case 0xba: {
      auto event = cursor.sourceOnly("Priority");
      static_cast<void>(event.u8("priority"));
      return event.ignore();
    }
    case 0xbb: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::tempo>(event.u8("tempo"));
    }
    case 0xbc: {
      auto event = cursor.command("Transpose", SequenceSemantic::State);
      return event.set<&TrackState::transpose>(event.s8("semitones"));
    }
    default:
      return cursor.command("Undefined MP2k Command", SequenceSemantic::End).invoke<&Playback::finish>();
  }
}

[[nodiscard]] TrackProgram decodeTrack(const TrackDecodeScope& tracks, u32 index, u32 start,
                                       std::vector<Diagnostic>* diagnostics) {
  DecodeState state;
  const DecodeContext context{
      .reader = tracks.reader,
      .end = static_cast<u32>(std::min<u64>(tracks.reader.size(), std::numeric_limits<u32>::max())),
      .diagnostics = diagnostics};
  return tracks.reachable(index, start, [&](u32 offset) { return decodeCommand(context, offset, state); });
}

}  // namespace

const SequenceDialect& mp2kSequenceDialect() {
  static const SequenceDialect dialect = makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = std::string(kMp2kSequenceDialectId)},
      .commandDetailKindPrefix = "mp2k",
      .timebase = Timebase{.ppqn = 24},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .commandLimit = kMaxTrackCommands,
              .panLaw = PanLaw::ConstantSum,
              .initialLevel = 0.0,
              .initialExpression = 1.0,
              .initialStereoBalance = StereoBalance{.leftGain = 127.0 / 256.0, .rightGain = 128.0 / 256.0},
              .initialPitchBendRangeSemitones = 2,
              .initialTempoMicrosecondsPerQuarter =
                  static_cast<u32>(std::llround(60000000.0 / (kGbaMixerFrameRate * 60.0 / 24.0))),
          },
  });
  return dialect;
}

SequenceProgram parseMp2kSequenceProgram(ByteReader reader, AssetId id, const Mp2kSong& song,
                                         SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const SequenceDialect& dialect = mp2kSequenceDialect();
  const u32 headerSize = 8 + song.declaredTracks * 4;
  SequenceProgram program = dialect.makeProgram(Address{song.offset});
  program.behavior = dialect.defaultBehavior;
  program.config.driverData.resize(kDriverDataSize);
  for (u32 index = 0; index < kToneCount && reader.has(song.bankOffset + index * 12, 8); ++index) {
    const u32 tone = song.bankOffset + index * 12;
    program.config.driverData[index] = reader.le32(tone);
    program.config.driverData[kToneWaveBase + index] = reader.le32(tone + 4);
    program.config.driverData[kToneEnvelopeBase + index] = reader.le32(tone + 8);
  }
  program.config.driverData[kReverbIndex] = song.reverb;

  std::optional<SourceAnnotationId> header;
  if (sourceMap && reader.has(song.offset, headerSize)) {
    auto annotation = sourceMap->header("MP2k Song Header", reader.range(song.offset, headerSize))
                          .kind("mp2k-song-header")
                          .owner(ObjectRefs::sequence(id))
                          .field("track_count", reader.range(song.offset, 1), song.declaredTracks)
                          .field("block_count", reader.range(song.offset + 1, 1), reader.u8At(song.offset + 1))
                          .field("priority", reader.range(song.offset + 2, 1), reader.u8At(song.offset + 2))
                          .field("reverb", reader.range(song.offset + 3, 1), reader.u8At(song.offset + 3));
    header = annotation.id();
  }

  const TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = static_cast<u32>(std::min<u64>(reader.size(), std::numeric_limits<u32>::max())),
      .maxCommands = kMaxTrackCommands,
      .sequenceAsset = id,
      .parentAnnotation = header,
      .sourceMap = sourceMap,
  };
  for (u32 track = 0; track < song.activeTracks; ++track) {
    const u32 pointerOffset = song.offset + 8 + track * 4;
    const u32 encoded = reader.le32(pointerOffset);
    const u32 start = encoded & 0x01ffffff;
    if (sourceMap) {
      auto pointerAnnotation =
          sourceMap->pointer("Track Pointer", reader.range(pointerOffset, 4), SourceTarget{reader.range(start, 1)})
              .kind("mp2k-track-pointer")
              .derived("track", track);
      if (header) {
        pointerAnnotation.parent(*header);
      }
    }
    program.tracks.push_back(decodeTrack(tracks, track, start, diagnostics));
  }
  return program;
}

}  // namespace vgmtrans::formats::mp2k
