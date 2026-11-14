#include "NinSnesSeq.h"

#include "base/Types.h"
#include "SeqEvent.h"

#include <array>
#include <vector>

#include "spdlog/fmt/fmt.h"

namespace {

constexpr std::array<const char*, 8> kAddmusicKHotPatchPresetNames = {
    "AddmusicK 1.0.8 and earlier",
    "AddmusicK 1.0.9",
    "AddmusicK Beta",
    "Romi's Addmusic404",
    "Addmusic405",
    "AddmusicM",
    "carol's MORE.bin",
    "Vanilla SMW",
};

void appendHotPatchFlags(std::string& desc, const NinSnesAddmusicKHotPatchState& state) {
  bool any = false;
  auto appendFlag = [&](bool active, const char* name) {
    if (!active) {
      return;
    }
    fmt::format_to(std::back_inserter(desc), "{}{}", any ? ", " : "", name);
    any = true;
  };

  desc += "  Active:";
  appendFlag((state.byte0 & NinSnesAddmusicKHotPatchState::ArpeggioSkipsRests) != 0,
             "arpeggio skips rests");
  appendFlag((state.byte0 & NinSnesAddmusicKHotPatchState::GainBeforeAdsr) != 0,
             "GAIN before ADSR");
  appendFlag((state.byte0 & NinSnesAddmusicKHotPatchState::ReadaheadScansLoops) != 0,
             "readahead scans loops");
  appendFlag(state.pitchSlideAccountsForSemitoneTune(),
             "$DD accounts for $FA $02");
  appendFlag(state.sampleLoadClearsPitchFraction(),
             "$F3 clears pitch fraction");
  appendFlag((state.byte0 & NinSnesAddmusicKHotPatchState::ZeroDelayEchoWritesDisabled) != 0,
             "zero-delay echo writes disabled");
  appendFlag((state.byte0 & NinSnesAddmusicKHotPatchState::GlissandoStopsAfterOneNote) != 0,
             "glissando stops after one note");
  appendFlag((state.byte1 & NinSnesAddmusicKHotPatchState::RestsKeyOffOnlyInReadahead) != 0,
             "rests key off only in readahead");
  if (!any) {
    desc += " none";
  }
}

}  // namespace

bool NinSnesAddmusicKHotPatchState::applyPreset(u8 preset) {
  struct Preset {
    u8 byte0;
    u8 byte1;
  };

  // AddmusicKFF asm/Commands.asm HotPatchPresetTable. The high continuation bit
  // is a stream encoding detail, so only the lower seven behavior bits are stored.
  static constexpr std::array<Preset, 8> kPresets = {{
      {0x80, 0x00},  // AddmusicK 1.0.8 and earlier
      {0xff, 0x00},  // AddmusicK 1.0.9
      {0x80, 0x00},  // AddmusicK Beta
      {0x80, 0x01},  // Romi's Addmusic404
      {0x80, 0x01},  // Addmusic405
      {0x88, 0x00},  // AddmusicM
      {0x80, 0x01},  // carol's MORE.bin
      {0x80, 0x01},  // Vanilla SMW
  }};

  if (preset >= kPresets.size()) {
    return false;
  }

  byte0 = kPresets[preset].byte0 & 0x7f;
  byte1 = kPresets[preset].byte1 & 0x7f;
  return true;
}

void NinSnesAddmusicKHotPatchState::applyBytes(std::span<const u8> bytes) {
  byte0 = bytes.empty() ? 0 : bytes[0] & 0x7f;
  byte1 = bytes.size() < 2 ? 0 : bytes[1] & 0x7f;
}

bool NinSnesTrack::handleAddmusicKEvent(NinSnesSeqEventType eventType, u32 beginOffset,
                                        std::string& desc) {
  auto& parentSeq = seq();

  switch (eventType) {
    case EVENT_ADDMUSICK_SUBLOOP: {
      const u8 count = readByte(curOffset++);
      if (count == 0) {
        state.addmusicKSubloopStartAddress = curOffset;
        state.addmusicKSubloopCount = 0;
        state.addmusicKSubloopActive = false;
        desc = fmt::format("Start: ${:04X}", state.addmusicKSubloopStartAddress);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Subloop Start", desc, Type::RepeatStart);
      } else {
        if (!state.addmusicKSubloopActive) {
          state.addmusicKSubloopCount = count;
          state.addmusicKSubloopActive = true;
        }

        desc = fmt::format("Start: ${:04X}  Remaining: {:d}", state.addmusicKSubloopStartAddress,
                           state.addmusicKSubloopCount);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Subloop End", desc, Type::RepeatEnd);

        if (state.addmusicKSubloopStartAddress != 0 && state.addmusicKSubloopCount != 0) {
          state.addmusicKSubloopCount--;
          curOffset = state.addmusicKSubloopStartAddress;
        } else {
          state.addmusicKSubloopActive = false;
        }
      }
      return true;
    }

    case EVENT_ADDMUSICK_ADSR_GAIN: {
      const u8 adsrOrGain = readByte(curOffset++);
      const u8 value = readByte(curOffset++);
      if (adsrOrGain == 0x80) {
        desc = fmt::format("GAIN: ${:02X}", value);
        addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN", desc, Type::Adsr);
      } else {
        desc = fmt::format("ADSR(1): ${:02X}  ADSR(2): ${:02X}", adsrOrGain, value);
        addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      }
      return true;
    }

    case EVENT_ADDMUSICK_SAMPLE_LOAD: {
      const u8 sample = readByte(curOffset++);
      const u8 pitchMultiplier = readByte(curOffset++);
      desc = fmt::format("Sample: ${:02X}  Pitch Multiplier: ${:02X}", sample, pitchMultiplier);
      if (parentSeq.addmusicKHotPatch.sampleLoadClearsPitchFraction()) {
        desc += "  Hot Patch: clears pitch fraction";
      }
      addGenericEvent(beginOffset, curOffset - beginOffset, "Sample Load", desc, Type::Sample);
      return true;
    }

    case EVENT_ADDMUSICK_OPTION: {
      const u8 option = readByte(curOffset++);
      std::string name = "AddmusicK Option";
      switch (option) {
        case 0x00:
        case 0x06:
          name = "Yoshi Drums";
          break;
        case 0x01:
          name = "Legato";
          intelliLegato = !intelliLegato;
          break;
        case 0x02:
          name = "Light Staccato";
          break;
        case 0x03:
          name = "Echo Toggle";
          break;
        case 0x05:
          name = "SNES Sync";
          break;
        case 0x07:
          name = "Tempo Hike Off";
          break;
        case 0x08:
          name = "Velocity Table";
          break;
        case 0x09:
          name = "Restore Instrument";
          break;
        default:
          break;
      }
      desc = fmt::format("Subcommand: ${:02X}", option);
      addGenericEvent(beginOffset, curOffset - beginOffset, name, desc, Type::ChangeState);
      return true;
    }

    case EVENT_ADDMUSICK_FIR_FILTER: {
      desc = "Coefficients:";
      for (u8 coeffIndex = 0; coeffIndex < 8; coeffIndex++) {
        fmt::format_to(std::back_inserter(desc), " ${:02X}", readByte(curOffset++));
      }
      addGenericEvent(beginOffset, curOffset - beginOffset, "FIR Filter", desc, Type::Reverb);
      return true;
    }

    case EVENT_ADDMUSICK_DSP_WRITE: {
      const u8 reg = readByte(curOffset++);
      const u8 value = readByte(curOffset++);
      desc = fmt::format("Register: ${:02X}  Value: ${:02X}", reg, value);
      addGenericEvent(beginOffset, curOffset - beginOffset, "DSP Write", desc, Type::Control);
      return true;
    }

    case EVENT_ADDMUSICK_DATA_WRITE: {
      const u8 addrHigh = readByte(curOffset++);
      const u8 addrLow = readByte(curOffset++);
      const u8 value = readByte(curOffset++);
      desc = fmt::format("Address: ${:02X}{:02X}  Value: ${:02X}  Legacy AddmusicM command",
                         addrHigh, addrLow, value);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Legacy Data Write", desc, Type::Misc);
      return true;
    }

    case EVENT_ADDMUSICK_NOISE: {
      const u8 noisePitch = readByte(curOffset++);
      desc = fmt::format("Pitch: ${:02X}", noisePitch);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise", desc, Type::Noise);
      return true;
    }

    case EVENT_ADDMUSICK_DATA_SEND: {
      const u8 byte1 = readByte(curOffset++);
      const u8 byte2 = readByte(curOffset++);
      desc = fmt::format("Byte 1: ${:02X}  Byte 2: ${:02X}", byte1, byte2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Data Send", desc, Type::Misc);
      return true;
    }

    case EVENT_ADDMUSICK_EXTENDED: {
      const u8 subcommand = readByte(curOffset++);
      const u8 value = readByte(curOffset++);
      std::string name = "AddmusicK Extended";
      switch (subcommand) {
        case 0x00:
          name = "Pitch Modulation";
          desc = fmt::format("Channels: ${:02X}", value);
          break;
        case 0x01:
          name = "GAIN";
          desc = fmt::format("GAIN: ${:02X}", value);
          addGenericEvent(beginOffset, curOffset - beginOffset, name, desc, Type::Adsr);
          return true;
        case 0x02: {
          const s8 semitones = static_cast<s8>(value);
          name = "Semitone Tune";
          desc = fmt::format("Semitones: {:d}", semitones);
          state.spcTranspose = semitones;
          addTranspose(beginOffset, curOffset - beginOffset, semitones, name);
          return true;
        }
        case 0x03:
          name = "Amplify";
          desc = fmt::format("Value: ${:02X}", value);
          state.addmusicKVolumeMultiplier = value;
          addVolNoItem(midiVolumeForCurrentState());
          break;
        case 0x04:
          name = "Echo Buffer Reserve";
          desc = fmt::format("Size: ${:02X}", value);
          break;
        case 0x06:
          name = "Velocity Table";
          parentSeq.addmusicKVelocityTableId = value;
          desc = fmt::format("Table: ${:02X}", value);
          break;
        case 0x7f:
          name = "Hot Patch Preset";
          if (parentSeq.addmusicKHotPatch.applyPreset(value)) {
            desc = fmt::format("Preset: ${:02X} ({})  Bytes: ${:02X} ${:02X}",
                               value,
                               kAddmusicKHotPatchPresetNames[value],
                               parentSeq.addmusicKHotPatch.byte0,
                               parentSeq.addmusicKHotPatch.byte1);
            appendHotPatchFlags(desc, parentSeq.addmusicKHotPatch);
          } else {
            desc = fmt::format("Preset: ${:02X} (reserved/user-defined, not modeled)", value);
          }
          break;
        case 0xfe: {
          name = "Hot Patch Toggle Bits";
          std::vector<u8> patchBytes;
          patchBytes.push_back(value);
          desc = "Bytes:";
          fmt::format_to(std::back_inserter(desc), " ${:02X}", value);
          u8 patchByte = value;
          while ((patchByte & 0x80) != 0 && curOffset < 0x10000) {
            patchByte = readByte(curOffset++);
            patchBytes.push_back(patchByte);
            fmt::format_to(std::back_inserter(desc), " ${:02X}", patchByte);
          }
          parentSeq.addmusicKHotPatch.applyBytes(patchBytes);
          fmt::format_to(std::back_inserter(desc),
                         "  Effective: ${:02X} ${:02X}",
                         parentSeq.addmusicKHotPatch.byte0,
                         parentSeq.addmusicKHotPatch.byte1);
          appendHotPatchFlags(desc, parentSeq.addmusicKHotPatch);
          break;
        }
        default:
          desc = fmt::format("Subcommand: ${:02X}  Value: ${:02X}", subcommand, value);
          break;
      }
      addGenericEvent(beginOffset, curOffset - beginOffset, name, desc, Type::ChangeState);
      return true;
    }

    case EVENT_ADDMUSICK_ARPEGGIO: {
      const u8 subcommand = readByte(curOffset++);
      if (subcommand == 0) {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Arpeggio Off", desc, Type::PitchBend);
      } else if (subcommand == 0x80 || subcommand == 0x81) {
        const u8 duration = readByte(curOffset++);
        const s8 semitones = static_cast<s8>(readByte(curOffset++));
        desc = fmt::format("Duration: {:d}  Semitones: {:d}", duration, semitones);
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        subcommand == 0x80 ? "Trill" : "Glissando", desc, Type::PitchBend);
      } else if (subcommand < 0x80) {
        const u8 count = subcommand;
        const u8 duration = readByte(curOffset++);
        desc = fmt::format("Count: {:d}  Duration: {:d}  Steps:", count, duration);
        for (u8 step = 0; step < count && curOffset < 0x10000; step++) {
          fmt::format_to(std::back_inserter(desc), " ${:02X}", readByte(curOffset++));
        }
        addGenericEvent(beginOffset, curOffset - beginOffset, "Arpeggio", desc, Type::PitchBend);
      } else {
        const u8 duration = readByte(curOffset++);
        const u8 value = readByte(curOffset++);
        desc = fmt::format("Subcommand: ${:02X}  Duration: {:d}  Value: ${:02X}", subcommand,
                           duration, value);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Arpeggio", desc, Type::PitchBend);
      }
      return true;
    }

    case EVENT_ADDMUSICK_REMOTE_COMMAND: {
      const u16 addr = getShortAddress(curOffset);
      curOffset += 2;
      const u8 event = readByte(curOffset++);
      const u8 wait = readByte(curOffset++);
      desc = fmt::format("Address: ${:04X}  Event: {:d}  Wait: {:d}", addr, event, wait);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Remote Command", desc, Type::Misc);
      return true;
    }

    default:
      return false;
  }
}
