#include "NinSnesSeq.h"

#include "ScaleConversion.h"
#include "SeqEvent.h"
#include "spdlog/fmt/fmt.h"

namespace {
struct NinSnesIntelliPercussionNoteState {
  uint8_t instrumentByte = 0;
  uint8_t playedNoteByte = 0xa4;
  std::optional<uint8_t> panByte;
  std::optional<uint8_t> reverbLevel;
};

struct NinSnesIntelliVoiceParamRecord {
  uint8_t instrumentByte = 0;
  uint8_t volumeByte = 0;
  uint8_t panValue = 0;
  uint8_t logicalProgNum = 0;
  uint32_t resolvedProgNum = 0;
  double fineTuningCents = 0.0;
  int8_t transpose = 0;
};

bool isIntelliTablePercussionVersion(NinSnesProfileId profileId) {
  const auto intelliMode = getNinSnesProfile(profileId).intelliMode;
  return intelliMode == NinSnesIntelliModeId::Ta || intelliMode == NinSnesIntelliModeId::Fe4;
}

bool isPackedIntelliVoiceParam(NinSnesIntelliModeId intelliMode) {
  return intelliMode == NinSnesIntelliModeId::Ta || intelliMode == NinSnesIntelliModeId::Fe4;
}

void addIntelliByteChild(VGMItem* parent, uint32_t offset, const std::string& label,
                         uint8_t value) {
  parent->addChild(offset, 1, fmt::format("{}: ${:02X}", label, value));
}

template <size_t N>
void addIntelliTableItem(VGMItem* parent, uint32_t offset, const std::string& name,
                         const std::array<const char*, N>& labels,
                         const std::array<uint8_t, N>& values) {
  VGMItem* item = parent->addChild(offset, static_cast<uint32_t>(N), name);
  for (size_t i = 0; i < N; i++) {
    addIntelliByteChild(item, offset + i, labels[i], values[i]);
  }
}

void updateIntelliFlags(uint8_t& flags, uint8_t mask, bool enabled) {
  if (enabled) {
    flags |= mask;
  } else {
    flags &= static_cast<uint8_t>(~mask);
  }
}

// Resolves a voice-param record address and reports TA/FE4 out-of-range table access.
bool getIntelliVoiceParamAddress(const NinSnesSeq& seq, uint8_t index, uint32_t& addrVoiceParam,
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
const char* describeIntelliFlagMaskEvent(NinSnesSeq& seq, uint8_t param, bool bitValue,
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
                                                             uint32_t addrVoiceParam,
                                                             double baseFineTuningCents,
                                                             int8_t currentTranspose) {
  constexpr std::array<int8_t, 7> FE3_TRANSPOSE_TABLE = {-24, -12, -1, 0, 1, 12, 24};

  NinSnesIntelliVoiceParamRecord record;
  record.instrumentByte = seq.readByte(addrVoiceParam);
  record.volumeByte = seq.readByte(addrVoiceParam + 1);
  const uint8_t packedPanByte = seq.readByte(addrVoiceParam + 2);
  const uint8_t lastByte = seq.readByte(addrVoiceParam + 3);

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
      const uint8_t tuningIndex = lastByte & 15;
      const uint8_t transposeIndex = (lastByte & 0x70) >> 4;

      if (tuningIndex != 0) {
        const uint8_t newTuning = (tuningIndex - 1) * 5;
        record.fineTuningCents = (newTuning / 256.0) * 100.0;
      }

      if (transposeIndex != 0) {
        record.transpose = FE3_TRANSPOSE_TABLE[transposeIndex - 1];
      }
      break;
    }

    case NinSnesIntelliModeId::Fe4:
    case NinSnesIntelliModeId::Ta:
      record.transpose = static_cast<int8_t>(lastByte);
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
                                                                uint8_t slot) {
  NinSnesIntelliPercussionNoteState percussionState;
  percussionState.instrumentByte = static_cast<uint8_t>(seq.STATUS_PERCUSSION_NOTE_MIN + slot);

  if (!seq.usesIntelliCustomPercTable()) {
    return percussionState;
  }

  const auto& customEntry = seq.intelliPerc.table[slot];
  percussionState.instrumentByte = customEntry.patchByte & 0xbf;
  percussionState.playedNoteByte = customEntry.noteByte;
  if (customEntry.panByte < 0x80) {
    percussionState.panByte = customEntry.panByte;
  }
  percussionState.reverbLevel = static_cast<uint8_t>((customEntry.patchByte & 0x40) != 0 ? 40 : 0);
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
uint32_t NinSnesSeq::registerIntelliTAInstrumentOverride(uint8_t logicalInstrIndex,
                                                         const std::array<uint8_t, 6>& regionData) {
  const uint32_t progNum = m_nextIntelliTAOverrideProgram++;
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
    const auto percussionState = getIntelliPercussionNoteState(*this, static_cast<uint8_t>(slot));

    drumKitDef.slots[slot].active = true;
    drumKitDef.slots[slot].sourceProgNum = resolveProgramNumber(percussionState.instrumentByte);
    drumKitDef.slots[slot].playedNoteByte = percussionState.playedNoteByte;
  }

  return drumKitDef;
}

// Reuses or allocates the exported drum-kit program for the current Intelli slot layout.
uint8_t NinSnesSeq::ensureIntelliTADrumKitProgram() {
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

  drumKitDef.program = static_cast<uint8_t>(m_intelliTADrumKitDefs.size());
  m_intelliTADrumKitDefs.push_back(drumKitDef);
  return drumKitDef.program;
}

// Applies Intelli percussion slot controls and references its source patch.
void NinSnesTrack::applyIntelliPercussionSlotState(uint8_t instrumentByte,
                                                   std::optional<uint8_t> panByte,
                                                   std::optional<uint8_t> reverbLevel) {
  if (panByte.has_value()) {
    double volumeScale;
    bool reverseLeft;
    bool reverseRight;
    const int8_t midiPan = calculatePanValue(*panByte, volumeScale, reverseLeft, reverseRight);
    const uint8_t expressionLevel = convertPercentAmpToStdMidiVal(volumeScale);
    addPanNoItem(midiPan);
    addExpressionNoItem(expressionLevel);
  }

  if (reverbLevel.has_value()) {
    addReverbNoItem(*reverbLevel);
  }

  auto& parentSeq = seq();
  const uint32_t progNum = parentSeq.resolveProgramNumber(instrumentByte);
  if (readMode == READMODE_ADD_TO_UI) {
    parentSeq.addInstrumentRef(progNum);
  }
}

// Emits an Intelli table percussion note through the synthetic TA/FE4 drum kit.
bool NinSnesTrack::handleIntelliPercussionNote(uint32_t beginOffset, uint8_t slot,
                                               uint8_t duration) {
  auto& parentSeq = seq();
  if (!isIntelliTablePercussionVersion(parentSeq.profileId) ||
      slot >= NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT) {
    return false;
  }

  const auto percussionState = getIntelliPercussionNoteState(parentSeq, slot);
  applyIntelliPercussionSlotState(percussionState.instrumentByte, percussionState.panByte,
                                  percussionState.reverbLevel);

  const uint8_t drumProgram = parentSeq.ensureIntelliTADrumKitProgram();
  const uint8_t drumKey = static_cast<uint8_t>(0x24 + slot);

  switchToPercussionProgramIfNeeded(drumProgram);
  addPercNoteByDur(beginOffset, curOffset - beginOffset,
                   static_cast<int8_t>(drumKey - cKeyCorrection - parentSeq.globalTranspose),
                   shared->spcNoteVolume / 2, duration, "Percussion Note");
  m_lastNoteWasPercussion = true;
  addTime(shared->spcNoteDuration);
  return true;
}

// Reads Intelli note-param streams, which encode duration and velocity indices inline.
void NinSnesTrack::readIntelliNoteParam(uint32_t beginOffset, uint8_t statusByte,
                                        std::string& desc) {
  auto& parentSeq = seq();
  if (intelliMode() == NinSnesIntelliModeId::Fe3 && !parentSeq.intelliUseCustomNoteParam) {
    readStandardNoteParam(beginOffset, statusByte, desc);
    return;
  }

  shared->spcNoteDuration = statusByte;
  desc = fmt::format("Duration: {:d}", shared->spcNoteDuration);

  while (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
    const uint8_t noteParam = readByte(curOffset++);
    if (noteParam < 0x40) {
      const uint8_t durIndex = noteParam & 0x3f;
      shared->spcNoteDurRate = parentSeq.intelliDurVolTable[durIndex];
      fmt::format_to(std::back_inserter(desc), "  Quantize: {} ({}/256)", durIndex,
                     shared->spcNoteDurRate);
    } else {
      const uint8_t velIndex = noteParam & 0x3f;
      shared->spcNoteVolume = parentSeq.intelliDurVolTable[velIndex];
      fmt::format_to(std::back_inserter(desc), "  Velocity: {} ({}/256)", velIndex,
                     shared->spcNoteVolume);
    }
  }

  addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::DurationChange);
}

// Handles Intelligent Systems-only events and their FE3/FE4/TA variant behavior.
bool NinSnesTrack::handleIntelliEvent(NinSnesSeqEventType eventType, uint32_t beginOffset,
                                      uint8_t statusByte, std::string& desc) {
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
      const uint8_t offset = readByte(curOffset++);
      const uint16_t dest = curOffset + offset;
      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Conditional Jump (Short)", desc,
                      Type::Misc);
      curOffset = dest;
      return true;
    }

    case EVENT_INTELLI_JUMP_SHORT: {
      const uint8_t offset = readByte(curOffset++);
      const uint16_t dest = curOffset + offset;
      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Jump (Short)", desc, Type::Misc);
      curOffset = dest;
      return true;
    }

    case EVENT_INTELLI_FE3_EVENT_F5: {
      const uint8_t param = readByte(curOffset++);
      if (param < 0xf0) {
        desc = fmt::format("Value: {:d}", param);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Wait for APU Port #2", desc,
                        Type::ChangeState);
        return true;
      }

      const uint8_t bit = param & 7;
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
      const uint8_t value = readByte(curOffset++);
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
      const int8_t param = readByte(curOffset++);
      if (param >= 0) {
        const uint32_t tableOffset = curOffset;
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
          for (uint8_t itemIndex = 0; itemIndex < parentSeq.intelliVoiceParam.size; itemIndex++) {
            const uint32_t recordOffset = tableOffset + itemIndex * 4u;
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
        const uint8_t instrNum = param & 0x3f;
        const uint32_t regionOffset = curOffset;
        std::array<uint8_t, 6> regionData{};
        for (size_t i = 0; i < regionData.size(); i++) {
          regionData[i] = readByte(curOffset + i);
        }
        curOffset += 6;
        desc = fmt::format("Instrument: {:d}", instrNum);

        if (currentIntelliMode == NinSnesIntelliModeId::Ta) {
          const uint32_t newProgNum =
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
      const uint8_t paramIndex = readByte(curOffset++);
      desc = fmt::format("Index: {:d}", paramIndex);

      uint32_t addrVoiceParam = 0;
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
                                                          shared->spcTranspose);

        addVolNoItem(record.volumeByte / 2);

        double volumeScale;
        bool reverseLeft;
        bool reverseRight;
        const int8_t midiPan =
            calculatePanValue(record.panValue, volumeScale, reverseLeft, reverseRight);
        addPanNoItem(midiPan);
        addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));

        shared->spcTranspose = record.transpose;
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
                         static_cast<uint16_t>(addrVoiceParam & 0xffff));
        }
      }

      addGenericEvent(beginOffset, curOffset - beginOffset, "Load Voice Param", desc,
                      Type::ProgramChange);
      return true;
    }

    case EVENT_INTELLI_ADSR: {
      const uint8_t adsr1 = readByte(curOffset++);
      const uint8_t adsr2 = readByte(curOffset++);
      desc = fmt::format("ADSR(1): ${:02X}  ADSR(2): ${:02X}", adsr1, adsr2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      return true;
    }

    case EVENT_INTELLI_GAIN_SUSTAIN_TIME_AND_RATE: {
      const uint8_t sustainDurRate = readByte(curOffset++);
      const uint8_t sustainGAIN = readByte(curOffset++);
      if (currentIntelliMode == NinSnesIntelliModeId::Ta) {
        shared->spcNoteDurRate = sustainDurRate;
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
      const uint8_t sustainDurRate = readByte(curOffset++);
      if (currentIntelliMode == NinSnesIntelliModeId::Ta) {
        shared->spcNoteDurRate = sustainDurRate;
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
      const uint8_t gain = readByte(curOffset++);
      desc = fmt::format("  GAIN: ${:02X}", gain);
      addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN (Release Rate)", desc,
                      Type::Adsr);
      return true;
    }

    case EVENT_INTELLI_FE4_EVENT_FC: {
      const uint8_t arg1 = readByte(curOffset++);
      const uint8_t numEntries = (arg1 & 15) + 1;
      const uint32_t tableOffset = curOffset;

      if (isIntelliTablePercussionVersion(parentSeq.profileId)) {
        for (uint8_t slot = 0; slot < numEntries && slot < NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT;
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
          for (uint8_t slot = 0;
               slot < numEntries && slot < NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT; slot++) {
            const uint32_t entryOffset = tableOffset + slot * 3u;
            const uint8_t drumNote =
                static_cast<uint8_t>(parentSeq.STATUS_PERCUSSION_NOTE_MIN + slot);
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
      const uint8_t type = readByte(curOffset++);

      switch (type) {
        case 0x00: {
          const uint8_t valueLo = readByte(curOffset++);
          const uint8_t valueHi = readByte(curOffset++);
          const uint8_t packedType = readByte(curOffset++);
          const uint16_t requestValue = valueLo | (valueHi << 8);
          const uint8_t requestKind = (packedType & 0x07) << 1;
          const uint8_t requestPriority = packedType & 0xf8;

          desc = fmt::format("Value: ${:04X}  Kind: ${:02X}  Priority: ${:02X}", requestValue,
                             requestKind, requestPriority);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Queued Global Request", desc,
                          Type::Misc);
          return true;
        }

        case 0x01:
        case 0x02: {
          const uint8_t param = readByte(curOffset++);
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
      const uint8_t type = readByte(curOffset++);
      switch (type) {
        case 0x01:
        case 0x02: {
          const uint8_t param = readByte(curOffset++);
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
