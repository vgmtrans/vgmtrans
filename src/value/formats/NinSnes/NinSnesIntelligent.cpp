/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnesSequencePrivate.h"

#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <utility>

// Intelligent Systems extends the shared player with voice definitions,
// writable percussion tables, and alternate note parameters. FE3 can switch
// parameter encodings during playback; FE4 always uses separate table indices
// for duration and velocity. Ordinary notes and controllers use the shared player.
namespace vgmtrans::formats::nin_snes::sequence {

namespace {

constexpr std::array<u8, 16> kVolumeIntelli{
    0x19, 0x32, 0x4c, 0x65, 0x72, 0x7f, 0x8c, 0x98, 0xa5, 0xb2, 0xbf, 0xcb, 0xd8, 0xe5, 0xf2, 0xfc,
};
constexpr std::array<u8, 8> kDurationIntelli{0x32, 0x65, 0x7f, 0x98, 0xb2, 0xcb, 0xe5, 0xfc};
constexpr std::array<u8, 64> kIntelliFe3{
    0x00, 0x0c, 0x19, 0x26, 0x33, 0x3f, 0x4c, 0x59, 0x66, 0x72, 0x75, 0x77, 0x70, 0x7c, 0x7f, 0x82,
    0x84, 0x87, 0x89, 0x8c, 0x8e, 0x91, 0x93, 0x96, 0x99, 0x9b, 0x9e, 0xa0, 0xa3, 0xa5, 0xa8, 0xaa,
    0xad, 0xaf, 0xb2, 0xb5, 0xb7, 0xba, 0xbc, 0xbf, 0xc1, 0xc4, 0xc6, 0xc9, 0xcc, 0xce, 0xd1, 0xd3,
    0xd6, 0xd8, 0xdb, 0xdd, 0xe0, 0xe2, 0xe5, 0xe8, 0xea, 0xed, 0xef, 0xf2, 0xf4, 0xf7, 0xf9, 0xfc,
};
constexpr std::array<u8, 64> kIntelliFe4{
    0x19, 0x26, 0x33, 0x3f, 0x4c, 0x59, 0x66, 0x6d, 0x70, 0x72, 0x75, 0x77, 0x70, 0x7c, 0x7f, 0x82,
    0x84, 0x87, 0x89, 0x8c, 0x8e, 0x91, 0x93, 0x96, 0x99, 0x9b, 0x9e, 0xa0, 0xa3, 0xa5, 0xa8, 0xaa,
    0xad, 0xaf, 0xb2, 0xb5, 0xb7, 0xba, 0xbc, 0xbf, 0xc1, 0xc4, 0xc6, 0xc9, 0xcc, 0xce, 0xd1, 0xd3,
    0xd6, 0xd8, 0xdb, 0xdd, 0xe0, 0xe2, 0xe5, 0xe8, 0xea, 0xed, 0xef, 0xf2, 0xf4, 0xf7, 0xf9, 0xfc,
};

}  // namespace

void configureIntelligentCommands(Definition& definition, IntelliMode mode) {
  if (mode == IntelliMode::Fe3) {
    loadStandardCommands(definition.events, 0xd6);
    definition.events[0xf1] = EventType::ChannelEchoOn;
    definition.events[0xf2] = EventType::ChannelEchoOff;
    definition.events[0xf3] = EventType::IntelliLegatoOn;
    definition.events[0xf4] = EventType::IntelliLegatoOff;
    definition.events[0xf5] = EventType::IntelliFe3F5;
    definition.events[0xf6] = EventType::IntelliWritePort;
    definition.events[0xf7] = EventType::IntelliConditionalJump;
    definition.events[0xf8] = EventType::IntelliJump;
    definition.events[0xf9] = EventType::IntelliFe3Percussion;
    definition.events[0xfa] = EventType::IntelliDefineVoice;
    definition.events[0xfb] = EventType::IntelliLoadVoice;
    definition.events[0xfc] = EventType::Adsr;
    definition.events[0xfd] = EventType::IntelliGainDurationRate;
    useDefault(definition.volume, kVolumeIntelli);
    useDefault(definition.duration, kDurationIntelli);
    useDefault(definition.intelliDuration, kIntelliFe3);
    useDefault(definition.intelliVolume, kIntelliFe3);
  } else if (mode == IntelliMode::Ta || mode == IntelliMode::Fe4) {
    loadStandardCommands(definition.events, 0xda);
    definition.events[0xf5] = EventType::ChannelEchoOn;
    definition.events[0xf6] = EventType::ChannelEchoOff;
    definition.events[0xf7] = mode == IntelliMode::Ta ? EventType::Adsr : EventType::IntelliGain;
    definition.events[0xf8] =
        mode == IntelliMode::Ta ? EventType::IntelliGainDurationRate : EventType::IntelliGain;
    definition.events[0xf9] =
        mode == IntelliMode::Ta ? EventType::IntelliGainDuration : EventType::IntelliReleaseGainOff;
    definition.events[0xfa] = EventType::IntelliDefineVoice;
    definition.events[0xfb] = EventType::IntelliLoadVoice;
    definition.events[0xfc] = EventType::IntelliCustomPercussion;
    definition.events[0xfd] =
        mode == IntelliMode::Ta ? EventType::IntelliTaSubevent : EventType::IntelliFe4Subevent;
    if (mode == IntelliMode::Ta) {
      useDefault(definition.volume, kVolumeIntelli);
      useDefault(definition.duration, kDurationIntelli);
    } else {
      useDefault(definition.intelliDuration, kIntelliFe4);
      useDefault(definition.intelliVolume, kIntelliFe4);
    }
  }
}

IntelligentConfig readIntelligentConfig(ByteReader reader, const Layout& layout) {
  const Profile& selected = profile(layout.profile);
  IntelligentConfig config;
  if (layout.profile == ProfileId::IntelliFe3 && reader.has(0xb9, 1)) {
    config.conditionalMask = reader.u8At(0xb9);
  }
  config.transposeTable = layout.intelliTransposeTable;
  if (layout.intelliPercussionTableAddress) {
    const u8 count = selected.intelli == IntelliMode::Fe3 ? 12 : kIntelliDrumSlots;
    const u32 address = *layout.intelliPercussionTableAddress;
    if (reader.has(address, count * 3)) {
      for (u8 slot = 0; slot < count; ++slot) {
        config.percussionTable[slot] = PercussionEntry{
            reader.u8At(address + slot), reader.u8At(address + count + slot),
            reader.u8At(address + count * 2 + slot)};
      }
    }
  }
  return config;
}

IntelligentState::IntelligentState(const IntelligentConfig& config)
    : conditionalMask(config.conditionalMask), transposeTable(config.transposeTable),
      initialPercussionTable(config.percussionTable) {}

void IntelligentState::reset() {
  flags = 0;
  voiceTable.clear();
  percussionTable = initialPercussionTable;
  instrumentOverrides.clear();
}

bool IntelligentState::usesCustomPercussion(IntelliMode mode) const {
  // FE4 always reads its percussion table; FE3 and TA can select the
  // ordinary percussion-base path using their flags command.
  return mode == IntelliMode::Fe3 ? (flags & 1) == 0
         : mode == IntelliMode::Fe4 || (flags & 0x40) != 0;
}

void ProgramState::registerOverride(u8 logical, u8 srcn, u8 adsr1, u8 adsr2, u8 gain, u8 pitchHigh, u8 pitchLow,
                                    SourceRange source) {
  const auto key = std::tuple{logical, srcn, adsr1, adsr2, gain, pitchHigh, pitchLow};
  const auto [entry, inserted] = intelligent.instrumentOverrides.try_emplace(
      key, 0x80u + static_cast<u32>(intelligent.instrumentOverrides.size()));
  const u32 program = entry->second;
  programs[logical] = program;
  if (!inserted) {
    return;
  }
  instrumentEnvelopes[program] = EnvelopeRegisters{adsr1, adsr2, gain};
  if (collecting) {
    recipes.overrides.push_back(InstrumentOverride{
        .program = program,
        .tuningProgram = logical,
        .srcn = srcn,
        .adsr1 = adsr1,
        .adsr2 = adsr2,
        .gain = gain,
        .pitchHigh = pitchHigh,
        .pitchLow = pitchLow,
        .source = source,
    });
  }
}

u32 ProgramState::intelliPercussionProgram(u8 slot, u8 percussionMinimum) const {
  const u8 patch = intelligent.usesCustomPercussion(selected.intelli)
                       ? intelligent.percussionTable[slot].patch & (selected.intelli == IntelliMode::Fe4 ? 0x3f : 0xbf)
                       : percussionMinimum + slot;
  return resolveProgram(patch, percussionMinimum);
}

u8 ProgramState::ensureIntelliDrumKit(u8 percussionMinimum, s16 transpose) {
  DrumKit candidate;
  const u8 slots = selected.intelli == IntelliMode::Fe3 ? 12 : kIntelliDrumSlots;
  candidate.slots.reserve(slots);
  for (u8 slot = 0; slot < slots; ++slot) {
    const u8 note = intelligent.usesCustomPercussion(selected.intelli) ? intelligent.percussionTable[slot].note : 0xa4;
    candidate.slots.push_back(DrumSlot{
        .key = static_cast<u8>(0x24 + slot),
        .sourceProgram = intelliPercussionProgram(slot, percussionMinimum),
        .sourceKey = static_cast<s16>((note & 0x7f) + kMelodicKeyCorrection + transpose),
    });
  }
  const auto found =
      std::ranges::find_if(recipes.drumKits, [&](const DrumKit& kit) { return kit.slots == candidate.slots; });
  if (found != recipes.drumKits.end()) {
    return found->program;
  }
  if (!collecting || recipes.drumKits.size() >= 0x80) {
    return recipes.drumKits.empty() ? 0 : recipes.drumKits.back().program;
  }
  candidate.program = static_cast<u8>(recipes.drumKits.size());
  recipes.drumKits.push_back(std::move(candidate));
  return recipes.drumKits.back().program;
}

void Playback::intelliParameter(u8 raw, u8 resolved) {
  if (raw < 0x40) {
    track.durationRate = resolved;
  } else {
    track.velocity = resolved;
  }
}

void Playback::fe3CustomParameter(u8 raw, u8 resolved) {
  if ((program.intelligent.flags & 0x80) != 0) {
    intelliParameter(raw, resolved);
  }
}

void Playback::fe3StandardParameter(bool present, u8 durationRate, u8 velocity) {
  if (present && (program.intelligent.flags & 0x80) == 0) {
    track.durationRate = durationRate;
    track.velocity = velocity;
  }
}

Effects Playback::fe3ParameterFlow(Address standardDestination, Address customDestination) {
  return vm.jump((program.intelligent.flags & 0x80) != 0 ? customDestination : standardDestination);
}

Effects Playback::intelligentPercussion(u8 slot, u8 percussionMinimum) {
  const u8 duration = soundingDuration();
  const bool custom = program.intelligent.usesCustomPercussion(program.selected.intelli);
  const PercussionEntry entry = program.intelligent.percussionTable[slot];
  loadInstrumentEnvelope(program.intelliPercussionProgram(slot, percussionMinimum));
  if (custom && entry.pan < 0x80) {
    pan(entry.pan);
  }
  if (custom) {
    channelEcho((entry.patch & 0x40) != 0);
  }
  const u8 kit = program.ensureIntelliDrumKit(percussionMinimum, track.transpose + program.globalTranspose);
  switchToDrumProgram(kit);
  const double key = 0x24 + slot - program.globalTranspose;
  beginNotePitch(static_cast<u8>(0x24 + slot - program.globalTranspose));
  emitVoiceNote(key, duration);
  return Effects::wait(track.noteLength);
}

void Playback::defineVoiceTable(u8 size) { program.intelligent.voiceTable.assign(size, VoiceRecord{}); }

void Playback::defineVoice(u8 index, u8 instrument, u8 volume, u8 pan, u8 tuningTranspose) {
  if (index < program.intelligent.voiceTable.size()) {
    program.intelligent.voiceTable[index] = VoiceRecord{
        .instrument = instrument,
        .volume = volume,
        .pan = pan,
        .tuningTranspose = tuningTranspose,
    };
  }
}

void Playback::overwriteInstrument(u8 logical, u8 srcn, u8 adsr1, u8 adsr2, u8 gain, u8 pitchHigh, u8 pitchLow) {
  // FA writes the shared RAM table. DSP registers change only when a
  // channel next selects that instrument (D6/DA/FB or percussion).
  program.registerOverride(logical, srcn, adsr1, adsr2, gain, pitchHigh, pitchLow, vm.sourceRange());
}

void Playback::loadVoice(u8 index, u8 percussionMinimum, IntelliMode mode) {
  // The declared table is the only typed data boundary; bytes belonging to
  // following commands are not silently reinterpreted as voice records.
  const u8 slot = index & 0x3f;  // The driver computes an eight-bit index * 4.
  if (slot >= program.intelligent.voiceTable.size()) {
    return;
  }
  const VoiceRecord& record = program.intelligent.voiceTable[slot];
  volume(record.volume);
  const u8 panValue = mode == IntelliMode::Fe3 ? record.pan : record.pan & 0x1f;
  pan(panValue);

  s8 transpose = track.transpose;
  if (mode == IntelliMode::Fe3) {
    constexpr std::array<s8, 7> transposes{-24, -12, -1, 0, 1, 12, 24};
    const u8 tuning = record.tuningTranspose & 0x0f;
    const u8 transposeIndex = (record.tuningTranspose >> 4) & 7;
    if (tuning != 0) {
      out.tuning(((tuning - 1) * 5 / 256.0) * 100.0);
    }
    if (transposeIndex != 0) {
      transpose = program.intelligent.transposeTable.size() == 7
                      ? static_cast<s8>(program.intelligent.transposeTable[transposeIndex - 1])
                      : transposes[transposeIndex - 1];
    }
  } else {
    const double tuningCents = (((record.pan >> 5) & 7) * 5 / 256.0) * 100.0;
    transpose = static_cast<s8>(record.tuningTranspose);
    out.tuning(tuningCents);
    if ((index & 0x80) != 0) {
      vibratoOff();
    }
    if (mode == IntelliMode::Fe4 && (index & 0x40) != 0) {
      track.pitch.motion.clear();
    }
  }
  track.transpose = transpose;
  melodicProgram(record.instrument, percussionMinimum);
}

void Playback::intelliGain(u8 gain) {
  track.envelope.gain = gain;
  if ((track.envelope.adsr1 & 0x80) == 0 && (gain & 0x80) == 0) {
    out.replaceEnvelope(snesDspEnvelope(track.envelope.adsr1, track.envelope.adsr2, gain),
                        VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }
}

void Playback::percussionEntry(u8 slot, u8 patch, u8 note, u8 pan) {
  if (slot < program.intelligent.percussionTable.size()) {
    program.intelligent.percussionTable[slot] = PercussionEntry{patch, note, pan};
  }
}

void Playback::enableCustomPercussion() { program.intelligent.flags |= 0x40; }

void Playback::intelliFlags(u8 mask, bool enabled) {
  if (enabled) {
    program.intelligent.flags |= mask;
  } else {
    program.intelligent.flags &= static_cast<u8>(~mask);
  }
}

void Playback::fe3Flags(u8 param) {
  if (param < 0xf0) {
    return;
  }
  intelliFlags(static_cast<u8>(1u << (param & 7)), (param & 8) == 0);
}

Effects Playback::intelliConditionalJump(Address destination) {
  const u8 channel = static_cast<u8>(1u << track.trackNumber);
  return (program.intelligent.conditionalMask & channel) == 0 ? vm.jump(destination) : Effects{};
}

DecodedBytecodeCommand decodeIntelligentNoteParameters(Cursor& cursor, const DecodeContext& context, u32 begin) {
  auto event = cursor.command("Note Parameters", SequenceSemantic::State);
  const u8 duration = event.opcodeValue("duration", cursor.opcode(), SourceValueDisplay::Decimal);
  event.set<&TrackState::noteLength>(duration);
  std::vector<std::pair<u8, u8>> parameters;
  while (event.peekU8() <= 0x7f && parameters.size() < 0x80) {
    const u8 raw = event.u8(fmt::format("parameter_{}", parameters.size() + 1), SourceValueDisplay::Hex);
    const auto& table = raw < 0x40 ? context.definition.intelliDuration : context.definition.intelliVolume;
    const u8 resolved = table[raw & 0x3f];
    event.derived(fmt::format("resolved_{}", parameters.size() + 1), resolved);
    parameters.emplace_back(raw, resolved);
    // A velocity byte terminates the parameter list. Only duration bytes
    // loop back to read another parameter in both FE3 and FE4.
    if (raw >= 0x40) {
      break;
    }
  }

  if (context.selected.intelli != IntelliMode::Fe3) {
    for (const auto& [raw, resolved] : parameters) {
      event.invoke<&Playback::intelliParameter>(raw, resolved);
    }
    return event;
  }

  // FE3 can switch between the ordinary packed byte and its variable-length
  // parameter stream at runtime. Decode both exits and let the semantic action
  // choose one; this is the only overlapping command shape in the driver.
  const bool hasPacked = !parameters.empty();
  const u8 packed = hasPacked ? parameters.front().first : 0;
  const u8 standardDuration = hasPacked ? context.definition.duration[(packed >> 4) & 7] : 0;
  const u8 standardVelocity = hasPacked ? context.definition.volume[packed & 15] : 0;
  event.invoke<&Playback::fe3StandardParameter>(hasPacked, standardDuration, standardVelocity);
  for (const auto& [raw, resolved] : parameters) {
    event.invoke<&Playback::fe3CustomParameter>(raw, resolved);
  }
  const Address standardDestination{begin + 1 + (hasPacked ? 1u : 0u)};
  const Address customDestination = event.nextAddress();
  event.derived("standard_destination", standardDestination, SourceValueDisplay::Address,
                SemanticOperandRole::JumpTarget);
  event.derived("custom_destination", customDestination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
  event.invoke<&Playback::fe3ParameterFlow>(standardDestination, customDestination);
  return event.discoverTarget(standardDestination);
}

std::optional<DecodedBytecodeCommand> decodeIntelligentCommand(Cursor& cursor, const DecodeContext& context,
                                                              EventType type) {
  switch (type) {
    case EventType::IntelliLegatoOn:
      return cursor.command("Legato On", SequenceSemantic::State).invoke<&Playback::legato>(true);
    case EventType::IntelliLegatoOff:
      return cursor.command("Legato Off", SequenceSemantic::State).invoke<&Playback::legato>(false);
    case EventType::IntelliConditionalJump: {
      auto event = cursor.command("Conditional Short Jump", SequenceSemantic::Jump);
      const u8 distance = event.u8("distance");
      const Address destination{event.nextAddress().value + distance};
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      return event.invoke<&Playback::intelliConditionalJump>(destination).discoverTarget(destination);
    }
    case EventType::IntelliJump: {
      auto event = cursor.command("Short Jump", SequenceSemantic::Jump);
      const u8 distance = event.u8("distance");
      const Address destination{event.nextAddress().value + distance};
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      return event.jump(destination);
    }
    case EventType::IntelliFe3F5: {
      auto event = cursor.command("FE3 Flags / Port Wait", SequenceSemantic::State);
      return event.invoke<&Playback::fe3Flags>(event.u8("parameter", SourceValueDisplay::Hex));
    }
    case EventType::IntelliWritePort: {
      auto event = cursor.sourceOnly("Write APU Port");
      event.u8("value");
      return event;
    }
    case EventType::IntelliFe3Percussion: {
      auto event = cursor.command("Custom Percussion Table", SequenceSemantic::State);
      std::array<u8, 12> patches{};
      std::array<u8, 12> notes{};
      for (u8 slot = 0; slot < 12; ++slot) {
        patches[slot] = event.u8(fmt::format("patch_{}", slot), SourceValueDisplay::Hex);
      }
      for (u8 slot = 0; slot < 12; ++slot) {
        notes[slot] = event.u8(fmt::format("note_{}", slot), SourceValueDisplay::Hex);
      }
      for (u8 slot = 0; slot < 12; ++slot) {
        const u8 pan = event.u8(fmt::format("pan_{}", slot), SourceValueDisplay::Hex);
        event.invoke<&Playback::percussionEntry>(slot, patches[slot], notes[slot], pan);
      }
      return event;  // F5 selects the mode; F9 only copies the three arrays.
    }
    case EventType::IntelliDefineVoice: {
      auto event = cursor.command("Voice Parameter Definition", SequenceSemantic::Program);
      const s8 parameter = event.s8("count_or_instrument", SourceValueDisplay::SignedDecimal);
      if (parameter >= 0 || !context.layout.intelliInstrumentOverwrite) {
        const u8 count = static_cast<u8>(parameter) & 0x3f;
        event.invoke<&Playback::defineVoiceTable>(count);
        for (u8 index = 0; index < count; ++index) {
          const u8 instrument = event.u8(fmt::format("instrument_{}", index), SemanticOperandRole::Instrument);
          const u8 volume = event.u8(fmt::format("volume_{}", index));
          const u8 pan = event.u8(fmt::format("pan_{}", index));
          const u8 tuningTranspose = event.u8(fmt::format("tuning_transpose_{}", index));
          event.invoke<&Playback::defineVoice>(index, instrument, volume, pan, tuningTranspose);
        }
        return event;
      }
      if (context.selected.intelli != IntelliMode::Fe3 && context.selected.intelli != IntelliMode::Ta) {
        return event.ignore();
      }
      const u8 logical = static_cast<u8>(parameter) & 0x3f;
      const u8 srcn = event.u8("srcn", SourceValueDisplay::Hex);
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
      const u8 gain = event.u8("gain", SourceValueDisplay::Hex);
      const u8 pitchHigh = event.u8("pitch_high", SourceValueDisplay::Hex);
      const u8 pitchLow = event.u8("pitch_low", SourceValueDisplay::Hex);
      return event.invoke<&Playback::overwriteInstrument>(logical, srcn, adsr1, adsr2, gain, pitchHigh, pitchLow);
    }
    case EventType::IntelliLoadVoice: {
      auto event = cursor.command("Load Voice Parameters", SequenceSemantic::Program);
      const u8 index = event.u8("index");
      return event.invoke<&Playback::loadVoice>(index, context.definition.status.percussionMin,
                                                context.selected.intelli);
    }
    case EventType::IntelliGainDurationRate: {
      auto event = cursor.command("GAIN Duration Rate", SequenceSemantic::State);
      event.u8("duration_rate");
      const u8 gain = event.u8("gain", SourceValueDisplay::Hex);
      event.invoke<&Playback::intelliGain>(gain);
      // This is the independent counter for switching ADSR to GAIN,
      // not the note's key-off duration rate.
      return event;
    }
    case EventType::IntelliGainDuration: {
      auto event = cursor.command("GAIN Duration", SequenceSemantic::State);
      event.u8("duration_rate");
      return event.ignore();
    }
    case EventType::IntelliReleaseGainOff:
      // FE4's zero-length F9 enters the release-GAIN store with A=0.
      return cursor.sourceOnly("Clear Release GAIN");
    case EventType::IntelliGain: {
      auto event = cursor.command("GAIN", SequenceSemantic::State);
      return event.invoke<&Playback::intelliGain>(event.u8("gain", SourceValueDisplay::Hex));
    }
    case EventType::IntelliCustomPercussion: {
      auto event = cursor.command("Custom Percussion Table", SequenceSemantic::State);
      const u8 packedCount = event.u8("packed_count", SourceValueDisplay::Hex);
      const u8 count = static_cast<u8>((packedCount & 0x0f) + 1);
      event.derived("count", count);
      // MUL leaves the high byte as the first slot; shorter writes keep the rest.
      const u8 firstSlot = static_cast<u8>((packedCount * 3) >> 8);
      event.derived("first_slot", firstSlot);
      for (u8 slot = 0; slot < count; ++slot) {
        const u8 patch =
            event.u8(fmt::format("patch_{}", slot), SourceValueDisplay::Hex, SemanticOperandRole::Instrument);
        const u8 note = event.u8(fmt::format("note_{}", slot), SourceValueDisplay::MidiNote);
        const u8 pan = event.u8(fmt::format("pan_{}", slot));
        event.invoke<&Playback::percussionEntry>(static_cast<u8>(firstSlot + slot), patch, note, pan);
      }
      return event.invoke<&Playback::enableCustomPercussion>();
    }
    case EventType::IntelliTaSubevent:
    case EventType::IntelliFe4Subevent: {
      auto event = cursor.command("Intelligent Systems Subevent", SequenceSemantic::State);
      const u8 subtype = event.u8("subtype", SourceValueDisplay::Hex);
      if (type == EventType::IntelliTaSubevent && subtype == 0) {
        event.u16le("request_value", SourceValueDisplay::Hex);
        event.u8("request_type", SourceValueDisplay::Hex);
        return event.ignore();
      }
      if (subtype == 1 || subtype == 2) {
        const u8 mask = event.u8("mask", SourceValueDisplay::Hex);
        return event.invoke<&Playback::intelliFlags>(mask, subtype == 1);
      }
      if (type == EventType::IntelliTaSubevent && subtype == 3) {
        return event.invoke<&Playback::legato>(true);
      }
      if (type == EventType::IntelliTaSubevent && subtype == 4) {
        return event.invoke<&Playback::legato>(false);
      }
      if (type == EventType::IntelliTaSubevent && subtype == 5) {
        event.u8("global_byte", SourceValueDisplay::Hex);
      }
      return event.ignore();
    }
    default:
      return std::nullopt;
  }
}

}  // namespace vgmtrans::formats::nin_snes::sequence
