/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/WinkysoftSnes/WinkysoftSnes.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <utility>

namespace vgmtrans::formats::winkysoft_snes {

using namespace core;

namespace {

constexpr u32 kCommandLimit = 1'000'000;
constexpr u32 kDecodeLimit = 65536;
// Keep DSP tuning/modulation independent of the primary wheel consumed by
// MIDI portamento when lowering a legato key change.
constexpr PitchBendLayerId kDspPitchLayer{1};

struct PanStep {
  std::optional<u8> position;
  u8 frames = 0;
  u16 nextAddress = 0;
};

struct Settings {
  Layout layout;
  DriverData data;
  std::array<u16, 128> panPointers{};
  std::map<u16, std::vector<PanStep>> panSequences;
};

struct ProgramState {
  explicit ProgramState(std::shared_ptr<const Settings> settings)
      : settings(std::move(settings)), tempo(this->settings->layout.initialTempo),
        dsp(this->settings->layout.initialDsp) {}

  std::shared_ptr<const Settings> settings;
  u8 tempo;
  u8 tempoScale = 0x80;
  u8 tempoTarget = 0x80;
  u8 tempoStep = 0;
  std::optional<u64> frame;
  std::array<u16, kTrackCount> launchRemainders{};
  std::array<u16, kTrackCount> panCursors{};
  std::array<u8, 128> dsp;
  bool initialized = false;

  [[nodiscard]] bool early() const {
    return settings->layout.version == Version::Early || settings->layout.version == Version::Jojo;
  }

  void advanceTempo() {
    if (tempoStep == 0) {
      tempoScale = tempoTarget;
    } else if (tempoScale < tempoTarget) {
      tempoScale = static_cast<u8>(std::min<unsigned>(tempoTarget, tempoScale + tempoStep + 1));
    } else {
      tempoScale = static_cast<u8>(std::max<int>(tempoTarget, tempoScale - tempoStep - 1));
    }
    tempo = static_cast<u8>((settings->layout.initialTempo * tempoScale * 2) >> 8);
  }

  void advance(u64 tick) {
    if (frame == tick) {
      return;
    }
    frame = tick;
    if (!early()) {
      advanceTempo();
    }
  }
};

struct Repeat {
  Address start;
  u8 iteration = 1;
};

struct TrackState {
  u8 instrument = 0;
  u8 volume = 0;
  u8 velocity = 0;
  u8 pan = 0x40;
  u8 transpose = 0;
  u8 pitchMultiplier = 0x80;
  u8 vibratoDepth = 0;
  u8 vibratoSpeed = 0x20;
  u8 vibratoPhase = 0x80;
  bool vibratoDescending = false;
  s8 pitchStep = 0;
  u16 pitchOffset = 0;
  u16 basePitch = 0;
  u16 noteLength = 0x8000;
  u16 gateLength = 0;
  u16 remaining = 0;
  u16 gateRemaining = 0;
  bool waiting = false;
  bool percussion = false;
  bool sounding = false;
  bool noise = false;
  std::optional<u64> frame;
  u8 frameTempo = 125;
  PerformanceNoteId note;
  double key = 60;
  std::vector<Repeat> repeats;
  std::optional<Address> returnAddress;
  u16 panSequence = 0;
  size_t panIndex = 0;
  u8 panWait = 0;
  double lastPitch = 0;
  double lastLeft = -2;
  double lastRight = -2;
};

struct Playback : SequencePlayback<TrackState> {
  ProgramState& program;

  [[nodiscard]] const Settings& settings() const { return *program.settings; }
  [[nodiscard]] const Instrument& patch() const { return settings().data.instruments[track.instrument]; }

  void emitEcho() {
    const auto& dsp = program.dsp;
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = static_cast<u32>((dsp[0x6c] & 0x20) ? 0 : dsp[0x4d]), .send = 1.0,
        .leftGain = static_cast<s8>(dsp[0x2c]) / 128.0,
        .rightGain = static_cast<s8>(dsp[0x3c]) / 128.0,
        .delayMilliseconds = (dsp[0x7d] & 15) * 16.0,
        .feedback = static_cast<s8>(dsp[0x0d]) / 128.0,
    });
  }

  void beforeCommand() {
    beginFrame();
    if (!program.initialized) {
      program.initialized = true;
      emitEcho();
    }
  }

  void beginFrame() {
    if (track.frame == vm.tick()) {
      return;
    }
    track.frame = vm.tick();
    program.advance(vm.tick());
    track.frameTempo = program.tempo;
    // Early drivers advance the ramp only when channel zero is ready to
    // decode. That channel has already copied the old tempo for this frame.
    if (program.early() && out.sourceTrackNumber() == 0 &&
        (!track.waiting || static_cast<u16>(track.remaining - track.frameTempo) < track.frameTempo)) {
      program.advanceTempo();
    }
  }

  void beginSection(bool first) {
    if (!first && track.sounding) {
      out.setNoteEnd(track.note, vm.tick());
    }
    track.transpose = 0;
    track.noteLength = 0x8000;
    track.gateLength = 0;
    track.remaining = program.launchRemainders[out.sourceTrackNumber()];
    track.waiting = false;
    track.frame.reset();
    track.percussion = false;
    track.sounding = false;
    track.note = {};
    track.vibratoSpeed = 0x20;
    track.panWait = 0;
    track.repeats.clear();
    track.returnAddress.reset();
  }

  void emitLevel() {
    // Preserve the driver's two truncating multiplies before stereo mixing.
    const u32 gain = ((track.velocity * track.volume >> 8) * settings().layout.initialVolume) >> 8;
    u8 left;
    u8 right;
    if (program.early()) {
      const u8 pan = settings().layout.stereo ? track.pan : 0x40;
      const u32 side = gain * pan >> 8;
      left = static_cast<u8>((gain >> 1) + side);
      right = static_cast<u8>(gain - side);
    } else {
      const u8 pan = settings().layout.stereo ? std::min<u8>(track.pan >> 3, 15) : 8;
      left = static_cast<u8>(gain * settings().data.panLeft[pan] >> 8);
      right = static_cast<u8>(gain * settings().data.panRight[pan] >> 8);
    }
    const double l = static_cast<s8>(left) / 128.0;
    const double r = static_cast<s8>(right) / 128.0;
    if (l != track.lastLeft || r != track.lastRight) {
      out.stereoBalance(l, r);
      track.lastLeft = l;
      track.lastRight = r;
    }
  }

  [[nodiscard]] u16 dspPitch() const {
    u8 multiplier = track.pitchMultiplier;
    s8 vibrato = 0;
    if (track.vibratoDepth != 0) {
      if (program.early()) {
        multiplier = static_cast<u8>(0x80 + ((track.vibratoDepth * track.vibratoPhase) >> 8));
      } else {
        vibrato = static_cast<s8>(((track.vibratoDepth * 2 * track.vibratoPhase) >> 8) - track.vibratoDepth);
      }
    }
    const u32 high = (track.basePitch >> 8) * multiplier * 2;
    const u32 product = (track.basePitch & 255) * static_cast<u8>(multiplier + vibrato);
    const u32 low = program.early() ? (product >> 8) * 2 : (product * 2) >> 8;
    return static_cast<u16>((high + low + track.pitchOffset) & 0x3fff);
  }

  [[nodiscard]] double pitchKey(u16 pitch) const {
    return unityKey(patch(), settings().data) + 12.0 * std::log2(std::max<u16>(1, pitch) / 4096.0);
  }

  void emitPitch() {
    if (!track.sounding || track.noise) {
      return;
    }
    const double bend = pitchKey(dspPitch()) - track.key;
    if (bend != track.lastPitch) {
      out.pitchBend(bend, kDspPitchLayer);
      track.lastPitch = bend;
    }
  }

  void advancePan() {
    if (track.panWait == 0 || --track.panWait != 0) {
      return;
    }
    const auto found = settings().panSequences.find(track.panSequence);
    if (found == settings().panSequences.end() || track.panIndex >= found->second.size()) {
      return;
    }
    const PanStep& step = found->second[track.panIndex++];
    program.panCursors[out.sourceTrackNumber()] = step.nextAddress;
    if (step.position) {
      track.pan = *step.position;
    }
    track.panWait = step.frames;
  }

  void tick() {
    beginFrame();
    advancePan();
    if (track.sounding) {
      if (track.gateRemaining < track.frameTempo) {
        out.setNoteEnd(track.note, vm.tick());
        track.sounding = false;
      } else {
        track.gateRemaining = static_cast<u16>(track.gateRemaining - track.frameTempo);
        out.setNoteEnd(track.note, vm.tick() + 1);
      }
    }
    if (track.vibratoDepth != 0) {
      if (program.early()) {
        // Vibrato writes the multiplier itself. Disabling it retains the
        // previous frame's multiplier until a pitch command replaces it.
        track.pitchMultiplier = static_cast<u8>(0x80 + ((track.vibratoDepth * track.vibratoPhase) >> 8));
      }
      if (track.vibratoDescending) {
        if (track.vibratoPhase < track.vibratoSpeed) {
          track.vibratoPhase = program.early() ? 0 : 1;
          track.vibratoDescending = false;
        } else {
          track.vibratoPhase = static_cast<u8>(track.vibratoPhase - track.vibratoSpeed);
        }
      } else {
        // Late drivers retain the carry from centering the vibrato sample.
        const u8 carry = !program.early() &&
            ((track.vibratoDepth * 2 * track.vibratoPhase) >> 8) >= track.vibratoDepth ? 1 : 0;
        const u32 next = track.vibratoPhase + track.vibratoSpeed + carry;
        if (next > 255) {
          track.vibratoPhase = 255;
          track.vibratoDescending = true;
        } else {
          track.vibratoPhase = static_cast<u8>(next);
        }
      }
    } else {
      track.vibratoPhase = 0x80;
      track.vibratoDescending = false;
    }
    track.pitchOffset = static_cast<u16>(track.pitchOffset + track.pitchStep);
    emitLevel();
    emitPitch();
  }

  [[nodiscard]] Effects wait(u32 duration) {
    if (track.waiting) {
      track.remaining = static_cast<u16>(track.remaining - track.frameTempo);
    } else {
      track.remaining = static_cast<u16>(track.remaining + duration);
    }
    if (track.frameTempo == 0) {
      vm.diagnostic(Diagnostic{.severity = Severity::Warning, .message = "WinkysoftSnes tempo stopped the sequencer"});
      return vm.end();
    }
    track.waiting = track.remaining >= track.frameTempo;
    if (track.waiting) {
      auto effect = vm.finiteBranch(Address{vm.sourceRange().offset});
      effect.advanceTicks = 1;
      return effect;
    }
    return vm.fallthrough();
  }

  void instrument(u8 index, bool drum = false) {
    track.instrument = index;
    const Instrument& value = patch();
    track.basePitch = value.pitch;
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = index});
    if (drum) {
      track.volume = value.volume;
    }
    if (track.panWait == 0) {
      track.pan = value.pan;
    }
    track.transpose = value.transpose;
    track.vibratoDepth = 0;
    track.pitchOffset = 0;
    track.pitchStep = 0;
    track.pitchMultiplier = 0x80;
    emitLevel();
    emitPitch();
  }

  [[nodiscard]] u16 melodicPitch(u8 key) const {
    // Key and fraction form an 8.8 number. Negative low-key corrections
    // borrow from the semitone byte before indexing the pitch table.
    const u8 correction = key >= 0x34 ? static_cast<u8>(key - 0x34)
                                     : (key < 0x13 ? static_cast<u8>((key - 0x13) * 2) : 0);
    const u8 correctedKey = key < 0x13 ? static_cast<u8>(key - 1) : key;
    const u8 doubled = static_cast<u8>(correctedKey * 2);
    const u8 index = static_cast<u8>((doubled % 24) / 2);
    const auto& pitches = settings().data.pitches;
    u16 base = static_cast<u16>(pitches[index] +
        (((pitches[index + 1] - pitches[index]) & 0xff) * correction >> 8));
    base = static_cast<u16>(base * 2);
    // The eight-bit octave counter runs until six; keys beyond the useful
    // range wrap and shift to silence rather than indexing past the table.
    const u8 shifts = static_cast<u8>(6 - doubled / 24);
    base = shifts >= 16 ? 0 : static_cast<u16>(base >> shifts);
    return static_cast<u16>((static_cast<u32>(base) * patch().pitch) >> 8);
  }

  [[nodiscard]] Effects note(u8 key, bool advances, u8 suffix, u8 value, bool noise) {
    if (track.waiting) {
      return wait(0);
    }
    const bool retrigger = track.gateLength < track.noteLength;
    if (track.percussion && !noise) {
      instrument(key, true);
      track.basePitch = patch().pitch;
    } else if (!noise) {
      track.basePitch = melodicPitch(static_cast<u8>(key + track.transpose));
    }
    if (retrigger) {
      track.vibratoPhase = 0x80;
      track.vibratoDescending = false;
      track.pitchOffset = static_cast<u16>(track.pitchStep);
    }
    if (track.sounding) {
      out.setNoteEnd(track.note, vm.tick());
    }
    if (noise) {
      program.dsp[0x3d] = static_cast<u8>(1u << (out.sourceTrackNumber() < 8 ? out.sourceTrackNumber()
                                                                                  : out.sourceTrackNumber() - 2));
      program.dsp[0x6c] = static_cast<u8>((program.dsp[0x6c] & 0xe0) | key);
    }
    const u32 voice = out.sourceTrackNumber() < 8 ? out.sourceTrackNumber() : out.sourceTrackNumber() - 2;
    track.noise = (program.dsp[0x3d] & (1u << voice)) != 0;
    const double soundingKey = track.noise ? 60.0 : pitchKey(dspPitch());
    // MIDI keys are integral. Keep the fractional DSP pitch in the bend so
    // later multipliers, drift, and vibrato retain the same tuning anchor.
    const double noteKey = std::clamp(std::round(soundingKey), 0.0, 127.0);
    const double bend = soundingKey - noteKey;
    NotePerformanceEvent event{.key = noteKey, .durationTicks = 1,
                                .restartsEnvelope = retrigger, .restartsLfoPhase = retrigger};
    if (track.noise) {
      event.instrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain),
          .key = noiseInstrumentKey(track.instrument, program.dsp[0x6c] & 31)};
    }
    out.pitchBend(bend, kDspPitchLayer);
    track.note = !retrigger && track.note.valid() ? out.continueVoice(track.note, std::move(event))
                                                 : out.note(std::move(event));
    track.key = noteKey;
    track.lastPitch = bend;
    track.sounding = true;
    // Inline velocity/gate controls apply after the key-on decision.
    if (suffix == 0x7d) {
      track.velocity = value;
    } else if (suffix == 0x7e) {
      track.gateLength = static_cast<u16>(value * 125);
    }
    track.gateRemaining = track.gateLength;
    emitLevel();
    return advances ? wait(track.noteLength) : vm.fallthrough();
  }

  [[nodiscard]] Effects packed(u8 velocity, u8 gate, u8 duration) {
    if (track.waiting) {
      return wait(0);
    }
    track.velocity = velocity;
    track.gateLength = static_cast<u16>(gate * 125);
    track.gateRemaining = track.gateLength;
    emitLevel();
    if (duration != 0) {
      track.noteLength = static_cast<u16>(duration * 125);
      return wait(track.noteLength);
    }
    return vm.fallthrough();
  }

  [[nodiscard]] Effects duration(u8 length, bool rest) {
    if (track.waiting) {
      return wait(0);
    }
    if (rest) {
      track.gateLength &= 0xff;
      track.gateRemaining &= 0xff;
    }
    track.noteLength = static_cast<u16>(length * 125);
    return wait(track.noteLength);
  }

  void velocity(u8 value) { track.velocity = value; emitLevel(); }
  void gate(u8 value) { track.gateLength = static_cast<u16>(value * 125); track.gateRemaining = track.gateLength; }

  [[nodiscard]] Effects control(u8 kind, u8 value, std::optional<u8> duration) {
    if (track.waiting) {
      return wait(0);
    }
    switch (kind) {
      case 0x70: track.pitchMultiplier = static_cast<u8>(value * 2); break;
      case 0x71: track.vibratoDepth = value & 0x7f; break;
      case 0x72: track.volume = static_cast<u8>((static_cast<u8>(value * 2) * patch().volume) >> 8); break;
    }
    emitLevel();
    emitPitch();
    if (duration) {
      if (*duration == 0) {
        return vm.fallthrough();
      }
      track.noteLength = static_cast<u16>(*duration * 125);
    }
    return wait(track.noteLength);
  }

  void tempo(u8 target, u8 step) { program.tempoTarget = target; program.tempoStep = step; }

  void noiseOff() { program.dsp[0x3d] = 0; }

  void dsp(u8 reg, u8 value) {
    reg &= 0x7f;
    program.dsp[reg] = value;
    if (reg == 0x0c || reg == 0x1c) {
      out.masterLevel((std::abs(static_cast<s8>(program.dsp[0x0c])) +
                       std::abs(static_cast<s8>(program.dsp[0x1c]))) / 256.0);
    } else if (reg == 0x2c || reg == 0x3c || reg == 0x4d || reg == 0x6c || reg == 0x7d || reg == 0x0d) {
      emitEcho();
    }
  }

  void echo(u8 mask, u8 feedback, u8 left, u8 right) {
    program.dsp[0x4d] = mask;
    program.dsp[0x0d] = feedback;
    program.dsp[0x2c] = left;
    program.dsp[0x3c] = right;
    emitEcho();
  }

  void pan(u8 value) { if (track.panWait == 0) { track.pan = value; emitLevel(); } }
  void slide(s8 value) { track.pitchStep = value; track.pitchOffset = static_cast<u16>(value); emitPitch(); }

  void panSequence(u16 address) {
    track.panSequence = address;
    program.panCursors[out.sourceTrackNumber()] = address;
    track.panIndex = 0;
    track.panWait = 1;
    advancePan();
    emitLevel();
  }

  void panPreset(u8 index) {
    if (index >= 0x80) {
      const u8 action = index & 6;
      switch (action) {
        case 0: index = 0; break;
        case 2: index = static_cast<u8>(program.panCursors[out.sourceTrackNumber()]); break;
        case 4:
          index = static_cast<u8>(program.panCursors[8]);
          program.panCursors[9] = static_cast<u16>((program.panCursors[9] & 0xff00) | index);
          break;
        default: track.panWait = 0; return;
      }
      if (action != 0 && index == 0) {
        track.panWait = 0;
        return;
      }
    }
    panSequence(settings().panPointers[index & 0x7f]);
  }

  [[nodiscard]] Effects startTrack(u8 channel, Address address) {
    if (channel >= kTrackCount) {
      vm.diagnostic(Diagnostic{.severity = Severity::Warning, .message = "WinkysoftSnes invalid logical channel"});
      return vm.end();
    }
    program.launchRemainders[channel] = track.remaining;
    // The SPC visits channels in ascending order once per frame. A lower
    // channel (or this same channel) cannot run again until the next frame.
    return vm.startStream(channel, address, channel <= out.sourceTrackNumber() ? 1 : 0);
  }

  [[nodiscard]] Effects beginRepeat(Address start) {
    if (track.repeats.size() == 8) {
      vm.diagnostic(Diagnostic{.severity = Severity::Warning, .message = "WinkysoftSnes repeat stack overflow"});
      return vm.end();
    }
    track.repeats.push_back(Repeat{start});
    return vm.fallthrough();
  }

  [[nodiscard]] Effects repeat(u8 count) {
    if (track.repeats.empty()) {
      vm.diagnostic(Diagnostic{.severity = Severity::Warning, .message = "WinkysoftSnes repeat without a start"});
      return vm.end();
    }
    Repeat& repeat = track.repeats.back();
    if (count < repeat.iteration) {
      if (settings().layout.resetTempoAtLoop) {
        track.remaining = 0;
        program.tempoStep = 0;
        program.tempoTarget = 0x80;
        program.tempo = settings().layout.initialTempo;
      }
      return vm.declaredLoop(repeat.start);
    }
    if (count == repeat.iteration) {
      track.repeats.pop_back();
      return vm.fallthrough();
    }
    ++repeat.iteration;
    return vm.finiteBranch(repeat.start);
  }

  [[nodiscard]] Effects call(Address target, Address continuation) {
    track.returnAddress = continuation;
    return vm.finiteBranch(target);
  }

  [[nodiscard]] Effects return_() {
    if (!track.returnAddress) {
      vm.diagnostic(Diagnostic{.severity = Severity::Warning, .message = "WinkysoftSnes return without a call"});
      return vm.end();
    }
    return vm.finiteBranch(*track.returnAddress);
  }

  [[nodiscard]] Effects end() {
    if (track.sounding) {
      out.setNoteEnd(track.note, vm.tick());
      track.sounding = false;
    }
    return vm.end();
  }
};

using Cursor = CompilerCursor<Playback>;

struct Walk {
  u8 channel = 0;
  u32 offset = 0;
  u8 implicit = 0;
  u32 returnAddress = 0;
  std::vector<u32> loops;
  bool percussion = false;
  friend auto operator<=>(const Walk&, const Walk&) = default;
};

void capturePanSequence(ByteReader reader, Settings& settings, u16 address) {
  if (settings.panSequences.contains(address)) {
    return;
  }
  auto& steps = settings.panSequences[address];
  std::optional<u8> position;
  for (u32 offset = address; reader.has(offset, 1) && offset - address < 4096; ++offset) {
    const u8 value = reader.u8At(offset);
    if (value >= 0xf0) {
      position = static_cast<u8>((value & 15) * 8);
    } else {
      steps.push_back(PanStep{position, value, static_cast<u16>(offset + 1)});
      position.reset();
      if (value == 0) {
        break;
      }
    }
  }
}

DecodedBytecodeCommand decodeCommand(ByteReader reader, Walk& walk, Settings& settings,
                                     std::set<u8>& instruments, std::set<u8>& noiseRates,
                                     std::vector<Diagnostic>* diagnostics,
                                     std::deque<Walk>& pending) {
  const Layout& layout = settings.layout;
  Cursor cursor(reader, walk.offset, kAramSize, "winkysoft-snes", diagnostics);
  if (!cursor.ok()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (walk.implicit != 0 || (opcode >= 0x70 && opcode <= 0x72)) {
    const u8 kind = walk.implicit != 0 ? walk.implicit : opcode;
    const bool implicit = walk.implicit != 0;
    auto event = cursor.command(kind == 0x70 ? "Pitch Multiplier" : kind == 0x71 ? "Vibrato Depth" : "Volume",
                                kind == 0x72 ? SequenceSemantic::Level : SequenceSemantic::Pitch);
    const u8 value = implicit ? cursor.opcodeValue("value", opcode) : cursor.u8("value");
    const std::optional<u8> duration = !implicit || value < 0x80 ? std::optional{cursor.u8("length")} : std::nullopt;
    walk.implicit = (value & 0x80) ? kind : 0;
    return event.invoke<&Playback::control>({kind, value, duration});
  }
  if (opcode >= 0x80) {
    return cursor.command("Velocity / Gate / Length", SequenceSemantic::State)
        .invoke<&Playback::packed>({static_cast<u8>(opcode & 0x7f), cursor.u8("gate"), cursor.u8("length")});
  }
  const u8 command = reader.u8At(layout.commandMap + opcode);
  const auto note = [&](u8 key, bool noise) -> DecodedBytecodeCommand {
    auto event = cursor.command(noise ? "Noise Note" : "Note", SequenceSemantic::Note);
    const u32 nextOffset = static_cast<u32>(cursor.nextAddress().value);
    if (!reader.has(nextOffset, 1)) {
      cursor.u8("note lookahead");
      return cursor.truncated();
    }
    const u8 next = reader.u8At(nextOffset);
    u8 suffix = 0;
    u8 value = 0;
    if (next == 0x7d || next == 0x7e) {
      suffix = cursor.u8("inline control", SourceValueDisplay::Hex);
      value = cursor.u8(next == 0x7d ? "velocity" : "gate");
    }
    if (walk.percussion && !noise) {
      instruments.insert(key);
    }
    return event.invoke<&Playback::note>({key, next < 0x7f, suffix, value, noise});
  };
  if (command == 2) {
    cursor.opcodeValue("key / percussion instrument", opcode);
    return note(opcode, false);
  }
  if (command == 0) {
    return cursor.ignored("Unused Command", 1);
  }
  switch (opcode) {
    case 0x67: {
      auto event = cursor.command("Pan Sequence", SequenceSemantic::Pan);
      const u16 address = cursor.u16le("address", SourceValueDisplay::Address);
      if (cursor.ok()) { capturePanSequence(reader, settings, address); }
      return event.invoke<&Playback::panSequence>({address});
    }
    case 0x68: {
      auto event = cursor.command("Pan Sequence Preset", SequenceSemantic::Pan);
      const u8 index = cursor.u8("index");
      if (index < 0x80) {
        capturePanSequence(reader, settings, settings.panPointers[index]);
      } else {
        for (const u16 address : settings.panPointers) { capturePanSequence(reader, settings, address); }
      }
      return event.invoke<&Playback::panPreset>({index});
    }
    case 0x69:
      return cursor.command("DSP Write", SequenceSemantic::State)
          .invoke<&Playback::dsp>({cursor.u8("register", SourceValueDisplay::Hex), cursor.u8("value", SourceValueDisplay::Hex)});
    case 0x6a:
      return cursor.command("Vibrato Speed", SequenceSemantic::Modulation)
          .set<&TrackState::vibratoSpeed>(cursor.u8("step"));
    case 0x6b: {
      const u8 clock = cursor.u8("noise clock");
      if (clock < 0x20) {
        noiseRates.insert(clock);
        return note(clock, true);
      }
      return cursor.command("Noise Off", SequenceSemantic::State).invoke<&Playback::noiseOff>();
    }
    case 0x6c:
      if (layout.version == Version::Early || layout.version == Version::Jojo) {
        cursor.u8("port value", SourceValueDisplay::Hex);
        return cursor.sourceOnly("CPU Port Write", "cpu-port-write");
      }
      return cursor.command("Pitch Slide", SequenceSemantic::Pitch)
          .invoke<&Playback::slide>({cursor.s8("pitch step per frame")});
    case 0x6d:
      if (layout.version == Version::Early || layout.version == Version::Jojo) {
        auto event = cursor.command("CPU Conditional Branch", SequenceSemantic::Jump);
        const u8 condition = cursor.u8("condition");
        const Address target{cursor.u16le("destination", SourceValueDisplay::Address, SemanticOperandRole::JumpTarget)};
        return layout.hostCondition != condition ? event.finiteBranch(target) : event;
      }
      return cursor.command("Echo", SequenceSemantic::State)
          .invoke<&Playback::echo>({cursor.u8("voice mask", SourceValueDisplay::Hex), cursor.u8("feedback"),
                                    cursor.u8("left volume"), cursor.u8("right volume")});
    case 0x6e: {
      auto event = cursor.command("Start Track", SequenceSemantic::State);
      const u8 channel = cursor.u8("channel");
      const Address target{cursor.u16le("destination", SourceValueDisplay::Address)};
      if (cursor.ok() && channel < kTrackCount) {
        pending.push_back(Walk{.channel = channel, .offset = static_cast<u32>(target.value)});
      }
      return event.invoke<&Playback::startTrack>({channel, target});
    }
    case 0x6f:
      walk.percussion = !walk.percussion;
      return cursor.command("Toggle Percussion", SequenceSemantic::State).toggle<&TrackState::percussion>();
    case 0x73:
      return cursor.command("Pan", SequenceSemantic::Pan).invoke<&Playback::pan>({cursor.u8("position")});
    case 0x74: {
      if (walk.loops.size() >= 8) { return cursor.unsupported("Repeat Stack Overflow").stop(); }
      const Address start{walk.offset + 1};
      walk.loops.push_back(static_cast<u32>(start.value));
      return cursor.command("Repeat Begin", SequenceSemantic::Repeat).invoke<&Playback::beginRepeat>({start});
    }
    case 0x75: {
      auto event = cursor.command("Repeat End", SequenceSemantic::Repeat);
      const u8 count = cursor.u8("count (zero = loop)");
      if (walk.loops.empty()) { return cursor.unsupported("Repeat Without Start").stop(); }
      const Address target{walk.loops.back()};
      if (count != 1) {
        Walk repeated = walk;
        repeated.offset = static_cast<u32>(target.value);
        pending.push_back(std::move(repeated));
      }
      walk.loops.pop_back();
      cursor.derived("destination", target, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      // Revisit with the block's resulting percussion/controller state so
      // instruments used on later iterations are included in the bank.
      return count == 0 ? event.invoke<&Playback::repeat>({count}).declaredLoop(target)
                        : event.invoke<&Playback::repeat>({count});
    }
    case 0x76: {
      if (layout.version == Version::Early) { return cursor.unsupported("Unimplemented Driver Call").stop(); }
      auto event = cursor.command("Call", SequenceSemantic::Call);
      const Address target{cursor.u16le("destination", SourceValueDisplay::Address, SemanticOperandRole::CallTarget)};
      walk.returnAddress = static_cast<u32>(cursor.nextAddress().value);
      return event.invoke<&Playback::call>({target, Address{walk.returnAddress}}).jump(target);
    }
    case 0x77:
      return cursor.command("Return", SequenceSemantic::Return).invoke<&Playback::return_>().return_();
    case 0x78:
      return cursor.command("End", SequenceSemantic::End).invoke<&Playback::end>().end();
    case 0x79:
      return cursor.command("Tempo Scale / Ramp", SequenceSemantic::Tempo)
          .invoke<&Playback::tempo>({cursor.u8("target (unity = 128)"), cursor.u8("step per frame")});
    case 0x7a:
      return cursor.command("Transpose", SequenceSemantic::Pitch).set<&TrackState::transpose>(cursor.u8("semitones"));
    case 0x7b: {
      auto event = cursor.command("Instrument", SequenceSemantic::Program);
      const u8 index = cursor.u8("instrument", SemanticOperandRole::InstrumentProgram);
      instruments.insert(index);
      return event.invoke<&Playback::instrument>({index, false});
    }
    case 0x7c:
    case 0x7f:
      return cursor.command(opcode == 0x7c ? "Rest" : "Length / Wait", SequenceSemantic::Rest)
          .invoke<&Playback::duration>({cursor.u8("length"), opcode == 0x7c});
    case 0x7d:
      return cursor.command("Velocity", SequenceSemantic::Level).invoke<&Playback::velocity>({cursor.u8("value")});
    case 0x7e:
      return cursor.command("Gate Length", SequenceSemantic::State).invoke<&Playback::gate>({cursor.u8("length")});
    default:
      return cursor.unsupported("Unknown Command").stop();
  }
}

}  // namespace

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, const DriverData& data, AssetId id,
                              SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  auto settings = std::make_shared<Settings>(Settings{.layout = layout, .data = data});
  if (layout.version == Version::Extended && reader.has(layout.panSequenceTable, 256)) {
    for (u32 i = 0; i < 128; ++i) { settings->panPointers[i] = reader.le16(layout.panSequenceTable + i * 2); }
  }
  const SequenceProgramConfig config{
      .commandKindPrefix = "winkysoft-snes", .timebase = Timebase{.ppqn = kPpqn},
      .behavior = {.commandLimit = kCommandLimit, .inferLoopsFromRepeatedState = false,
                   .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
                   .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
                   .initialLevel = 1.0,
                   .initialMasterLevel = (std::abs(static_cast<s8>(layout.initialDsp[0x0c])) +
                                          std::abs(static_cast<s8>(layout.initialDsp[0x1c]))) / 256.0,
                   .initialReverbSend = 0.0, .initialPitchBendRangeSemitones = 24,
                   .initialTempoMicrosecondsPerQuarter = layout.frameMicroseconds * kPpqn},
  };
  SequenceDecodeSession sequence(reader, config, id, reader.range(layout.songHeader, layout.songHeaderSize),
                                 sourceMap, kDecodeLimit, kAramSize);
  const u32 tempoOffset = layout.songHeader + (layout.songHeaderSize == 6 ? 1 : 0);
  sequence.header().field("initial tempo step", reader.range(tempoOffset, 1), layout.initialTempo)
      .description("Lengths are multiplied by 125; the tempo step is subtracted once per hardware timer frame");
  sequence.header().field("initial volume", reader.range(tempoOffset + 1, 1), layout.initialVolume);

  std::vector<TrackDecodeSession> tracks;
  for (u32 channel = 0; channel < kTrackCount; ++channel) {
    tracks.push_back(sequence.trackScope().begin(channel, layout.songStart));
  }
  std::deque<Walk> pending{{.channel = layout.firstTrack, .offset = layout.songStart}};
  std::set<Walk> visited;
  std::map<std::pair<u8, u32>, u8> modes;
  std::set<u8> instruments{0};
  std::set<u8> noiseRates;
  while (!pending.empty() && visited.size() < kDecodeLimit) {
    Walk walk = std::move(pending.front());
    pending.pop_front();
    if (!reader.has(walk.offset, 1) || walk.offset >= kAramSize || !visited.insert(walk).second) { continue; }
    const u32 begin = walk.offset;
    const auto [mode, inserted] = modes.emplace(std::pair{walk.channel, begin}, walk.implicit);
    if (!inserted && mode->second != walk.implicit) {
      if (diagnostics) {
        diagnostics->push_back(Diagnostic{.severity = Severity::Warning,
            .message = "WinkysoftSnes byte reached with conflicting repeated-controller modes",
            .range = reader.range(begin, 1)});
      }
      continue;
    }
    auto decoded = decodeCommand(reader, walk, *settings, instruments, noiseRates, diagnostics, pending);
    const auto& command = tracks[walk.channel].findOrAppend(std::move(decoded), begin);
    const auto transition = command.flow.defaultTransition;
    if (const auto target = command.flow.defaultDestination()) {
      if (transition.jumpSemantics != JumpSemantics::DeclaredLoop) {
        walk.offset = static_cast<u32>(target->value);
        pending.push_back(walk);
      }
    } else if (transition.kind == CommandTransitionKind::Return) {
      if (walk.returnAddress != 0) {
        walk.offset = walk.returnAddress;
        pending.push_back(walk);
      }
    } else if (command.flow.discoveryContinuation()) {
      walk.offset = static_cast<u32>(command.flow.continuation.value);
      pending.push_back(walk);
    }
  }
  if (!pending.empty() && diagnostics) {
    diagnostics->push_back(Diagnostic{.severity = Severity::Warning, .message = "WinkysoftSnes decode limit reached"});
  }
  for (u32 channel = 0; channel < kTrackCount; ++channel) {
    TrackProgram track = tracks[channel].finish();
    track.streams.front().initiallyActive = channel == layout.firstTrack;
    if (channel != layout.firstTrack && !track.commands.empty()) {
      track.startAddress = track.commands.front().address;
    }
    sequence.addTrack(std::move(track));
  }
  return SequenceParse{sequence.finish(makeCompiledRuntime<Playback, ProgramState>(
                           std::shared_ptr<const Settings>(std::move(settings)))), std::move(instruments),
                       std::move(noiseRates)};
}

}  // namespace vgmtrans::formats::winkysoft_snes
