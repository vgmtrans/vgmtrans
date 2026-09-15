/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SculptSoftSnes/SculptSoftSnesLate.h"
#include "value/formats/SculptSoftSnes/SculptSoftSnesPhrase.h"
#include "value/sequence/CompilerCursor.h"

#include <fmt/format.h>
#include <algorithm>
#include <bit>
#include <map>
#include <utility>

namespace vgmtrans::formats::sculpt_soft_snes {
using namespace core;
namespace {

enum class TimedEvent { AbsoluteNote, RelativeNote, FinePitch, Rest, Wait };

struct VoiceSlot {
  std::optional<u32> owner;
  u8 priority = 0;
  bool active = false;
};

struct ProgramState {
  EchoState echo;
  std::array<VoiceSlot, 8> voices;
  std::array<std::optional<u64>, 20> stolen;
  u8 tempo = 0;
  u8 phase = 0xff;
  u64 frame = 0;
  bool sequenceTick = true;
  bool legato = false;
  bool tie = false;

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
  TrackState(TrackStateContext context, const RuntimeConfig& config)
      : VoiceState(context, config), assignedVoice(static_cast<u8>(number)) {}

  u8 pitch = 0;
  u16 finePitch = 0;
  u16 trackTranspose = 0;
  u8 volume = 127;
  u8 volumeScale = 255;
  u8 program = 0;
  u16 rawPitch = 0;
  s8 pan = 0;
  std::optional<u8> currentVoice;
  u8 priority = 64;
  u8 assignedVoice;
  u8 voiceMode = 8;
  u8 voiceMask = 0;
  u16 remaining = 0;
  bool ending = false;
  PhraseStack phrases;
};

struct Playback : SequencePlayback<TrackState> {
  ProgramState& program;

  Voice voice() { return Voice{track, program.echo, out, vm}; }
  void retireStolenVoice() {
    auto& stolen = program.stolen[track.number];
    if (stolen) {
      if (track.note) {
        out.setNoteEnd(*track.note, *stolen);
      }
      track.note.reset();
      track.sounding = false;
      track.currentVoice.reset();
      stolen.reset();
    }
  }
  void beforeCommand() {
    program.advance(vm.tick());
    retireStolenVoice();
  }
  void tick() {
    program.advance(vm.tick());
    retireStolenVoice();
    voice().tick();
    if (track.currentVoice && (track.patch.flags & 1) && !track.lateCurves[0].active) {
      program.voices[*track.currentVoice].active = false;
    }
  }

  Effects waitFrame() {
    auto effect = vm.finiteBranch(Address{vm.sourceRange().offset});
    effect.advanceTicks = 1;
    return effect;
  }

  enum class Allocation { Ready, Muted, Unsupported };

  Allocation claimVoice(bool reuse) {
    if (reuse && track.currentVoice) {
      auto& slot = program.voices[*track.currentVoice];
      slot.active = true;
      slot.priority = track.priority;
      return Allocation::Ready;
    }
    if ((track.voiceMode & 0x10) != 0) {
      return Allocation::Muted;
    }
    u8 assigned = track.assignedVoice;
    if ((track.voiceMode & 8) == 0) {
      if (track.voiceMask == 0) {
        return Allocation::Muted;
      }
      if (std::popcount(track.voiceMask) != 1) {
        voice().warning("SculptSoftSnes rotating voice allocation is not supported");
        return Allocation::Unsupported;
      }
      assigned = static_cast<u8>(std::countr_zero(unsigned(track.voiceMask)));
    }
    if (assigned >= 8) {
      return Allocation::Muted;
    }
    if (track.sounding && track.dspVoice != assigned) {
      voice().warning("Overlapping SculptSoftSnes voices on one sequence track are not supported");
      return Allocation::Unsupported;
    }
    auto& slot = program.voices[assigned];
    if (slot.active && ((track.voiceMode & 2) || track.priority < slot.priority ||
                        ((track.voiceMode & 1) && track.priority == slot.priority))) {
      return Allocation::Muted;
    }
    if (slot.owner && *slot.owner != track.number) {
      program.stolen[*slot.owner] = vm.tick();
    }
    slot = VoiceSlot{.owner = track.number, .priority = track.priority, .active = true};
    track.currentVoice = assigned;
    track.dspVoice = assigned;
    return Allocation::Ready;
  }

  Effects timed(TimedEvent action, s16 value, u16 duration) {
    if (track.remaining != 0) {
      if (program.sequenceTick && --track.remaining == 0) {
        return vm.fallthrough();
      }
      return waitFrame();
    }
    if (action == TimedEvent::Rest) {
      release();
    } else if (action != TimedEvent::Wait) {
      if (action == TimedEvent::FinePitch) {
        track.finePitch = static_cast<u16>(track.finePitch + value);
      } else {
        track.pitch = static_cast<u8>(value + (action == TimedEvent::RelativeNote ? track.pitch : 0));
        track.finePitch = 0;
      }
      bool attack = action != TimedEvent::FinePitch && !std::exchange(program.tie, false);
      const bool legato = program.legato && track.currentVoice.has_value();
      attack = attack || !track.currentVoice;
      if (attack) {
        release();
      }
      const auto allocation = claimVoice(!attack || legato);
      if (allocation == Allocation::Unsupported) {
        return stop();
      }
      if (allocation == Allocation::Ready) {
        const u16 pitch = static_cast<u16>(track.pitch * 20 - 900 + track.finePitch + track.phrases.transpose);
        // The track-transpose handler's low result is discarded in this driver:
        // attacks ignore it, while ties retain only the resulting high byte.
        track.rawPitchOffset = track.rawPitch;
        track.voicePitch =
            attack ? pitch : static_cast<u16>(((pitch + track.trackTranspose) & 0xff00) | (pitch & 0xff));
        if (attack || !track.sounding) {
          const u8 volume = static_cast<u8>((std::min<u8>(track.volume, 127) * track.volumeScale) >> 8);
          track.panOffset = track.pan;
          voice().attack(track.program, volume, 0xffff, legato && track.sounding);
          program.legato = false;
        } else {
          voice().emitVoice();
        }
      }
    }
    if (duration == 0) {
      return vm.fallthrough();
    }
    track.remaining = duration;
    return waitFrame();
  }

  void noop() {}
  void release() {
    track.released = true;
    if (track.currentVoice && program.voices[*track.currentVoice].active) {
      auto& slot = program.voices[*track.currentVoice];
      slot.priority = (track.patch.flags & 0x80) ? static_cast<u8>(slot.priority - 1) : slot.priority / 2;
    }
  }
  void fine(bool keyOff) {
    track.finePitch = 0;
    if (keyOff) {
      release();
    }
  }
  void parameter(u8 opcode, u16 value) {
    switch (opcode) {
      case 0xe0:
        track.rawPitch = value;
        break;
      case 0xe1:
        track.curveSpeeds[0] = value;
        break;
      case 0xe2:
        track.curveSpeeds[2] = value;
        break;
      case 0xe3:
        track.curveSpeeds[3] = value;
        break;
      case 0xe4:
        track.curveSpeeds[1] = value;
        break;
      case 0xed:
        track.volumeScale = value;
        break;
      case 0xf3:
        track.priority = value;
        break;
      case 0xf4:
        program.tie = true;
        break;

      case 0xfa:
        program.tempo = value;
        break;
      case 0xfb:
        track.pan = static_cast<s8>(value);
        break;
      case 0xfc:
        program.legato = true;
        break;
      default:
        break;
    }
  }
  void transpose(u16 value, bool shared) { (shared ? track.phrases.transpose : track.trackTranspose) = value; }
  void assign(u8 mode, u8 start, u8 mask) {
    track.voiceMode = mode;
    track.assignedVoice = start;
    track.voiceMask = mask;
  }
  void defaultVoice() { assign(8, static_cast<u8>(track.number), 0); }
  void reset() {
    if (track.currentVoice) {
      program.voices[*track.currentVoice].active = false;
      track.currentVoice.reset();
    }
    track.pitch = 0;
    track.finePitch = track.phrases.transpose = track.trackTranspose = 0;
    track.rawPitch = 0;
    track.pan = 0;
    track.curveSpeeds = {};
    track.volume = 127;
    track.volumeScale = 255;
    track.priority = 64;
    defaultVoice();
    program.legato = program.tie = false;
  }
  void volume(u8 value) { track.volume = value; }
  void scaleVolume(u16 scale) { track.volume = scaledVolume(track.volume, scale); }
  void instrument(u8 value) { track.program = track.phrases.instrument(value); }
  [[nodiscard]] Effects call(Phrase phrase) { return track.phrases.call(std::move(phrase), track.volume, vm); }
  [[nodiscard]] std::optional<Effects> phraseBoundary() { return track.phrases.boundary(track.volume, vm); }

  Effects restart(Address start) {
    reset();
    track.phrases.clear();
    program.phase = 255;
    return vm.loopCandidate(start);
  }
  Effects stop() {
    if (track.currentVoice) {
      program.voices[*track.currentVoice].active = false;
    }
    voice().silence();
    return vm.end();
  }
  Effects end() {
    if (!std::exchange(track.ending, true)) {
      release();
    }
    if (track.sounding && (track.patch.flags & 1)) {
      const auto& player = track.lateCurves[0];
      const auto& curve = *player.curve;
      const bool finiteRelease = curve.loopEnd == 255 || curve.loopEnd + 1u < curve.pointCount;
      if (finiteRelease && (player.active || (voice().gainTarget() == 0 && track.envelope != 0))) {
        return waitFrame();
      }
    }
    return stop();
  }
};

using Cursor = CompilerCursor<Playback>;

DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 offset, Address start, bool& fine, const Layout& layout,
                                     std::vector<Phrase>& phrases, std::vector<Diagnostic>* diagnostics,
                                     std::set<u8>* referencedPrograms) {
  Cursor cursor(reader, offset, "sculpt-soft-snes", diagnostics);
  const u8 opcode = cursor.opcode();
  if (fine) {
    if (opcode == 0) {
      fine = false;
      return cursor.command("End Fine Pitch", SequenceSemantic::State).invoke<&Playback::noop>();
    }
    if (opcode == 2) {
      return cursor.command("Key Off", SequenceSemantic::Envelope).invoke<&Playback::release>();
    }
    auto event = cursor.command(opcode == 1 ? "Fine Pitch Wait" : "Fine Pitch", SequenceSemantic::Pitch);
    if (opcode == 1) {
      const u8 duration = event.u8("duration (0 = 256)");
      return event.invokeFlow<&Playback::timed>(TimedEvent::Wait, s16{0}, u16(duration == 0 ? 256 : duration));
    }
    const s16 delta = opcode < 16 ? event.s8("pitch delta (1/20 semitone)") : s16((opcode >> 4) - 8);
    // Extended fine commands reuse the now-zero wait counter and are immediate.
    return event.invokeFlow<&Playback::timed>(TimedEvent::FinePitch, delta, u16(opcode < 16 ? 0 : opcode & 15));
  }
  if (opcode < 0xe0) {
    if (opcode >= 0xc0) {
      return cursor.command("Rest", SequenceSemantic::Rest)
          .invokeFlow<&Playback::timed>(TimedEvent::Rest, s16{0}, u16(opcode - 0xbf));
    }
    auto event = cursor.command("Note", SequenceSemantic::Note);
    if (opcode < 0x80) {
      return event.invokeFlow<&Playback::timed>(TimedEvent::RelativeNote, s16((opcode >> 3) - 8),
                                                u16((opcode & 7) + 1));
    }
    const u8 duration = event.u8("duration (0 = 256)");
    return event.invokeFlow<&Playback::timed>(TimedEvent::AbsoluteNote, s16(opcode - 0x60),
                                              u16(duration == 0 ? 256 : duration));
  }
  switch (opcode) {
    case 0xe0: {
      auto event = cursor.command("DSP Pitch Offset", SequenceSemantic::Pitch);
      return event.invoke<&Playback::parameter>(opcode, event.u16le("offset"));
    }
    case 0xf7: {
      auto event = cursor.command("Track Pitch Offset", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transpose>(event.u16le("offset (1/20 semitone)"), layout.sharedTranspose);
    }
    case 0xe1:
    case 0xe2:
    case 0xe3:
    case 0xe4: {
      constexpr std::array labels{"GAIN Envelope Speed", "Pan Envelope Speed", "Sample Envelope Speed",
                                  "Pitch Envelope Speed"};
      auto event = cursor.command(labels[opcode - 0xe1], SequenceSemantic::Envelope);
      return event.invoke<&Playback::parameter>(opcode, u16(event.u8("multiplier / 32 (0 = default)")));
    }
    case 0xe5: {
      auto event = cursor.command("Voice Allocation", SequenceSemantic::State);
      const u8 mode = event.u8("mode");
      const u8 voice = event.u8("starting voice");
      return event.invoke<&Playback::assign>(mode, voice, event.u8("voice mask"));
    }
    case 0xe6:
      return cursor.command("Default Voice", SequenceSemantic::State).invoke<&Playback::defaultVoice>();
    case 0xe7:
      if (layout.resetCommand) {
        return cursor.command("Reset Track Parameters", SequenceSemantic::State).invoke<&Playback::reset>();
      }
      return cursor.command("No Operation", SequenceSemantic::State).invoke<&Playback::noop>();
    case 0xe9: {
      auto event = cursor.command("Sound Effect Voice Mask", SequenceSemantic::State);
      (void)event.u8("value");
      return event.invoke<&Playback::noop>();
    }
    case 0xea:
      return cursor.command("Key Off", SequenceSemantic::Envelope).invoke<&Playback::release>();
    case 0xeb:
    case 0xec:
      fine = true;
      return cursor
          .command(opcode == 0xeb ? "Fine Pitch Stream" : "Key Off / Fine Pitch Stream", SequenceSemantic::Pitch)
          .invoke<&Playback::fine>(opcode == 0xec);
    case 0xed: {
      auto event = cursor.command("Volume Multiplier", SequenceSemantic::Level);
      return event.invoke<&Playback::parameter>(opcode, u16(event.u8("multiplier / 256")));
    }
    case 0xf3: {
      auto event = cursor.command("Voice Priority", SequenceSemantic::State);
      return event.invoke<&Playback::parameter>(opcode, u16(event.u8("priority")));
    }
    case 0xfa: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::parameter>(opcode, u16(event.u8("tempo accumulator step")));
    }
    case 0xfb: {
      auto event = cursor.command("Pan Offset", SequenceSemantic::Pan);
      return event.invoke<&Playback::parameter>(opcode, static_cast<u16>(event.s8("offset")));
    }
    case 0xef: {
      auto event = cursor.command("Absolute Note", SequenceSemantic::Note);
      const u8 note = event.u8("note");
      const u8 duration = event.u8("duration (0 = 256)");
      return event.invokeFlow<&Playback::timed>(TimedEvent::AbsoluteNote, s16(note),
                                                u16(duration == 0 ? 256 : duration));
    }
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
    case 0xf4:
    case 0xfc:
      return cursor.command(opcode == 0xf4 ? "Tie Next Note" : "Legato Attack", SequenceSemantic::Note)
          .invoke<&Playback::parameter>(opcode, u16{0});
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
      Phrase phrase = readPhrase(event, referencedPrograms);
      if (!event.ok() || phrase.start.value < 0x200 || phrase.start.value >= phrase.end.value ||
          phrase.end.value >= kAramSize) {
        return event.label("Invalid Phrase").stop();
      }
      phrases.push_back(phrase);
      return event.invokeFlow<&Playback::call>(phrase).call(phrase.start);
    }

    case 0xf8:
    case 0xfd:
    case 0xfe:
    case 0xff:
      return cursor.command("Wait", SequenceSemantic::Rest)
          .invokeFlow<&Playback::timed>(TimedEvent::Wait, s16{0}, u16(opcode == 0xf8 ? 32 : 64u << (opcode - 0xfd)));
    case 0xf9:
      return cursor.command("Restart Track", SequenceSemantic::Loop)
          .invokeFlow<&Playback::restart>(start)
          .loopCandidate(start);
    default:
      break;
  }
  if (diagnostics) {
    diagnostics->push_back(Diagnostic{.severity = Severity::Warning,
                                      .message = fmt::format("Invalid late SculptSoftSnes opcode ${:02X}", opcode),
                                      .range = reader.range(offset, 1)});
  }
  return cursor.unsupported(fmt::format("Invalid Opcode ${:02X}", opcode)).stop();
}

// Phrase returns are address comparisons, not opcodes. Discover each bounded
// phrase separately, then add a return boundary only where no real command lives.
[[nodiscard]] TrackProgram decodeLateTrackImpl(const TrackDecodeScope& scope, u32 number, u32 start,
                                               const Layout& layout, std::vector<Diagnostic>* diagnostics,
                                               std::set<u8>* referencedPrograms) {
  std::map<u32, DecodedBytecodeCommand> commands;
  std::map<u32, bool> interpretations;
  std::vector<std::pair<u32, u32>> pending{{start, kAramSize}};
  std::set<u32> ends;
  std::set<std::pair<u32, u32>> visited;
  while (!pending.empty() && commands.size() < kCommandLimit) {
    const auto [begin, end] = pending.back();
    pending.pop_back();
    if (!visited.emplace(begin, end).second) {
      continue;
    }
    bool fine = false;
    for (u32 offset = begin; offset < end && commands.size() < kCommandLimit;) {
      if (const auto existing = commands.find(offset); existing != commands.end()) {
        if (interpretations.at(offset) != fine) {
          if (diagnostics) {
            diagnostics->push_back(Diagnostic{.severity = Severity::Warning,
                                              .message = "Conflicting SculptSoftSnes fine-pitch stream interpretations",
                                              .range = scope.reader.range(offset, 1)});
          }
          break;
        }
        const auto opcode = scope.reader.u8At(offset);
        fine = fine ? opcode != 0 : opcode == 0xeb || opcode == 0xec;
        const auto next = existing->second.flow.discoveryContinuation();
        if (!next || next->value <= offset) {
          break;
        }
        offset = next->value;
        continue;
      }
      interpretations.emplace(offset, fine);
      const bool boundary = !fine;
      std::vector<Phrase> phrases;
      auto command =
          decodeCommand(scope.reader, offset, Address{start}, fine, layout, phrases, diagnostics, referencedPrograms);
      for (const auto& phrase : phrases) {
        pending.emplace_back(phrase.start.value, phrase.end.value);
        ends.insert(phrase.end.value);
      }
      if (boundary) {
        auto body = std::move(command.execution.body);
        command.execution.body = [body = std::move(body)](void* state) {
          auto& playback = *static_cast<Playback*>(state);
          if (const auto effect = playback.phraseBoundary()) {
            return *effect;
          }
          return body ? body(state) : playback.vm.end();
        };
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
    if (!command.execution.body) {
      command.execution.body = [](void* state) {
        auto& playback = *static_cast<Playback*>(state);
        if (const auto boundary = playback.phraseBoundary()) {
          return *boundary;
        }
        return playback.vm.end();
      };
    }
    session.findOrAppend(std::move(command), address);
  }
  return session.finish();
}

}  // namespace

TrackProgram decodeLateTrack(const TrackDecodeScope& scope, const Layout& layout, u32 number, u32 start,
                             std::vector<Diagnostic>* diagnostics, std::set<u8>* referencedPrograms) {
  return decodeLateTrackImpl(scope, number, start, layout, diagnostics, referencedPrograms);
}

SequenceRuntime makeLateRuntime(RuntimeConfig config) {
  return makeCompiledRuntime<Playback, ProgramState>(std::move(config));
}

}  // namespace vgmtrans::formats::sculpt_soft_snes
