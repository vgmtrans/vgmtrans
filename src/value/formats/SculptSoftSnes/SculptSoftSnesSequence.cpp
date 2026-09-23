/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SculptSoftSnes/SculptSoftSnes.h"
#include "value/formats/SculptSoftSnes/SculptSoftSnesEarly.h"
#include "value/formats/SculptSoftSnes/SculptSoftSnesLate.h"
#include "value/formats/SculptSoftSnes/SculptSoftSnesPhrase.h"
#include "value/formats/SculptSoftSnes/SculptSoftSnesVoice.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"

#include <fmt/format.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace vgmtrans::formats::sculpt_soft_snes {

using namespace core;

namespace {

struct ProgramState {
  u8 tempo = 0;
  u8 phase = 0xff;
  u16 gateScale = 0x100;
  u64 frame = 0;
  bool sequenceTick = true;
  EchoState echo;
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

struct TrackState : VoiceState {
  using VoiceState::VoiceState;

  u16 pitch = 0x21c;
  u16 finePitch = 0;
  u8 volume = 0;
  u8 program = 0;
  PhraseStack phrases;
  u16 remaining = 0;
};

struct Playback : SequencePlayback<TrackState> {
  ProgramState& program;

  void beforeCommand() { program.advance(vm.tick()); }

  [[nodiscard]] Voice voice() { return Voice{track, program.echo, out, vm}; }

  void tick() {
    program.advance(vm.tick());
    voice().tick();
  }

  void attack(u16 duration, bool legato) {
    const auto scaledByte = [&](u8 value) { return (u32(value) * program.gateScale + 128) >> 8; };
    // The extended driver scales and rounds the two duration bytes separately.
    const u16 gate = static_cast<u16>(scaledByte(static_cast<u8>(duration)) +
                                      (track.revision == Revision::Extended ? scaledByte(duration >> 8) << 8 : 0));
    voice().attack(track.program, track.volume, gate, legato);
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
      voice().silence();
    } else if (opcode != 0xf4 && duration != 0) {
      const u16 gate = program.nextGate.value_or(duration);
      program.nextGate.reset();
      const bool legato = std::exchange(program.legato, false);
      track.voicePitch = static_cast<u16>(track.pitch + track.finePitch + track.phrases.transpose);
      const bool retrigger = opcode < 0xf0 ? (opcode & 0x20) != 0 : opcode == 0xf7;
      if (retrigger) {
        attack(gate, legato);
      } else if (track.sounding) {
        voice().emitVoice();
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
  void instrument(u8 value) { track.program = track.phrases.instrument(value); }
  void tempo(u8 value, u16 gateScale) {
    program.tempo = value;
    program.gateScale = gateScale;
  }
  void gate(u16 duration) { program.nextGate = duration; }
  void legato() { program.legato = true; }

  [[nodiscard]] Effects call(Phrase phrase) { return track.phrases.call(std::move(phrase), track.volume, vm); }
  [[nodiscard]] std::optional<Effects> phraseBoundary() { return track.phrases.boundary(track.volume, vm); }

  [[nodiscard]] Effects restart(Address start) {
    track.pitch = 0x21c;
    track.volume = 0;
    return vm.loopCandidate(start);
  }

  [[nodiscard]] Effects end() {
    voice().silence();
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
    return cursor.command((opcode & 0x20) ? "Note" : "Pitch / Tie", SequenceSemantic::Note)
        .invokeFlow<&Playback::timed>({opcode, u16{0}, cursor.u8("duration")});
  }
  switch (opcode) {
    case 0xf0:
      return cursor.command("End", SequenceSemantic::End).invokeFlow<&Playback::end>().end();
    case 0xf1:
      return cursor.command("Scale Volume", SequenceSemantic::Level)
          .invoke<&Playback::scaleVolume>({cursor.u16le("multiplier (8.8)")});
    case 0xf2:
      return cursor.command("Volume", SequenceSemantic::Level).invoke<&Playback::volume>({cursor.u8("volume")});
    case 0xf3:
    case 0xf4:
      return cursor.command(opcode == 0xf3 ? "Rest" : "Wait", SequenceSemantic::Rest)
          .invokeFlow<&Playback::timed>({opcode, u16{0}, cursor.u8("duration")});
    case 0xf5: {
      auto event = cursor.command("Instrument", SequenceSemantic::Instrument);
      const u8 instrument = cursor.u8("instrument", SemanticOperandRole::Instrument);
      if (referencedPrograms) {
        referencedPrograms->insert(instrument);
      }
      return event.invoke<&Playback::instrument>({instrument});
    }
    case 0xf6: {
      auto event = cursor.command("Phrase", SequenceSemantic::Call);
      Phrase phrase = readPhrase(cursor, referencedPrograms);
      if (!cursor.ok() || phrase.start.value < 0x200 || phrase.start.value >= phrase.end.value ||
          phrase.end.value >= kAramSize) {
        return event.label("Invalid Phrase").stop();
      }
      phrases.push_back(phrase);
      return event.invokeFlow<&Playback::call>({phrase}).call(phrase.start);
    }
    case 0xf7:
    case 0xf8:
      return cursor.command(opcode == 0xf7 ? "Absolute Note" : "Absolute Pitch / Tie", SequenceSemantic::Note)
          .invokeFlow<&Playback::timed>({opcode, cursor.u16le("pitch (1/20 semitone)"), cursor.u8("duration")});
    case 0xf9:
      return cursor.command("Restart Track", SequenceSemantic::Loop)
          .invokeFlow<&Playback::restart>({start})
          .loopCandidate(start);
    case 0xfa:
      return cursor.command("Tempo / Articulation", SequenceSemantic::Tempo)
          .invoke<&Playback::tempo>({cursor.u8("tempo accumulator step"), cursor.u16le("gate multiplier (8.8)")});
    case 0xfb:
      if (revision == Revision::Extended) {
        auto event = cursor.command("Envelope Gate", SequenceSemantic::Envelope);
        u16 duration = 0;
        u8 part;
        do {
          part = cursor.u8("duration (255 = continue)");
          duration = static_cast<u16>(duration + part);
        } while (part == 255 && cursor.ok());
        return event.invoke<&Playback::gate>({duration});
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

[[nodiscard]] TrackProgram decodeTrack(const TrackDecodeScope& scope, u32 number, u32 start, Revision revision,
                                       std::vector<Diagnostic>* diagnostics, std::set<u8>* referencedPrograms) {
  return decodePhraseTrack<Playback>(
      scope, number, start, diagnostics, [&](u32 offset, bool&, std::vector<Phrase>& phrases) {
        return decodeCommand(scope.reader, offset, Address{start}, revision, phrases, diagnostics, referencedPrograms);
      });
}

}  // namespace

SequenceProgram decodeSequence(ByteReader reader, const Layout& layout, const DriverData& data, AssetId id,
                               SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics,
                               std::set<u8>* referencedPrograms) {
  // One performance tick is one envelope frame. Music tempo changes the wait
  // counters, not this timebase, so physical envelopes keep their original rate.
  const SequenceProgramConfig config{
      .commandKindPrefix = "sculpt-soft-snes",
      .timebase = {.ppqn = 50},
      .behavior = {.commandLimit = kCommandLimit,
                   .inferLoopsFromRepeatedState = false,
                   .initialLevel = 1.0,
                   .initialMasterLevel = (layout.revision == Revision::Late ? 127.0 : 75.0) / 128.0,
                   .initialReverbSend = 0.0,
                   .initialTempoMicrosecondsPerQuarter = layout.frameMicroseconds * 50u},
  };
  const bool late = layout.revision == Revision::Late;
  SequenceDecodeSession sequence(reader, config, id,
                                 reader.range(layout.song, 1u + layout.tracks * (layout.inlineTrackPointers ? 2u : 1u)),
                                 sourceMap, kCommandLimit, kAramSize);
  if (referencedPrograms) {
    referencedPrograms->insert(0);
  }
  const u16 tracks = reader.le16(layout.tables + (layout.revision == Revision::Early ? 0x18 : 0x10));
  for (u32 order = 0; order < layout.tracks; ++order) {
    const u32 i = late ? order : layout.tracks - order - 1;
    const u32 pointer =
        layout.inlineTrackPointers ? layout.song + 1 + i * 2u : tracks + reader.u8At(layout.song + 1 + i) * 2u;
    const u16 start = reader.le16(pointer);
    sequence.trackPointer(i, reader.range(pointer, 2), start);
    switch (layout.revision) {
      case Revision::Early:
        sequence.addTrack(decodeEarlyTrack(sequence.trackScope(), layout, i, start, diagnostics, referencedPrograms));
        break;
      case Revision::Late:
        sequence.addTrack(decodeLateTrack(sequence.trackScope(), layout, i, start, diagnostics, referencedPrograms));
        break;
      default:
        sequence.addTrack(
            decodeTrack(sequence.trackScope(), i, start, layout.revision, diagnostics, referencedPrograms));
        break;
    }
  }
  RuntimeConfig runtime{.data = std::make_shared<const DriverData>(data),
                        .frameMicroseconds = layout.frameMicroseconds,
                        .revision = layout.revision};
  switch (layout.revision) {
    case Revision::Early:
      return sequence.finish(makeEarlyRuntime(std::move(runtime)));
    case Revision::Late:
      return sequence.finish(makeLateRuntime(std::move(runtime)));
    default:
      return sequence.finish(makeCompiledRuntime<Playback, ProgramState>(std::move(runtime)));
  }
}

}  // namespace vgmtrans::formats::sculpt_soft_snes
