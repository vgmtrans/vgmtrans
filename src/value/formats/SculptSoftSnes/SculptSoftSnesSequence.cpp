/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SculptSoftSnes/SculptSoftSnes.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <utility>

namespace vgmtrans::formats::sculpt_soft_snes {

using namespace core;

namespace {

constexpr std::array<u8, 4> kCurveFlags{1, 4, 8, 2};  // Gain, pitch, pan, sample.

struct Phrase {
  Address start;
  Address end;
  u8 count = 0;
  u16 transpose = 0;
  u16 volumeScale = 0x100;
  std::vector<u8> instruments;
};

struct PhraseFrame {
  Phrase phrase;
  u8 iteration = 0;
  u8 instrumentIndex = 0;
  u8 savedVolume = 0;
};

struct RuntimeConfig {
  std::shared_ptr<const DriverData> data;
  u32 frameMicroseconds;
  Revision revision;
};

struct ProgramState {
  u8 tempo = 0;
  u8 phase = 0xff;
  u16 gateScale = 0x100;
  u64 frame = 0;
  bool sequenceTick = true;
  u8 echoMask = 0;
  std::optional<std::array<u8, 12>> echo;
  std::optional<u16> nextGate;
  bool legato = false;

  void advance(u64 tick) {
    if (tick == frame) {
      return;
    }
    frame = tick;
    const unsigned sum = phase + tempo;
    sequenceTick = tempo == 0 || sum > 255;
    phase = static_cast<u8>(sum);
  }
};

struct TrackState {
  TrackState(TrackStateContext context, const RuntimeConfig& config)
      : data(config.data), frameSeconds(config.frameMicroseconds / 1000000.0), revision(config.revision),
        number(context.sourceTrackNumber) {}

  std::shared_ptr<const DriverData> data;
  double frameSeconds;
  Revision revision;
  u32 number;
  u16 pitch = 0x21c;
  u16 finePitch = 0;
  u16 transpose = 0;
  u8 volume = 0;
  u8 program = 0;
  u8 sample = 0;
  u8 voiceVolume = 0;
  u8 fixedPan = 50;
  u8 fixedSample = 0;
  Patch patch;
  std::array<CurvePlayer, 4> curves;
  bool alternatePan = false;
  std::vector<PhraseFrame> phrases;
  u16 remaining = 0;
  std::optional<PerformanceNoteId> note;
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

[[nodiscard]] u8 scaledVolume(u8 volume, u16 scale) {
  return static_cast<u8>(std::min<u32>(127, (u32(volume) * scale + 128) >> 8));
}

struct Playback : SequencePlayback<TrackState> {
  ProgramState& program;

  void beforeCommand() { program.advance(vm.tick()); }

  void warning(std::string message) {
    vm.diagnostic(Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = vm.sourceRange()});
  }

  [[nodiscard]] double key() const {
    if ((track.data->sampleFlags[track.sample] & 0x80) != 0) {
      return 72.0;
    }
    u16 pitch = track.voicePitch;
    if ((track.patch.flags & 4) != 0) {
      pitch = static_cast<u16>(pitch + track.curves[1].value - 0x4b0);
    }
    pitch = static_cast<u16>(pitch + track.data->sampleTuning[track.sample]);
    if ((track.patch.flags & 0x40) != 0) {
      return 72.0 + 12.0 * std::log2(std::max(1u, unsigned(pitch & 0x3fff)) / 4096.0);
    }
    const unsigned topOctave = track.revision == Revision::Extended ? 10 : 4;
    if (track.revision == Revision::Extended) {
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
    return track.data->pitchBaseKey - 12.0 * (topOctave - octave) + (pitch % 240) / 20.0;
  }

  void selectSample(u8 sample) {
    track.sample = sample;
    if (track.emittedSample != sample) {
      out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = sample});
      track.emittedSample = sample;
    }
  }

  void emitEcho() {
    const u8 bit = static_cast<u8>(1u << track.number);
    program.echoMask &= static_cast<u8>(~bit);
    if ((track.patch.flags & 0x10) != 0) {
      if (const auto& preset = track.data->echoes[track.patch.echo]) {
        program.echo = preset;
        program.echoMask |= bit;
      } else {
        warning("Invalid SculptSoftSnes echo preset");
      }
    }
    if (!program.echo) {
      out.reverb(ReverbPerformanceEvent{.voiceMask = program.echoMask, .send = 0.0});
      return;
    }
    const auto& echo = program.echo;
    const auto gain = [](u8 value) { return std::min<u8>(value, 75) / 128.0; };
    const double left = gain((*echo)[1]);
    const double right = gain((*echo)[2]);
    out.reverb(ReverbPerformanceEvent{.voiceMask = program.echoMask,
                                      .send = std::max(std::abs(left), std::abs(right)),
                                      .leftGain = left,
                                      .rightGain = right,
                                      .delayMilliseconds = ((*echo)[0] & 15) * 16.0,
                                      .feedback = static_cast<s8>((*echo)[3]) / 128.0});
  }

  void emitVoice() {
    const u8 pan = static_cast<u8>((track.patch.flags & 8) ? track.curves[2].value : track.fixedPan);
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

  void attack(u16 duration, bool legato) {
    if (!track.data->patches[track.program]) {
      warning(fmt::format("Invalid SculptSoftSnes instrument {}", track.program));
      return;
    }
    track.patch = *track.data->patches[track.program];
    track.voiceVolume = std::min<u8>(track.volume, 127);
    const std::array<u8, 4> indices{track.patch.gain, track.patch.pitch, track.patch.pan, track.patch.sample};
    const auto scaledByte = [&](u8 value) { return (u32(value) * program.gateScale + 128) >> 8; };
    // The extended driver scales and rounds the two duration bytes separately.
    const u16 gate = static_cast<u16>(scaledByte(static_cast<u8>(duration)) +
                                      (track.revision == Revision::Extended ? scaledByte(duration >> 8) << 8 : 0));
    for (u8 lane = 0; lane < (legato ? 2 : 4); ++lane) {
      if ((track.patch.flags & kCurveFlags[lane]) == 0) {
        continue;
      }
      const auto& curve = track.data->curves[lane][indices[lane]];
      if (!curve) {
        warning(fmt::format("Invalid SculptSoftSnes envelope: patch {}, lane {}, index {}", track.program, lane,
                            indices[lane]));
        track.patch.flags &= static_cast<u8>(~kCurveFlags[lane]);
      } else {
        track.curves[lane].start(*curve, gate);
        // $0856 runs sequence commands before the frame's envelope updates.
        track.curves[lane].tick();
      }
    }
    if (!legato) {
      track.alternatePan = (track.patch.flags & 8) != 0 && track.curves[2].curve->alternate && !track.alternatePan;
      if ((track.patch.flags & 8) == 0) {
        track.fixedPan = track.patch.pan;
      }
      if ((track.patch.flags & 2) == 0) {
        track.fixedSample = track.patch.sample;
      }
    }
    const u8 sample = static_cast<u8>((track.patch.flags & 2) ? track.curves[3].value : track.fixedSample);
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
    if (legato) {
      updateGainRegister();
    } else {
      track.gainRegister = static_cast<u8>((track.patch.flags & 1) ? track.curves[0].value : track.patch.gain);
      track.envelope = snesDspGainEnvelopeValue(track.gainRegister, 0, 0.0);
      emitEcho();
    }
    emitVoice();
    if (retrigger) {
      track.note = out.note(track.noteKey, 1.0, 1);
    }
    track.sounding = track.sounding || retrigger;
  }

  void tick() {
    program.advance(vm.tick());
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
        track.curves[lane].tick();
      }
    }
    if ((track.patch.flags & 2) != 0 && track.curves[3].value != track.sample) {
      out.setNoteEnd(*track.note, vm.tick());
      selectSample(static_cast<u8>(track.curves[3].value));
      track.noteKey = std::clamp(std::round(key()), 0.0, 127.0);
      track.note = out.note(track.noteKey, 1.0, 1);
    }
    updateGainRegister();
    emitVoice();
    out.setNoteEnd(*track.note, vm.tick() + 1);
  }

  void updateGainRegister() {
    // Choose a linear GAIN rate from the distance to the next target.
    constexpr std::array<u8, 22> distances{255, 214, 160, 128, 107, 80, 64, 54, 40, 32, 27,
                                           20,  16,  14,  11,  8,   6,  5,  4,  3,  2,  1};
    constexpr std::array<u8, 22> rates{29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19,
                                       18, 17, 16, 15, 14, 13, 12, 10, 9,  6,  0};
    const int target = static_cast<u8>((track.patch.flags & 1) ? track.curves[0].value : track.patch.gain);
    const int difference = target - (track.envelope >> 4);
    const unsigned distance = std::abs(difference) / 2;
    unsigned rateIndex = 21;
    while (rateIndex > 0 && distance >= distances[rateIndex]) {
      --rateIndex;
    }
    track.gainRegister =
        distance == 0 && target == 0 ? 0x82 : static_cast<u8>((difference < 0 ? 0x80 : 0xc0) | rates[rateIndex]);
  }

  [[nodiscard]] Effects waitFrame() {
    auto effect = vm.finiteBranch(Address{vm.sourceRange().offset});
    effect.advanceTicks = 1;
    return effect;
  }

  [[nodiscard]] Effects timed(u8 opcode, u16 absolute, u8 duration) {
    if (track.remaining != 0) {
      if (program.sequenceTick && --track.remaining == 0) {
        return vm.fallthrough();
      }
      return waitFrame();
    }
    if (opcode < 0xf0) {
      if (opcode < 0x80) {
        track.pitch = static_cast<u16>(track.pitch + track.data->deltas[opcode & 31]);
        track.finePitch = 0;
      } else {
        const int delta = ((opcode & 7) + 1) * 2;
        track.finePitch = static_cast<u16>(track.finePitch + ((opcode & 8) ? delta : -delta));
      }
    } else if (opcode == 0xf7 || opcode == 0xf8) {
      track.pitch = absolute;
      track.finePitch = 0;
    }
    if (opcode == 0xf3) {
      if (track.note) {
        out.setNoteEnd(*track.note, vm.tick());
      }
      track.note.reset();
      track.sounding = false;
    } else if (opcode != 0xf4 && duration != 0) {
      const u16 gate = program.nextGate.value_or(duration);
      program.nextGate.reset();
      const bool legato = std::exchange(program.legato, false);
      track.voicePitch = static_cast<u16>(track.pitch + track.finePitch + track.transpose);
      const bool retrigger = opcode < 0xf0 ? (opcode & 0x20) != 0 : opcode == 0xf7;
      if (retrigger) {
        attack(gate, legato);
      } else if (track.sounding) {
        emitVoice();
      }
    }
    if (duration == 0 && opcode != 0xf3 && opcode != 0xf4) {
      return vm.fallthrough();
    }
    track.remaining = duration == 0 ? 256 : duration;
    return waitFrame();
  }

  void volume(u8 value) { track.volume = value; }
  void scaleVolume(u16 scale) { track.volume = scaledVolume(track.volume, scale); }
  void instrument(u8 value) {
    if (!track.phrases.empty()) {
      auto& frame = track.phrases.back();
      if (!frame.phrase.instruments.empty()) {
        const u8 replacement = frame.phrase.instruments[frame.instrumentIndex];
        frame.instrumentIndex = static_cast<u8>((frame.instrumentIndex + 1) % frame.phrase.instruments.size());
        if (replacement != 0xff) {
          value = replacement;
        }
      }
    }
    track.program = value;
  }
  void tempo(u8 value, u16 gateScale) {
    program.tempo = value;
    program.gateScale = gateScale;
  }
  void gate(u16 duration) { program.nextGate = duration; }
  void legato() { program.legato = true; }

  [[nodiscard]] Effects call(Phrase phrase) {
    if (track.phrases.size() == 5) {
      warning("SculptSoftSnes phrase stack exceeds five entries");
      return vm.end();
    }
    const u8 savedVolume = track.volume;
    track.volume = scaledVolume(track.volume, phrase.volumeScale);
    track.transpose = static_cast<u16>(track.transpose + phrase.transpose);
    const auto target = phrase.start;
    track.phrases.push_back(PhraseFrame{.phrase = std::move(phrase), .savedVolume = savedVolume});
    return vm.call(target);
  }

  [[nodiscard]] std::optional<Effects> phraseBoundary() {
    if (track.phrases.empty() || track.phrases.back().phrase.end.value != vm.sourceRange().offset) {
      return std::nullopt;
    }
    auto& frame = track.phrases.back();
    ++frame.iteration;
    if (frame.phrase.count == 0) {
      return vm.loopCandidate(frame.phrase.start);
    }
    if (frame.iteration != frame.phrase.count) {
      return vm.finiteBranch(frame.phrase.start);
    }
    track.volume = frame.savedVolume;
    track.transpose = static_cast<u16>(track.transpose - frame.phrase.transpose);
    track.phrases.pop_back();
    return vm.return_();
  }

  [[nodiscard]] Effects restart(Address start) {
    track.pitch = 0x21c;
    track.volume = 0;
    return vm.loopCandidate(start);
  }

  [[nodiscard]] Effects end() {
    if (track.note) {
      out.setNoteEnd(*track.note, vm.tick());
    }
    return vm.end();
  }
};

using Cursor = CompilerCursor<Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 offset, Address start, Revision revision,
                                                   std::vector<Phrase>& phrases, std::vector<Diagnostic>* diagnostics,
                                                   std::set<u8>* referencedPrograms) {
  Cursor cursor(reader, offset, "sculpt-soft-snes", diagnostics);
  const u8 opcode = cursor.opcode();
  if (opcode < 0xf0) {
    auto event = cursor.command((opcode & 0x20) ? "Note" : "Pitch / Tie", SequenceSemantic::Note);
    return event.invokeFlow<&Playback::timed>(opcode, u16{0}, event.u8("duration"));
  }
  switch (opcode) {
    case 0xf0:
      return cursor.command("End", SequenceSemantic::End).invokeFlow<&Playback::end>().end();
    case 0xf1: {
      auto event = cursor.command("Scale Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::scaleVolume>(event.u16le("multiplier (8.8)"));
    }
    case 0xf2: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume"));
    }
    case 0xf3:
    case 0xf4: {
      auto event = cursor.command(opcode == 0xf3 ? "Rest" : "Wait", SequenceSemantic::Rest);
      return event.invokeFlow<&Playback::timed>(opcode, u16{0}, event.u8("duration"));
    }
    case 0xf5: {
      auto event = cursor.command("Instrument", SequenceSemantic::Instrument);
      const u8 instrument = event.u8("instrument", SemanticOperandRole::Instrument);
      if (referencedPrograms) {
        referencedPrograms->insert(instrument);
      }
      return event.invoke<&Playback::instrument>(instrument);
    }
    case 0xf6: {
      auto event = cursor.command("Phrase", SequenceSemantic::Call);
      Phrase phrase;
      phrase.start = event.addressLe("start", SemanticOperandRole::CallTarget);
      phrase.end = event.addressLe("end");
      phrase.count = event.u8("plays (0 = loop)");
      phrase.transpose = event.u16le("pitch offset (1/20 semitone)");
      phrase.volumeScale = event.u16le("volume multiplier (8.8)");
      const u8 instruments = event.u8("instrument replacements");
      for (u32 i = 0; i < instruments; ++i) {
        const u8 replacement = event.u8("replacement", SemanticOperandRole::Instrument);
        phrase.instruments.push_back(replacement);
        if (referencedPrograms && replacement != 0xff) {
          referencedPrograms->insert(replacement);
        }
      }
      if (!event.ok() || phrase.start.value < 0x200 || phrase.start.value >= phrase.end.value ||
          phrase.end.value >= kAramSize) {
        return event.label("Invalid Phrase").stop();
      }
      phrases.push_back(phrase);
      return event.invokeFlow<&Playback::call>(phrase).call(phrase.start);
    }
    case 0xf7:
    case 0xf8: {
      auto event = cursor.command(opcode == 0xf7 ? "Absolute Note" : "Absolute Pitch / Tie", SequenceSemantic::Note);
      const u16 pitch = event.u16le("pitch (1/20 semitone)");
      return event.invokeFlow<&Playback::timed>(opcode, pitch, event.u8("duration"));
    }
    case 0xf9:
      return cursor.command("Restart Track", SequenceSemantic::Loop)
          .invokeFlow<&Playback::restart>(start)
          .loopCandidate(start);
    case 0xfa: {
      auto event = cursor.command("Tempo / Articulation", SequenceSemantic::Tempo);
      const u8 tempo = event.u8("tempo accumulator step");
      return event.invoke<&Playback::tempo>(tempo, event.u16le("gate multiplier (8.8)"));
    }
    case 0xfb:
      if (revision == Revision::Extended) {
        auto event = cursor.command("Envelope Gate", SequenceSemantic::Envelope);
        u16 duration = 0;
        u8 part;
        do {
          part = event.u8("duration (255 = continue)");
          duration = static_cast<u16>(duration + part);
        } while (part == 255 && event.ok());
        return event.invoke<&Playback::gate>(duration);
      }
      break;
    case 0xfc:
      if (revision == Revision::Extended) {
        return cursor.command("Legato Attack", SequenceSemantic::Note).invoke<&Playback::legato>();
      }
      break;
    default:
      break;
  }
  if (diagnostics) {
    diagnostics->push_back(Diagnostic{.severity = Severity::Warning,
                                      .message = fmt::format("Invalid SculptSoftSnes opcode ${:02X}", opcode),
                                      .range = reader.range(offset, 1)});
  }
  return cursor.unsupported(fmt::format("Invalid Opcode ${:02X}", opcode)).stop();
}

// Phrase returns are address comparisons, not opcodes. Discover each bounded
// phrase separately, then add a return boundary only where no real command lives.
[[nodiscard]] TrackProgram decodeTrack(const TrackDecodeScope& scope, u32 number, u32 start, Revision revision,
                                       std::vector<Diagnostic>* diagnostics, std::set<u8>* referencedPrograms) {
  std::map<u32, DecodedBytecodeCommand> commands;
  std::vector<std::pair<u32, u32>> pending{{start, kAramSize}};
  std::set<u32> ends;
  std::set<std::pair<u32, u32>> visited;
  while (!pending.empty() && commands.size() < kCommandLimit) {
    const auto [begin, end] = pending.back();
    pending.pop_back();
    if (!visited.emplace(begin, end).second) {
      continue;
    }
    for (u32 offset = begin; offset < end && commands.size() < kCommandLimit;) {
      if (const auto existing = commands.find(offset); existing != commands.end()) {
        const auto next = existing->second.flow.discoveryContinuation();
        if (!next || next->value <= offset) {
          break;
        }
        offset = next->value;
        continue;
      }
      std::vector<Phrase> phrases;
      auto command =
          decodeCommand(scope.reader, offset, Address{start}, revision, phrases, diagnostics, referencedPrograms);
      for (const auto& phrase : phrases) {
        pending.emplace_back(phrase.start.value, phrase.end.value);
        ends.insert(phrase.end.value);
      }
      const auto next = command.flow.discoveryContinuation();
      commands.emplace(offset, std::move(command));
      if (!next || next->value <= offset) {
        break;
      }
      offset = next->value;
    }
  }
  for (const u32 end : ends) {
    if (!commands.contains(end)) {
      commands.emplace(end,
                       DecodedBytecodeCommand{
                           .range = scope.reader.range(end, 0),
                           .flow = {.continuation = Address{end}, .defaultTransition = CommandTransition::return_()},
                           .presentation = {.label = "Phrase End",
                                            .kind = "sculpt-soft-snes-phrase-end",
                                            .semantic = SequenceSemantic::Return,
                                            .playback = CommandPlaybackStatus::AffectsControlFlow},
                       });
    }
  }
  auto session = scope.begin(number, start);
  for (auto& [address, command] : commands) {
    auto body = std::move(command.execution.body);
    command.execution.body = [body = std::move(body)](void* state) {
      auto& playback = *static_cast<Playback*>(state);
      if (const auto boundary = playback.phraseBoundary()) {
        return *boundary;
      }
      return body ? body(state) : playback.vm.end();
    };
    session.findOrAppend(std::move(command), address);
  }
  return session.finish();
}

}  // namespace

SequenceProgram decodeSequence(ByteReader reader, const Layout& layout, const DriverData& data, AssetId id,
                               SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics,
                               std::set<u8>* referencedPrograms) {
  const SequenceProgramConfig config{
      .commandKindPrefix = "sculpt-soft-snes",
      .timebase = {.ppqn = 50},
      .behavior = {.commandLimit = kCommandLimit,
                   .inferLoopsFromRepeatedState = false,
                   .initialLevel = 1.0,
                   .initialMasterLevel = 75.0 / 128.0,
                   .initialReverbSend = 0.0,
                   .initialTempoMicrosecondsPerQuarter = layout.frameMicroseconds * 50u},
  };
  SequenceDecodeSession sequence(reader, config, id, reader.range(layout.song, 1u + layout.tracks), sourceMap,
                                 kCommandLimit, kAramSize);
  if (referencedPrograms) {
    referencedPrograms->insert(0);
  }
  const u16 tracks = reader.le16(layout.tables + 0x10);
  for (u32 i = layout.tracks; i-- > 0;) {
    const u32 pointer = tracks + reader.u8At(layout.song + 1 + i) * 2u;
    const u16 start = reader.le16(pointer);
    sequence.trackPointer(i, reader.range(pointer, 2), start);
    sequence.addTrack(decodeTrack(sequence.trackScope(), i, start, layout.revision, diagnostics, referencedPrograms));
  }
  return sequence.finish(
      makeCompiledRuntime<Playback, ProgramState>(RuntimeConfig{.data = std::make_shared<const DriverData>(data),
                                                                .frameMicroseconds = layout.frameMicroseconds,
                                                                .revision = layout.revision}));
}

}  // namespace vgmtrans::formats::sculpt_soft_snes
