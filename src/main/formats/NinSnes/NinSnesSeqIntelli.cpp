#include "base/types.h"
#include "NinSnesSeq.h"

#include "scale_conversion.h"
#include "SeqEvent.h"
#include "spdlog/fmt/fmt.h"

namespace {
struct NinSnesIntelliPercussionNoteState {
  u8 instrumentByte = 0;
  u8 playedNoteByte = 0xa4;
  std::optional<u8> panByte;
  std::optional<u8> reverbLevel;
};

struct NinSnesIntelliVoiceParamRecord {
  u8 instrumentByte = 0;
  u8 volumeByte = 0;
  u8 panValue = 0;
  u8 logicalProgNum = 0;
  u32 resolvedProgNum = 0;
  double fineTuningCents = 0.0;
  s8 transpose = 0;
};

bool isIntelliTablePercussionVersion(NinSnesProfileId profileId) {
  const auto intelliMode = getNinSnesProfile(profileId).intelliMode;
  return intelliMode == NinSnesIntelliModeId::Ta || intelliMode == NinSnesIntelliModeId::Fe4;
}

bool isPackedIntelliVoiceParam(NinSnesIntelliModeId intelliMode) {
  return intelliMode == NinSnesIntelliModeId::Ta || intelliMode == NinSnesIntelliModeId::Fe4;
}

void addIntelliByteChild(VGMItem* parent, u32 offset, const std::string& label,
                         u8 value) {
  parent->addChild(offset, 1, fmt::format("{}: ${:02X}", label, value));
}

template <size_t N>
void addIntelliTableItem(VGMItem* parent, u32 offset, const std::string& name,
                         const std::array<const char*, N>& labels,
                         const std::array<u8, N>& values) {
  VGMItem* item = parent->addChild(offset, static_cast<u32>(N), name);
  for (size_t i = 0; i < N; i++) {
    addIntelliByteChild(item, offset + i, labels[i], values[i]);
  }
}

void updateIntelliFlags(u8& flags, u8 mask, bool enabled) {
  if (enabled) {
    flags |= mask;
  } else {
    flags &= static_cast<u8>(~mask);
  }
}

// Resolves a voice-param record address and reports TA/FE4 out-of-range table access.
bool getIntelliVoiceParamAddress(const NinSnesSeq& seq, u8 index, u32& addrVoiceParam,
                                 bool& outOfRange) {
  addrVoiceParam = 0;
  outOfRange = false;

  if (!seq.intelliVoiceParam.defined) {
    return false;
  }

  addrVoiceParam = seq.intelliVoiceParam.addr + (index * 4u);
  outOfRange = index >= seq.intelliVoiceParam.size;
  const bool packedVoiceParams = isPackedIntelliVoiceParam(seq.profile().intelliMode);
  return (addrVoiceParam + 3) < 0x10000 && (packedVoiceParams || !outOfRange);
}

// Resolves Intelli flag-mask events and applies their side effects.
const char* describeIntelliFlagMaskEvent(NinSnesSeq& seq, u8 param, bool bitValue,
                                         bool allowCustomPercLabel, std::string& desc) {
  updateIntelliFlags(seq.intelliPerc.flags, param, bitValue);

  if (allowCustomPercLabel && param == 0x40) {
    desc = fmt::format("Status: {}", (bitValue ? "On" : "Off"));
    return "Use Custom Percussion Table";
  }

  desc = fmt::format("Mask: ${:02X}", param);
  return bitValue ? "Set Global Flags" : "Clear Global Flags";
}

// Decodes one Intelli voice-param record into resolved playback state.
NinSnesIntelliVoiceParamRecord decodeIntelliVoiceParamRecord(const NinSnesSeq& seq,
                                                             NinSnesIntelliModeId intelliMode,
                                                             u32 addrVoiceParam,
                                                             double baseFineTuningCents,
                                                             s8 currentTranspose) {
  constexpr std::array<s8, 7> FE3_TRANSPOSE_TABLE = {-24, -12, -1, 0, 1, 12, 24};

  NinSnesIntelliVoiceParamRecord record;
  record.instrumentByte = seq.readByte(addrVoiceParam);
  record.volumeByte = seq.readByte(addrVoiceParam + 1);
  const u8 packedPanByte = seq.readByte(addrVoiceParam + 2);
  const u8 lastByte = seq.readByte(addrVoiceParam + 3);

  record.panValue = packedPanByte;
  record.fineTuningCents = baseFineTuningCents;
  record.transpose = currentTranspose;
  record.resolvedProgNum = record.instrumentByte;

  if (isPackedIntelliVoiceParam(intelliMode)) {
    record.panValue = packedPanByte & 0x1f;
    record.fineTuningCents = (((packedPanByte >> 5) & 0x07) * 5 / 256.0) * 100.0;
    record.resolvedProgNum = seq.resolveProgramNumber(record.instrumentByte, &record.logicalProgNum);
  } else {
    record.logicalProgNum = record.instrumentByte;
    if (intelliMode != NinSnesIntelliModeId::Fe3) {
      record.logicalProgNum &= 0x1f;
    }
    record.resolvedProgNum = record.logicalProgNum;
  }

  switch (intelliMode) {
    case NinSnesIntelliModeId::Fe3: {
      const u8 tuningIndex = lastByte & 15;
      const u8 transposeIndex = (lastByte & 0x70) >> 4;

      if (tuningIndex != 0) {
        const u8 newTuning = (tuningIndex - 1) * 5;
        record.fineTuningCents = (newTuning / 256.0) * 100.0;
      }

      if (transposeIndex != 0) {
        record.transpose = FE3_TRANSPOSE_TABLE[transposeIndex - 1];
      }
      break;
    }

    case NinSnesIntelliModeId::Fe4:
    case NinSnesIntelliModeId::Ta:
      record.transpose = static_cast<s8>(lastByte);
      break;

    default:
      break;
  }

  return record;
}

constexpr std::array<const char*, 4> INTELLI_VOICE_PARAM_LABELS = {
    "Instrument",
    "Volume",
    "Pan",
    "Tuning/Transpose",
};

constexpr std::array<const char*, 4> INTELLI_PACKED_VOICE_PARAM_LABELS = {
    "Instrument",
    "Volume",
    "Pan/Tuning",
    "Transpose",
};

constexpr std::array<const char*, 6> INTELLI_OVERWRITE_LABELS = {
    "SRCN/Noise", "ADSR1", "ADSR2", "GAIN", "Pitch Low", "Pitch High",
};

constexpr std::array<const char*, 3> INTELLI_PERCUSSION_ENTRY_LABELS = {
    "Patch",
    "Note",
    "Pan",
};

// Derives the live Intelli percussion slot state, including custom table overrides.
NinSnesIntelliPercussionNoteState getIntelliPercussionNoteState(const NinSnesSeq& seq,
                                                                u8 slot) {
  NinSnesIntelliPercussionNoteState percussionState;
  percussionState.instrumentByte = static_cast<u8>(seq.STATUS_PERCUSSION_NOTE_MIN + slot);

  if (!seq.usesIntelliCustomPercTable()) {
    return percussionState;
  }

  const auto& customEntry = seq.intelliPerc.table[slot];
  percussionState.instrumentByte = customEntry.patchByte & 0xbf;
  percussionState.playedNoteByte = customEntry.noteByte;
  if (customEntry.panByte < 0x80) {
    percussionState.panByte = customEntry.panByte;
  }
  percussionState.reverbLevel = static_cast<u8>((customEntry.patchByte & 0x40) != 0 ? 40 : 0);
  return percussionState;
}
}  // namespace

bool NinSnesSeq::usesIntelliCustomPercTable() const {
  return usesNinSnesIntelliCustomPercTable(profile(), intelliUseCustomPercTable, intelliPerc.flags);
}

void NinSnesSeq::setIntelliCustomPercTableEnabled(bool enabled) {
  setNinSnesIntelliCustomPercTableEnabled(profile(), enabled, intelliUseCustomPercTable,
                                          intelliPerc.flags);
}

// Registers a TA instrument overwrite as a synthetic exported program.
u32 NinSnesSeq::registerIntelliTAInstrumentOverride(u8 logicalInstrIndex,
                                                         const std::array<u8, 6>& regionData) {
  const u32 progNum = m_nextIntelliTAOverrideProgram++;
  m_intelliTAInstrumentOverrides.push_back({logicalInstrIndex, progNum, regionData});

  if (logicalInstrIndex < intelliInstrumentProgramMap.size()) {
    intelliInstrumentProgramMap[logicalInstrIndex] = progNum;
  }

  return progNum;
}

// Builds the current TA drum-kit definition from Intelli percussion slots.
NinSnesIntelliTADrumKitDef NinSnesSeq::buildIntelliTADrumKitDef() const {
  NinSnesIntelliTADrumKitDef drumKitDef;

  for (size_t slot = 0; slot < NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT; slot++) {
    const auto percussionState = getIntelliPercussionNoteState(*this, static_cast<u8>(slot));

    drumKitDef.slots[slot].active = true;
    drumKitDef.slots[slot].sourceProgNum = resolveProgramNumber(percussionState.instrumentByte);
    drumKitDef.slots[slot].playedNoteByte = percussionState.playedNoteByte;
  }

  return drumKitDef;
}

// Reuses or allocates the exported drum-kit program for the current Intelli slot layout.
u8 NinSnesSeq::ensureIntelliTADrumKitProgram() {
  auto drumKitDef = buildIntelliTADrumKitDef();

  for (const auto& existingDef : m_intelliTADrumKitDefs) {
    if (existingDef.slots == drumKitDef.slots) {
      return existingDef.program;
    }
  }

  if (m_intelliTADrumKitDefs.size() >= 0x80) {
    L_WARN("Too many Intelligent Systems TA drum kit variations; reusing the last exported kit.");
    return m_intelliTADrumKitDefs.back().program;
  }

  drumKitDef.program = static_cast<u8>(m_intelliTADrumKitDefs.size());
  m_intelliTADrumKitDefs.push_back(drumKitDef);
  return drumKitDef.program;
}

// Applies Intelli percussion slot controls and references its source patch.
void NinSnesTrack::applyIntelliPercussionSlotState(u8 instrumentByte,
                                                   std::optional<u8> panByte,
                                                   std::optional<u8> reverbLevel) {
  if (panByte.has_value()) {
    double volumeScale;
    bool reverseLeft;
    bool reverseRight;
    const s8 midiPan = calculatePanValue(*panByte, volumeScale, reverseLeft, reverseRight);
    const u8 expressionLevel = convertPercentAmpToStdMidiVal(volumeScale);
    addPanNoItem(midiPan);
    addExpressionNoItem(expressionLevel);
  }

  if (reverbLevel.has_value()) {
    addReverbNoItem(*reverbLevel);
  }

  auto& parentSeq = seq();
  const u32 progNum = parentSeq.resolveProgramNumber(instrumentByte);
  if (readMode == READMODE_ADD_TO_UI) {
    parentSeq.addInstrumentRef(progNum);
  }
}

// Emits an Intelli table percussion note through the synthetic TA/FE4 drum kit.
bool NinSnesTrack::handleIntelliPercussionNote(u32 beginOffset, u8 slot,
                                               u8 duration) {
  auto& parentSeq = seq();
  if (!isIntelliTablePercussionVersion(parentSeq.profileId) ||
      slot >= NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT) {
    return false;
  }

  const auto percussionState = getIntelliPercussionNoteState(parentSeq, slot);
  applyIntelliPercussionSlotState(percussionState.instrumentByte, percussionState.panByte,
                                  percussionState.reverbLevel);

  const u8 drumProgram = parentSeq.ensureIntelliTADrumKitProgram();
  const u8 drumKey = static_cast<u8>(0x24 + slot);

  switchToPercussionProgramIfNeeded(drumProgram);
  beginNotePitch(static_cast<u8>(drumKey - parentSeq.globalTranspose));
  addPercNoteByDur(beginOffset, curOffset - beginOffset,
                   static_cast<s8>(drumKey - cKeyCorrection - parentSeq.globalTranspose),
                   state.spcNoteVolume / 2, duration, "Percussion Note");
  consumeQueuedPitchSlide();
  m_lastNoteWasPercussion = true;
  addTime(state.spcNoteDuration);
  return true;
}

// Reads Intelli note-param streams, which encode duration and velocity indices inline.
void NinSnesTrack::readIntelliNoteParam(u32 beginOffset, u8 statusByte,
                                        std::string& desc) {
  auto& parentSeq = seq();
  if (intelliMode() == NinSnesIntelliModeId::Fe3 && !parentSeq.intelliUseCustomNoteParam) {
    readStandardNoteParam(beginOffset, statusByte, desc);
    return;
  }

  state.spcNoteDuration = statusByte;
  desc = fmt::format("Duration: {:d}", state.spcNoteDuration);

  while (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
    const u8 noteParam = readByte(curOffset++);
    if (noteParam < 0x40) {
      const u8 durIndex = noteParam & 0x3f;
      state.spcNoteDurRate = parentSeq.intelliDurVolTable[durIndex];
      fmt::format_to(std::back_inserter(desc), "  Quantize: {} ({}/256)", durIndex,
                     state.spcNoteDurRate);
    } else {
      const u8 velIndex = noteParam & 0x3f;
      state.spcNoteVolume = parentSeq.intelliDurVolTable[velIndex];
      fmt::format_to(std::back_inserter(desc), "  Velocity: {} ({}/256)", velIndex,
                     state.spcNoteVolume);
    }
  }

  addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::DurationChange);
}

// Handles Intelligent Systems-only events and their FE3/FE4/TA variant behavior.
bool NinSnesTrack::handleIntelliEvent(NinSnesSeqEventType eventType, u32 beginOffset,
                                      u8 statusByte, std::string& desc) {
  auto& parentSeq = seq();
  const auto currentIntelliMode = intelliMode();

  switch (eventType) {
    case EVENT_INTELLI_NOTE_PARAM:
      readIntelliNoteParam(beginOffset, statusByte, desc);
      return true;

    case EVENT_INTELLI_ECHO_ON:
      addReverb(beginOffset, curOffset - beginOffset, 40, "Echo On");
      return true;

    case EVENT_INTELLI_ECHO_OFF:
      addReverb(beginOffset, curOffset - beginOffset, 0, "Echo Off");
      return true;

    case EVENT_INTELLI_LEGATO_ON:
      intelliLegato = true;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Legato On (No Key Off)", desc,
                      Type::Portamento);
      return true;

    case EVENT_INTELLI_LEGATO_OFF:
      intelliLegato = false;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Legato Off", desc, Type::Portamento);
      return true;

    case EVENT_INTELLI_JUMP_SHORT_CONDITIONAL: {
      const u8 offset = readByte(curOffset++);
      const u16 dest = curOffset + offset;
      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Conditional Jump (Short)", desc,
                      Type::Misc);
      curOffset = dest;
      return true;
    }

    case EVENT_INTELLI_JUMP_SHORT: {
      const u8 offset = readByte(curOffset++);
      const u16 dest = curOffset + offset;
      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Jump (Short)", desc, Type::Misc);
      curOffset = dest;
      return true;
    }

    case EVENT_INTELLI_FE3_EVENT_F5: {
      const u8 param = readByte(curOffset++);
      if (param < 0xf0) {
        desc = fmt::format("Value: {:d}", param);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Wait for APU Port #2", desc,
                        Type::ChangeState);
        return true;
      }

      const u8 bit = param & 7;
      const bool bitValue = (param & 8) == 0;
      desc = fmt::format("Status: {}", (bitValue ? "On" : "Off"));
      switch (bit) {
        case 0:
          parentSeq.intelliUseCustomPercTable = bitValue;
          addGenericEvent(beginOffset, curOffset - beginOffset, "Use Custom Percussion Table",
                          desc, Type::ChangeState);
          return true;

        case 7:
          parentSeq.intelliUseCustomNoteParam = bitValue;
          addGenericEvent(beginOffset, curOffset - beginOffset, "Use Custom Note Param", desc,
                          Type::ChangeState);
          return true;

        default:
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
          return true;
      }
    }

    case EVENT_INTELLI_WRITE_APU_PORT: {
      const u8 value = readByte(curOffset++);
      desc = fmt::format("Value: {:d}", value);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Write APU Port", desc,
                      Type::ChangeState);
      return true;
    }

    case EVENT_INTELLI_FE3_EVENT_F9:
      curOffset += 36;
      addUnknown(beginOffset, curOffset - beginOffset);
      return true;

    case EVENT_INTELLI_DEFINE_VOICE_PARAM: {
      const s8 param = readByte(curOffset++);
      if (param >= 0) {
        const u32 tableOffset = curOffset;
        parentSeq.intelliVoiceParam.addr = tableOffset;
        parentSeq.intelliVoiceParam.size = param;
        parentSeq.intelliVoiceParam.defined = true;
        curOffset += parentSeq.intelliVoiceParam.size * 4;
        desc = fmt::format("Number of Items: {:d}", parentSeq.intelliVoiceParam.size);
        SeqEvent* event = addGenericEvent(beginOffset, curOffset - beginOffset,
                            "Voice Param Table", desc, Type::Misc);

        if (event != nullptr) {
          const auto& labels = isPackedIntelliVoiceParam(currentIntelliMode)
                                   ? INTELLI_PACKED_VOICE_PARAM_LABELS
                                   : INTELLI_VOICE_PARAM_LABELS;
          for (u8 itemIndex = 0; itemIndex < parentSeq.intelliVoiceParam.size; itemIndex++) {
            const u32 recordOffset = tableOffset + itemIndex * 4u;
            addIntelliTableItem(event, recordOffset, fmt::format("Voice Param {:d}", itemIndex),
                                labels,
                                {readByte(recordOffset), readByte(recordOffset + 1),
                                 readByte(recordOffset + 2), readByte(recordOffset + 3)});
          }
        }
        return true;
      }

      if (currentIntelliMode == NinSnesIntelliModeId::Fe3 ||
          currentIntelliMode == NinSnesIntelliModeId::Ta) {
        const u8 instrNum = param & 0x3f;
        const u32 regionOffset = curOffset;
        std::array<u8, 6> regionData{};
        for (size_t i = 0; i < regionData.size(); i++) {
          regionData[i] = readByte(curOffset + i);
        }
        curOffset += 6;
        desc = fmt::format("Instrument: {:d}", instrNum);

        if (currentIntelliMode == NinSnesIntelliModeId::Ta) {
          const u32 newProgNum =
              parentSeq.registerIntelliTAInstrumentOverride(instrNum, regionData);
          if (currentLogicalProgram.has_value() && *currentLogicalProgram == instrNum) {
            rememberMelodicProgram(newProgNum, instrNum);
            if (!m_lastNoteWasPercussion) {
              addProgramChangeNoItem(newProgNum, true);
            }
          }
        }

        SeqEvent* event = addGenericEvent(beginOffset, curOffset - beginOffset,
                                          "Overwrite Instrument Region", desc, Type::Misc);

        if (event != nullptr) {
          addIntelliTableItem(event, regionOffset, fmt::format("Instrument {:d}", instrNum),
                              INTELLI_OVERWRITE_LABELS, regionData);
        }
      } else {
        addUnknown(beginOffset, curOffset - beginOffset);
      }
      return true;
    }

    case EVENT_INTELLI_LOAD_VOICE_PARAM: {
      const u8 paramIndex = readByte(curOffset++);
      desc = fmt::format("Index: {:d}", paramIndex);

      u32 addrVoiceParam = 0;
      bool outOfRange = false;
      const bool taStyleVoiceParam = isPackedIntelliVoiceParam(currentIntelliMode);
      const bool canReadVoiceParam =
          getIntelliVoiceParamAddress(parentSeq, paramIndex, addrVoiceParam, outOfRange);
      if (taStyleVoiceParam && outOfRange) {
        fmt::format_to(std::back_inserter(desc), "  Out of Declared Range");
      }

      if (canReadVoiceParam) {
        const auto record = decodeIntelliVoiceParamRecord(parentSeq, currentIntelliMode,
                                                          addrVoiceParam, this->fineTuningCents,
                                                          state.spcTranspose);

        addVolNoItem(record.volumeByte / 2);

        double volumeScale;
        bool reverseLeft;
        bool reverseRight;
        const s8 midiPan =
            calculatePanValue(record.panValue, volumeScale, reverseLeft, reverseRight);
        addPanNoItem(midiPan);
        addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));

        state.spcTranspose = record.transpose;
        transpose = record.transpose;
        addFineTuningNoItem(record.fineTuningCents);

        rememberMelodicProgram(record.resolvedProgNum, record.logicalProgNum);
        if (!m_lastNoteWasPercussion) {
          addProgramChangeNoItem(record.resolvedProgNum, true);
        }

        fmt::format_to(std::back_inserter(desc),
                       "  Instrument: ${:02X}  Volume: {:d}  Pan: {:d}  Tuning: {:.3f} semitones  "
                       "Transpose: {:d}",
                       record.instrumentByte, record.volumeByte, record.panValue,
                       record.fineTuningCents / 100.0, record.transpose);
      } else if (taStyleVoiceParam) {
        if (!parentSeq.intelliVoiceParam.defined) {
          fmt::format_to(std::back_inserter(desc), "  Table: Undefined");
        } else {
          fmt::format_to(std::back_inserter(desc), "  Record Address: ${:04X} (Invalid)",
                         static_cast<u16>(addrVoiceParam & 0xffff));
        }
      }

      addGenericEvent(beginOffset, curOffset - beginOffset, "Load Voice Param", desc,
                      Type::ProgramChange);
      return true;
    }

    case EVENT_INTELLI_ADSR: {
      const u8 adsr1 = readByte(curOffset++);
      const u8 adsr2 = readByte(curOffset++);
      desc = fmt::format("ADSR(1): ${:02X}  ADSR(2): ${:02X}", adsr1, adsr2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      return true;
    }

    case EVENT_INTELLI_GAIN_SUSTAIN_TIME_AND_RATE: {
      const u8 sustainDurRate = readByte(curOffset++);
      const u8 sustainGAIN = readByte(curOffset++);
      if (currentIntelliMode == NinSnesIntelliModeId::Ta) {
        state.spcNoteDurRate = sustainDurRate;
        desc = fmt::format("Duration Rate: {:d}/256  GAIN: ${:02X}", sustainDurRate, sustainGAIN);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate / GAIN", desc,
                        Type::DurationChange);
      } else {
        desc = fmt::format("Duration for Sustain: {:d}/256  GAIN for Sustain: ${:02X}",
                           sustainDurRate, sustainGAIN);
        addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN Sustain Time/Rate", desc,
                        Type::Adsr);
      }
      return true;
    }

    case EVENT_INTELLI_GAIN_SUSTAIN_TIME: {
      const u8 sustainDurRate = readByte(curOffset++);
      if (currentIntelliMode == NinSnesIntelliModeId::Ta) {
        state.spcNoteDurRate = sustainDurRate;
        desc = fmt::format("Duration Rate: {:d}/256", sustainDurRate);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate", desc,
                        Type::DurationChange);
      } else {
        desc = fmt::format("Duration for Sustain: {:d}/256", sustainDurRate);
        addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN Sustain Time", desc,
                        Type::Adsr);
      }
      return true;
    }

    case EVENT_INTELLI_GAIN: {
      const u8 gain = readByte(curOffset++);
      desc = fmt::format("  GAIN: ${:02X}", gain);
      addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN (Release Rate)", desc,
                      Type::Adsr);
      return true;
    }

    case EVENT_INTELLI_FE4_EVENT_FC: {
      const u8 arg1 = readByte(curOffset++);
      const u8 numEntries = (arg1 & 15) + 1;
      const u32 tableOffset = curOffset;

      if (isIntelliTablePercussionVersion(parentSeq.profileId)) {
        for (u8 slot = 0; slot < numEntries && slot < NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT;
             slot++) {
          auto& customEntry = parentSeq.intelliPerc.table[slot];
          customEntry.patchByte = readByte(curOffset++);
          customEntry.noteByte = readByte(curOffset++);
          customEntry.panByte = readByte(curOffset++);
        }

        parentSeq.setIntelliCustomPercTableEnabled(true);
        desc = fmt::format("Entries: {:d}", numEntries);
        SeqEvent* event = addGenericEvent(beginOffset, curOffset - beginOffset,
                           "Custom Percussion Table", desc, Type::ChangeState);

        if (event != nullptr) {
          for (u8 slot = 0;
               slot < numEntries && slot < NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT; slot++) {
            const u32 entryOffset = tableOffset + slot * 3u;
            const u8 drumNote =
                static_cast<u8>(parentSeq.STATUS_PERCUSSION_NOTE_MIN + slot);
            const auto& entry = parentSeq.intelliPerc.table[slot];
            addIntelliTableItem(event, entryOffset, fmt::format("Drum ${:02X}", drumNote),
                                INTELLI_PERCUSSION_ENTRY_LABELS,
                                {entry.patchByte, entry.noteByte, entry.panByte});
          }
        }
      } else {
        curOffset += numEntries * 3;
        addUnknown(beginOffset, curOffset - beginOffset);
      }
      return true;
    }

    case EVENT_INTELLI_TA_SUBEVENT: {
      const u8 type = readByte(curOffset++);

      switch (type) {
        case 0x00: {
          const u8 valueLo = readByte(curOffset++);
          const u8 valueHi = readByte(curOffset++);
          const u8 packedType = readByte(curOffset++);
          const u16 requestValue = valueLo | (valueHi << 8);
          const u8 requestKind = (packedType & 0x07) << 1;
          const u8 requestPriority = packedType & 0xf8;

          desc = fmt::format("Value: ${:04X}  Kind: ${:02X}  Priority: ${:02X}", requestValue,
                             requestKind, requestPriority);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Queued Global Request", desc,
                          Type::Misc);
          return true;
        }

        case 0x01:
        case 0x02: {
          const u8 param = readByte(curOffset++);
          const bool bitValue = (type == 0x01);
          const char* eventName =
              describeIntelliFlagMaskEvent(parentSeq, param, bitValue, true, desc);
          addGenericEvent(beginOffset, curOffset - beginOffset, eventName, desc,
                          Type::ChangeState);
          return true;
        }

        case 0x03:
          intelliLegato = true;
          addGenericEvent(beginOffset, curOffset - beginOffset, "Legato On (No Key Off)", desc,
                          Type::Portamento);
          return true;

        case 0x04:
          intelliLegato = false;
          addGenericEvent(beginOffset, curOffset - beginOffset, "Legato Off", desc,
                          Type::Portamento);
          return true;

        case 0x05:
          parentSeq.intelliPerc.unknownByte = readByte(curOffset++);
          desc = fmt::format("Value: ${:02X}", parentSeq.intelliPerc.unknownByte);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Set Global Byte $0364", desc,
                          Type::ChangeState);
          return true;

        default:
          addUnknown(beginOffset, curOffset - beginOffset);
          return true;
      }
    }

    case EVENT_INTELLI_FE4_SUBEVENT: {
      const u8 type = readByte(curOffset++);
      switch (type) {
        case 0x01:
        case 0x02: {
          const u8 param = readByte(curOffset++);
          const bool bitValue = (type == 0x01);
          const char* eventName =
              describeIntelliFlagMaskEvent(parentSeq, param, bitValue, false, desc);
          addGenericEvent(beginOffset, curOffset - beginOffset, eventName, desc,
                          Type::ChangeState);
          return true;
        }

        default:
          addUnknown(beginOffset, curOffset - beginOffset);
          return true;
      }
    }

    default:
      return false;
  }
}
