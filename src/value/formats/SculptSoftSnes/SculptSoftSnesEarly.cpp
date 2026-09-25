/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SculptSoftSnes/SculptSoftSnesEarly.h"
#include "value/sequence/CompilerCursor.h"

#include <fmt/format.h>
#include <algorithm>
#include <bit>
#include <map>

namespace vgmtrans::formats::sculpt_soft_snes {
using namespace core;

namespace {

enum class Layer { Track, List, Pattern, Notes };
struct Block {
  u32 address;
  Layer layer;
};

struct TrackState : VoiceState {
  using VoiceState::VoiceState;

  u16 pitch = 0;
  u16 finePitch = 0;
  u16 trackTranspose = 0;
  u16 patternTranspose = 0;
  u8 divisor = 0;
  u8 baseVolume = 0;
  u8 noteVolume = 0;
  std::array<u8, 2> patches{};
  bool pitched = true;
  std::optional<u8> assignedVoice;
  u32 remaining = 0;
};

struct ProgramState {
  EchoState echo;
  std::array<std::optional<u32>, 8> voiceOwners;
};

struct Playback : SequencePlayback<TrackState> {
  ProgramState& program;

  [[nodiscard]] Voice voice() { return Voice{track, program.echo, out, vm}; }
  void tick() { voice().tick(); }

  void initialPitch(u16 value) { track.pitch = value; }
  void divisor(u8 value) { track.divisor = value; }
  void transpose(u16 value, bool pattern) { (pattern ? track.patternTranspose : track.trackTranspose) = value; }
  void instrument(u8 value, bool alternate) { track.patches[alternate] = value; }
  void pitchMode(bool pitched, u16 value) {
    track.pitched = pitched;
    if (!pitched) {
      track.pitch = value;
    }
  }
  void volume(u8 value, bool relative, bool base) {
    auto& target = base ? track.baseVolume : track.noteVolume;
    target = relative ? static_cast<u8>(std::clamp(int(target) + static_cast<s8>(value), 0, 127)) : value;
  }
  [[nodiscard]] Effects assignVoice(u8 value, u8 mask) {
    // TrackState holds one voice; changing assignments can leave independent
    // envelopes sounding on multiple DSP voices, which this runtime cannot retain.
    if (value == 0xff && mask != 0 && (mask & (mask - 1)) == 0) {
      value = static_cast<u8>(std::countr_zero(unsigned(mask)));
    }
    if (value > 7 || (track.assignedVoice && track.assignedVoice != value)) {
      voice().warning("SculptSoftSnes dynamic voice allocation is not supported");
      return end();
    }
    track.assignedVoice = value;
    return vm.fallthrough();
  }

  [[nodiscard]] Effects waitFrame() {
    auto effect = vm.finiteBranch(Address{vm.sourceRange().offset});
    effect.advanceTicks = 1;
    return effect;
  }

  [[nodiscard]] Effects timed(u8 opcode, u8 duration) {
    if (track.remaining != 0) {
      return --track.remaining == 0 ? vm.fallthrough() : waitFrame();
    }
    if (opcode < 0xf0 && track.pitched) {
      if (opcode < 0x80) {
        track.pitch = static_cast<u16>(track.pitch + track.data->deltas[opcode & 31]);
        track.finePitch = 0;
      } else {
        const int delta = ((opcode & 7) + 1) * 2;
        track.finePitch = static_cast<u16>(track.finePitch + ((opcode & 8) ? delta : -delta));
      }
    }
    if (opcode == 0xf3) {
      voice().silence();
    } else if (opcode != 0xf4 && duration != 0) {
      auto& owner = program.voiceOwners[track.assignedVoice.value_or(0)];
      if (owner && *owner != track.number) {
        voice().warning("Sharing a SculptSoftSnes DSP voice between tracks is not supported");
        return end();
      }
      owner = track.number;
      track.voicePitch =
          track.pitched
              ? static_cast<u16>(track.pitch + track.finePitch + track.trackTranspose + track.patternTranspose)
              : track.pitch;
      if ((opcode & 0x20) != 0) {
        const u8 volume = static_cast<u8>(track.baseVolume + track.noteVolume);
        voice().attack(track.patches[(opcode & 0x40) != 0], (volume & 0x80) ? 127 : volume,
                       static_cast<u16>(duration * track.divisor));
      } else if (track.sounding) {
        voice().emitVoice();
      }
    }
    if (duration == 0 && opcode < 0xf0) {
      return vm.fallthrough();
    }
    // Both counters are bytes. Zero wraps to 256, but the envelope gate above
    // uses the ordinary 8-by-8 multiply, including a literal zero divisor.
    track.remaining = (duration == 0 ? 256u : duration) * (track.divisor == 0 ? 256u : track.divisor);
    return waitFrame();
  }

  [[nodiscard]] Effects end() {
    voice().silence();
    return vm.end();
  }
};

using Cursor = CompilerCursor<Playback>;

struct Decoder {
  ByteReader reader;
  const Layout& layout;
  Address start;
  std::vector<Diagnostic>* diagnostics;
  std::set<u8>* referencedPrograms;
  std::vector<Block> pending;

  [[nodiscard]] DecodedBytecodeCommand invalid(Cursor& cursor, u32 offset, std::string message) const {
    if (diagnostics) {
      diagnostics->push_back(
          Diagnostic{.severity = Severity::Warning, .message = message, .range = reader.range(offset, 1)});
    }
    return cursor.unsupported(message).stop();
  }

  [[nodiscard]] DecodedBytecodeCommand call(Cursor& cursor, u32 offset, u8 table, Layer layer, std::string_view name) {
    auto event = cursor.command(name, SequenceSemantic::Call);
    const u8 index = cursor.u8("index");
    const auto target = readTablePointer(reader, layout, table, index);
    if (!cursor.ok() || !target) {
      return invalid(cursor, offset, "Invalid SculptSoftSnes pattern pointer");
    }
    pending.push_back(Block{*target, layer});
    return event.call(Address{*target});
  }

  [[nodiscard]] DecodedBytecodeCommand decode(u32 offset, Layer layer) {
    Cursor cursor(reader, offset, "sculpt-soft-snes", diagnostics);
    const u8 opcode = cursor.opcode();
    if (layer == Layer::Pattern) {
      auto event = cursor.command("Initial Pattern Pitch", SequenceSemantic::Pitch);
      const u16 pitch = opcode | (cursor.u8("high pitch byte") << 8);
      return event.invoke<&Playback::initialPitch>({pitch});
    }
    if (layer == Layer::Notes) {
      if (opcode < 0xf0 || opcode == 0xf3 || opcode == 0xf4) {
        const auto label = opcode == 0xf3 ? "Rest" : opcode == 0xf4 ? "Wait" : (opcode & 0x20) ? "Note" : "Pitch / Tie";
        return cursor.command(label, opcode < 0xf0 ? SequenceSemantic::Note : SequenceSemantic::Rest)
            .invokeFlow<&Playback::timed>({opcode, cursor.u8("duration")});
      }
      if (opcode == 0xf0) {
        return cursor.command("End Pattern", SequenceSemantic::Return).return_();
      }
      if (opcode == 0xf1 || opcode == 0xf2) {
        return cursor.command(opcode == 0xf1 ? "Add Note Volume" : "Note Volume", SequenceSemantic::Level)
            .invoke<&Playback::volume>({cursor.u8("volume"), opcode == 0xf1, false});
      }
    } else if (layer == Layer::Track) {
      switch (opcode) {
        case 0x00:
          return cursor.command("End Track", SequenceSemantic::End).invokeFlow<&Playback::end>().end();
        case 0x02:
          return cursor.command("Restart Track", SequenceSemantic::Loop).loopCandidate(start);
        case 0x04: {
          return cursor.command("Track Tick Divisor", SequenceSemantic::Tempo)
              .invoke<&Playback::divisor>({cursor.u8("frames per tick")});
        }
        case 0x06: {
          return cursor.command("Track Transpose", SequenceSemantic::Pitch)
              .invoke<&Playback::transpose>({cursor.u16le("pitch offset (1/20 semitone)"), false});
        }
        case 0x08:
          return call(cursor, offset, 0x16, Layer::List, "Pattern List");
        case 0x0a: {
          auto event = cursor.command("Voice Assignment", SequenceSemantic::Unknown);
          const u8 voice = cursor.u8("voice (255 = allocate)");
          const u8 mask = voice == 0xff ? cursor.u8("allowed voices") : 0;
          return event.invokeFlow<&Playback::assignVoice>({voice, mask});
        }
      }
    } else if (layer == Layer::List) {
      switch (opcode) {
        case 0x00:
        case 0x16:
          return cursor.command("End Pattern List", SequenceSemantic::Return).return_();
        case 0x02:
          return call(cursor, offset, 0x10, Layer::Pattern, "Note Pattern");
        case 0x04:
        case 0x14:
          // The host polling loop advances the RNG independently of music ticks;
          // sequence bytes alone do not determine which pattern to compile.
          return invalid(cursor, offset, "SculptSoftSnes randomized patterns are not supported");
        case 0x06: {
          return cursor.command("Pattern Transpose", SequenceSemantic::Pitch)
              .invoke<&Playback::transpose>({cursor.u16le("pitch offset (1/20 semitone)"), true});
        }
        case 0x08:
        case 0x0a: {
          auto event = cursor.command(opcode == 0x08 ? "Primary Instrument" : "Secondary Instrument",
                                      SequenceSemantic::Instrument);
          const u8 patch = cursor.u8("instrument", SemanticOperandRole::Instrument);
          if (referencedPrograms) {
            referencedPrograms->insert(patch);
          }
          return event.invoke<&Playback::instrument>({patch, opcode == 0x0a});
        }
        case 0x0c:
          return cursor.command("Enable Pitched Notes", SequenceSemantic::Pitch)
              .invoke<&Playback::pitchMode>({true, u16{0}});
        case 0x0e: {
          return cursor.command("Fixed Pitch", SequenceSemantic::Pitch)
              .invoke<&Playback::pitchMode>({false, cursor.u16le("pitch (1/20 semitone)")});
        }
        case 0x10:
        case 0x12: {
          return cursor.command(opcode == 0x10 ? "Add Base Volume" : "Base Volume", SequenceSemantic::Level)
              .invoke<&Playback::volume>({cursor.u8("volume"), opcode == 0x10, true});
        }
      }
    }
    return invalid(cursor, offset, fmt::format("Invalid SculptSoftSnes early opcode ${:02X}", opcode));
  }
};

}  // namespace

TrackProgram decodeEarlyTrack(const TrackDecodeScope& scope, const Layout& layout, u32 number, u32 start,
                              std::vector<Diagnostic>* diagnostics, std::set<u8>* referencedPrograms) {
  Decoder decoder{scope.reader, layout, Address{start}, diagnostics, referencedPrograms, {{start, Layer::Track}}};
  auto session = scope.begin(number, start);
  std::map<u32, Layer> visited;
  while (!decoder.pending.empty() && visited.size() < kCommandLimit) {
    auto [offset, layer] = decoder.pending.back();
    decoder.pending.pop_back();
    while (scope.reader.has(offset, 1) && visited.size() < kCommandLimit) {
      const auto [position, inserted] = visited.emplace(offset, layer);
      if (!inserted) {
        if (position->second != layer && diagnostics) {
          diagnostics->push_back(Diagnostic{.severity = Severity::Warning,
                                            .message = "Conflicting SculptSoftSnes pattern interpretations",
                                            .range = scope.reader.range(offset, 1)});
        }
        break;
      }
      auto command = decoder.decode(offset, layer);
      const auto next = command.flow.discoveryContinuation();
      session.findOrAppend(std::move(command), offset);
      if (!next || next->value <= offset) {
        break;
      }
      offset = next->value;
      if (layer == Layer::Pattern) {
        layer = Layer::Notes;
      }
    }
  }
  return session.finish();
}

SequenceRuntime makeEarlyRuntime(RuntimeConfig config) {
  return makeCompiledRuntime<Playback, ProgramState>(std::move(config));
}

}  // namespace vgmtrans::formats::sculpt_soft_snes
