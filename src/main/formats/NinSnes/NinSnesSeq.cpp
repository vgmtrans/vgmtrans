#include "NinSnesSeq.h"
#include "SeqEvent.h"
#include "ScaleConversion.h"
#include "spdlog/fmt/fmt.h"

DECLARE_FORMAT(NinSnes);

namespace {
struct NinSnesIntelliPercussionNoteState {
  uint8_t instrumentByte = 0;
  uint8_t playedNoteByte = 0xa4;
  std::optional<uint8_t> panByte;
  std::optional<uint8_t> reverbLevel;
};

bool isIntelliTablePercussionVersion(NinSnesProfileId profileId) {
  const auto intelliMode = getNinSnesProfile(profileId).intelliMode;
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

bool getIntelliVoiceParamAddress(const NinSnesSeq& seq, uint8_t index, uint32_t& addrVoiceParam,
                                 bool& outOfRange) {
  addrVoiceParam = 0;
  outOfRange = false;

  if (!seq.intelliVoiceParam.defined) {
    return false;
  }

  addrVoiceParam = seq.intelliVoiceParam.addr + (index * 4u);
  outOfRange = index >= seq.intelliVoiceParam.size;
  const auto intelliMode = seq.profile().intelliMode;
  const bool packedVoiceParams =
      intelliMode == NinSnesIntelliModeId::Ta || intelliMode == NinSnesIntelliModeId::Fe4;
  return (addrVoiceParam + 3) < 0x10000 && (packedVoiceParams || !outOfRange);
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

//  **********
//  NinSnesSeq
//  **********
#define MAX_TRACKS 8
#define SEQ_PPQN 48
#define SEQ_KEYOFS 24

NinSnesSeq::NinSnesSeq(RawFile* file, NinSnesProfileId profile, uint32_t offset,
                       uint8_t percussion_base, const std::vector<uint8_t>& theVolumeTable,
                       const std::vector<uint8_t>& theDurRateTable, std::string theName)
    : VGMMultiSectionSeq(NinSnesFormat::name, file, offset, 0, theName),
      signature(NinSnesSignatureId::None), profileId(profile), header(NULL),
      volumeTable(theVolumeTable), durRateTable(theDurRateTable),
      spcPercussionBaseInit(percussion_base), konamiBaseAddress(0), quintetBGMInstrBase(0),
      falcomBaseOffset(0), m_nextIntelliTAOverrideProgram(0x80) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

NinSnesSeq::NinSnesSeq(RawFile* file, const NinSnesScanResult& scanResult)
    : NinSnesSeq(file, scanResult.profile, scanResult.songStartAddr, 0, scanResult.volumeTable,
                 scanResult.durRateTable, scanResult.name) {
  signature = scanResult.signature;
  profileId = scanResult.profile;
  konamiBaseAddress = scanResult.konamiBaseAddress;
  quintetBGMInstrBase = scanResult.quintetBGMInstrBase;
  quintetAddrBGMInstrLookup = scanResult.quintetAddrBGMInstrLookup;
  falcomBaseOffset = scanResult.falcomBaseOffset;
}

NinSnesSeq::~NinSnesSeq() {
}

const NinSnesProfile& NinSnesSeq::profile() const {
  return getNinSnesProfile(profileId);
}

void NinSnesSeq::resetVars() {
  VGMMultiSectionSeq::resetVars();

  spcPercussionBase = spcPercussionBaseInit;
  sectionRepeatCount = 0;
  globalTranspose = 0;
  m_percussionInstrNoteMap.clear();

  // Intelligent Systems:
  intelliUseCustomNoteParam = false;
  intelliUseCustomPercTable = false;
  intelliVoiceParam.clear();
  intelliPerc.clear();
  m_nextIntelliTAOverrideProgram = 0x80;
  m_intelliTAInstrumentOverrides.clear();
  m_intelliTADrumKitDefs.clear();
  for (size_t program = 0; program < intelliInstrumentProgramMap.size(); program++) {
    intelliInstrumentProgramMap[program] = static_cast<uint32_t>(program);
  }

  for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    sharedTrackData[trackIndex].resetVars();
  }
}

bool NinSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);
  nNumTracks = MAX_TRACKS;

  if (dwStartOffset + 2 > 0x10000) {
    return false;
  }

  // validate first section
  uint16_t firstSectionPtr = dwStartOffset;
  uint16_t addrFirstSection = readShort(firstSectionPtr);
  if (addrFirstSection + 16 > 0x10000) {
    return false;
  }

  if (addrFirstSection >= 0x0100) {
    addrFirstSection = convertToAPUAddress(addrFirstSection);

    uint8_t numActiveTracks = 0;
    for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
      uint16_t addrTrackStart = readShort(addrFirstSection + trackIndex * 2);
      if (addrTrackStart != 0) {
        addrTrackStart = convertToAPUAddress(addrTrackStart);

        if (addrTrackStart < addrFirstSection) {
          return false;
        }
        numActiveTracks++;
      }
    }
    if (numActiveTracks == 0) {
      return false;
    }
  }

  // events will be added later, see ReadEvent
  header = addHeader(dwStartOffset, 0);
  return true;
}

bool NinSnesSeq::readEvent(long stopTime) {
  const auto& profile = this->profile();
  uint32_t beginOffset = curOffset;
  if (curOffset + 1 >= 0x10000) {
    return false;
  }

  if (curOffset < header->offset()) {
    uint32_t distance = header->offset() - curOffset;
    header->setOffset(curOffset);
    if (header->length() != 0) {
      header->setLength(header->length() + distance);
    }
  }

  uint16_t sectionAddress = readShort(curOffset);
  curOffset += 2;
  bool bContinue = true;

  if (sectionAddress == 0) {
    // End
    if (!isOffsetUsed(beginOffset)) {
      header->addChild(beginOffset, curOffset - beginOffset, "Section Playlist End");
    }
    bContinue = false;
  } else if (sectionAddress <= 0xff) {
    // Jump
    if (curOffset + 1 >= 0x10000) {
      return false;
    }

    uint16_t repeatCount = sectionAddress;
    uint16_t dest = getShortAddress(curOffset);
    curOffset += 2;

    bool startNewRepeat = false;
    bool doJump = false;
    bool infiniteLoop = false;

    if (profile.playlistModel == NinSnesPlaylistModelId::Tose) {
      if (sectionRepeatCount == 0) {
        // set new repeat count
        sectionRepeatCount = (uint8_t)repeatCount;
        startNewRepeat = true;
        doJump = true;
      } else {
        // infinite loop?
        if (sectionRepeatCount == 0xff) {
          if (isOffsetUsed(dest)) {
            infiniteLoop = true;
          }
          doJump = true;
        } else {
          // decrease 8-bit counter
          sectionRepeatCount--;

          // jump if the counter is zero
          if (sectionRepeatCount != 0) {
            doJump = true;
          }
        }
      }
    } else {
      // decrease 8-bit counter
      sectionRepeatCount--;
      // check overflow (end of loop)
      if (sectionRepeatCount >= 0x80) {
        // set new repeat count
        sectionRepeatCount = (uint8_t)repeatCount;
        startNewRepeat = true;
      }

      // jump if the counter is zero
      if (sectionRepeatCount != 0) {
        doJump = true;
        if (isOffsetUsed(dest) && sectionRepeatCount > 0x80) {
          infiniteLoop = true;
        }
      }
    }

    // add event to sequence
    if (!isOffsetUsed(beginOffset)) {
      header->addChild(beginOffset, curOffset - beginOffset, "Playlist Jump");

      // add the last event too, if available
      if (curOffset + 1 < 0x10000 && readShort(curOffset) == 0x0000) {
        header->addChild(curOffset, 2, "Playlist End");
      }
    }

    if (infiniteLoop) {
      bContinue = addLoopForeverNoItem();
    }

    // do actual jump, at last
    if (doJump) {
      curOffset = dest;

      if (curOffset < offset()) {
        uint32_t distance = offset() - curOffset;
        setOffset(curOffset);
        if (length() != 0) {
          setLength(length() + (distance));
        }
      }
    }
  } else {
    sectionAddress = convertToAPUAddress(sectionAddress);

    // Play the section
    if (!isOffsetUsed(beginOffset)) {
      header->addChild(beginOffset, curOffset - beginOffset, "Section Pointer");
    }

    NinSnesSection* section = (NinSnesSection*)getSectionAtOffset(sectionAddress);
    if (section == NULL) {
      section = new NinSnesSection(this, sectionAddress);
      if (!section->load()) {
        L_ERROR("Failed to load section");
        return false;
      }
      addSection(section);
    }

    if (!loadSection(section, stopTime)) {
      bContinue = false;
    }

    if (profile.id == NinSnesProfileId::Unknown) {
      bContinue = false;
    }
  }

  return bContinue;
}

void NinSnesSeq::loadEventMap() {
  const auto definition =
      buildNinSnesSeqDefinition(profileId, volumeTable, durRateTable, panTable, intelliDurVolTable);

  STATUS_END = definition.status.end;
  STATUS_NOTE_MIN = definition.status.noteMin;
  STATUS_NOTE_MAX = definition.status.noteMax;
  STATUS_PERCUSSION_NOTE_MIN = definition.status.percussionNoteMin;
  STATUS_PERCUSSION_NOTE_MAX = definition.status.percussionNoteMax;
  EventMap = definition.eventMap;
  volumeTable = definition.volumeTable;
  durRateTable = definition.durRateTable;
  panTable = definition.panTable;
  intelliDurVolTable = definition.intelliDurVolTable;
}

double NinSnesSeq::getTempoInBPM(uint8_t tempo) {
  if (tempo != 0) {
    return (double)60000000 / (SEQ_PPQN * 2000) * ((double)tempo / 256);
  } else {
    return 1.0;  // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}

uint16_t NinSnesSeq::convertToAPUAddress(uint16_t offset) {
  return convertNinSnesAddress(profile(), offset, konamiBaseAddress, falcomBaseOffset);
}

uint16_t NinSnesSeq::getShortAddress(uint32_t offset) {
  return readNinSnesAddress(profile(), rawFile(), offset, konamiBaseAddress, falcomBaseOffset);
}

uint32_t NinSnesSeq::resolveProgramNumber(uint8_t instrumentByte,
                                          uint8_t* logicalInstrIndex) const {
  return resolveNinSnesProgramNumber(profile(), rawFile(), instrumentByte, STATUS_PERCUSSION_NOTE_MIN,
                                     spcPercussionBase, quintetBGMInstrBase, quintetAddrBGMInstrLookup,
                                     &intelliInstrumentProgramMap, logicalInstrIndex);
}

bool NinSnesSeq::usesIntelliCustomPercTable() const {
  return usesNinSnesIntelliCustomPercTable(profile(), intelliUseCustomPercTable, intelliPerc.flags);
}

void NinSnesSeq::setIntelliCustomPercTableEnabled(bool enabled) {
  setNinSnesIntelliCustomPercTableEnabled(profile(), enabled, intelliUseCustomPercTable,
                                          intelliPerc.flags);
}

uint32_t NinSnesSeq::registerIntelliTAInstrumentOverride(uint8_t logicalInstrIndex,
                                                         const std::array<uint8_t, 6>& regionData) {
  const uint32_t progNum = m_nextIntelliTAOverrideProgram++;
  m_intelliTAInstrumentOverrides.push_back({logicalInstrIndex, progNum, regionData});

  if (logicalInstrIndex < intelliInstrumentProgramMap.size()) {
    intelliInstrumentProgramMap[logicalInstrIndex] = progNum;
  }

  return progNum;
}

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

//  **************
//  NinSnesSection
//  **************

NinSnesSection::NinSnesSection(NinSnesSeq* parentFile, uint32_t offset, uint32_t length)
    : VGMSeqSection(parentFile, offset, length) {
}

bool NinSnesSection::parseTrackPointers() {
  NinSnesSeq* parentSeq = (NinSnesSeq*)this->parentSeq;
  uint32_t curOffset = offset();

  VGMHeader* header = addHeader(curOffset, 16);
  uint8_t numActiveTracks = 0;
  for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    if (curOffset + 1 >= 0x10000) {
      return false;
    }

    uint16_t startAddress = readShort(curOffset);

    bool active = ((startAddress & 0xff00) != 0);
    NinSnesTrack* track;
    if (active) {
      startAddress = convertToApuAddress(startAddress);

      // correct sequence address
      // probably it's not necessary for regular case, but just in case...
      if (startAddress < offset()) {
        uint32_t distance = offset() - startAddress;
        setOffset(startAddress);
        if (length() != 0) {
          setLength(length() + (distance));
        }
      }

      auto trackName = fmt::format("Track {}", trackIndex + 1);
      track = new NinSnesTrack(this, startAddress, 0, trackName);

      numActiveTracks++;
    } else {
      // add an inactive track
      track = new NinSnesTrack(this, curOffset, 2, "NULL");
      track->available = false;
    }
    track->shared = &parentSeq->sharedTrackData[trackIndex];
    aTracks.push_back(track);

    char name[32];
    snprintf(name, 32, "Track Pointer #%d", trackIndex + 1);

    header->addChild(curOffset, 2, name);
    curOffset += 2;
  }

  if (numActiveTracks == 0) {
    return false;
  }

  return true;
}

uint16_t NinSnesSection::convertToApuAddress(uint16_t offset) {
  NinSnesSeq* parentSeq = (NinSnesSeq*)this->parentSeq;
  return parentSeq->convertToAPUAddress(offset);
}

uint16_t NinSnesSection::getShortAddress(uint32_t offset) {
  NinSnesSeq* parentSeq = (NinSnesSeq*)this->parentSeq;
  return parentSeq->getShortAddress(offset);
}

NinSnesTrackSharedData::NinSnesTrackSharedData() {
  resetVars();
}

void NinSnesTrackSharedData::resetVars(void) {
  loopCount = 0;
  spcTranspose = 0;

  // just in case
  spcNoteDuration = 1;
  spcNoteDurRate = 0xfc;
  spcNoteVolume = 0xfc;

  // Konami:
  konamiLoopStart = 0;
  konamiLoopCount = 0;
}

//  ************
//  NinSnesTrack
//  ************

NinSnesTrack::NinSnesTrack(NinSnesSection* parentSection, uint32_t offset, uint32_t length,
                           const std::string& theName)
    : SeqTrack(parentSection->parentSeq, offset, length, theName), parentSection(parentSection) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
}

void NinSnesTrack::resetVars() {
  SeqTrack::resetVars();

  cKeyCorrection = SEQ_KEYOFS;
  if (shared != NULL) {
    transpose = shared->spcTranspose;
  }
  m_lastNoteWasPercussion = false;
  nonPercussionProgram = 0;
  currentPercussionProgram = 0;
  currentLogicalProgram.reset();
  intelliLegato = false;
}

uint8_t NinSnesTrack::getEffectiveNoteDuration() const {
  if (intelliLegato) {
    return shared->spcNoteDuration;
  }

  uint8_t duration = (shared->spcNoteDuration * shared->spcNoteDurRate) >> 8;
  return std::min(std::max(duration, (uint8_t)1), (uint8_t)(shared->spcNoteDuration - 2));
}

void NinSnesTrack::setTrackedProgramState(uint32_t progNum, std::optional<uint8_t> logicalProgram) {
  nonPercussionProgram = progNum;
  if (logicalProgram.has_value()) {
    currentLogicalProgram = logicalProgram;
  }

  if (readMode == READMODE_ADD_TO_UI) {
    static_cast<NinSnesSeq*>(parentSeq)->addInstrumentRef(progNum);
  }
}

void NinSnesTrack::restoreNonPercussionProgramIfNeeded() {
  if (!m_lastNoteWasPercussion) {
    return;
  }

  addBankSelectNoItem(0);
  addProgramChangeNoItem(nonPercussionProgram, false);
  m_lastNoteWasPercussion = false;
}

void NinSnesTrack::switchToPercussionProgramIfNeeded(uint8_t program) {
  if (m_lastNoteWasPercussion && currentPercussionProgram == program) {
    return;
  }

  addBankSelectNoItem(127);
  addProgramChangeNoItem(program, false);
  currentPercussionProgram = program;
  m_lastNoteWasPercussion = true;
}

void NinSnesTrack::applyIntelliPercussionState(uint8_t instrumentByte,
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

  auto* ninSeq = static_cast<NinSnesSeq*>(parentSeq);
  uint8_t logicalProgram = 0;
  const uint32_t progNum = ninSeq->resolveProgramNumber(instrumentByte, &logicalProgram);
  setTrackedProgramState(progNum, logicalProgram);
}

void NinSnesTrack::addProgramChangeEvent(uint32_t offset, uint32_t length, uint32_t progNum,
                                         bool requireBank, const std::string& eventName,
                                         std::optional<uint8_t> logicalProgram) {
  setTrackedProgramState(progNum, logicalProgram);

  bool isNewOffset = onEvent(offset, length);
  recordSeqEvent<ProgChangeSeqEvent>(isNewOffset, getTime(), progNum, offset, length, eventName);

  if (!m_lastNoteWasPercussion) {
    addProgramChangeNoItem(progNum, requireBank);
  }
}

NinSnesSeq& NinSnesTrack::seq() const {
  return *static_cast<NinSnesSeq*>(parentSeq);
}

NinSnesIntelliModeId NinSnesTrack::intelliMode() const {
  return seq().profile().intelliMode;
}

void NinSnesTrack::readStandardNoteParam(uint32_t beginOffset, uint8_t statusByte,
                                         std::string& desc) {
  auto& parentSeq = seq();
  shared->spcNoteDuration = statusByte;
  desc = fmt::format("Duration: {:d}", shared->spcNoteDuration);

  if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
    const uint8_t quantizeAndVelocity = readByte(curOffset++);
    const uint8_t durIndex = (quantizeAndVelocity >> 4) & 7;
    const uint8_t velIndex = quantizeAndVelocity & 15;

    shared->spcNoteDurRate = parentSeq.durRateTable[durIndex];
    shared->spcNoteVolume = parentSeq.volumeTable[velIndex];

    fmt::format_to(std::back_inserter(desc),
                   "  Quantize: {:d} ({:d}/256)  Velocity: {:d} ({:d}/256)", durIndex,
                   shared->spcNoteDurRate, velIndex, shared->spcNoteVolume);
  }

  addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::DurationChange);
}

void NinSnesTrack::readLemmingsNoteParam(uint32_t beginOffset, uint8_t statusByte,
                                         std::string& desc) {
  shared->spcNoteDuration = statusByte;
  desc = fmt::format("Duration: {:d}", shared->spcNoteDuration);

  if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
    const uint8_t durByte = readByte(curOffset++);
    shared->spcNoteDurRate = (durByte << 1) + (durByte >> 1) + (durByte & 1);
    fmt::format_to(std::back_inserter(desc), "  Quantize: {} ({}/256)", durByte,
                   shared->spcNoteDurRate);

    if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
      const uint8_t velByte = readByte(curOffset++);
      shared->spcNoteVolume = velByte << 1;
      fmt::format_to(std::back_inserter(desc), "  Velocity: {} ({}/256)", velByte,
                     shared->spcNoteVolume);
    }
  }

  addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::DurationChange);
}

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

bool NinSnesTrack::handleCoreEvent(NinSnesSeqEventType eventType, uint32_t beginOffset,
                                   uint8_t statusByte, std::string& desc,
                                   bool& continueReading) {
  auto& parentSeq = seq();

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc = fmt::format("Event: 0x{:02X}", statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;

    case EVENT_UNKNOWN1: {
      const uint8_t arg1 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}", statusByte, arg1);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_UNKNOWN2: {
      const uint8_t arg1 = readByte(curOffset++);
      const uint8_t arg2 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}", statusByte, arg1, arg2);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_UNKNOWN3: {
      const uint8_t arg1 = readByte(curOffset++);
      const uint8_t arg2 = readByte(curOffset++);
      const uint8_t arg3 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}", statusByte, arg1,
                         arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_UNKNOWN4: {
      const uint8_t arg1 = readByte(curOffset++);
      const uint8_t arg2 = readByte(curOffset++);
      const uint8_t arg3 = readByte(curOffset++);
      const uint8_t arg4 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}  Arg4: {:d}",
                         statusByte, arg1, arg2, arg3, arg4);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_NOP:
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc, Type::Nop);
      return true;

    case EVENT_NOP1:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc, Type::Nop);
      return true;

    case EVENT_END:
      if (shared->loopCount == 0) {
        if (readMode == READMODE_FIND_DELTA_LENGTH) {
          for (size_t trackIndex = 0; trackIndex < parentSeq.aTracks.size(); trackIndex++) {
            parentSeq.aTracks[trackIndex]->totalTicks = getTime();
          }
        } else if (readMode == READMODE_CONVERT_TO_MIDI) {
          for (size_t trackIndex = 0; trackIndex < parentSeq.aTracks.size(); trackIndex++) {
            parentSeq.aTracks[trackIndex]->limitPrevDurNoteEnd();
          }
        }

        parentSeq.deactivateAllTracks();
        continueReading = false;
        parentSeq.bIncTickAfterProcessingTracks = false;
      } else {
        shared->loopCount--;
        curOffset =
            (shared->loopCount == 0) ? shared->loopReturnAddress : shared->loopStartAddress;
      }
      return true;

    case EVENT_NOTE_PARAM:
      readStandardNoteParam(beginOffset, statusByte, desc);
      return true;

    case EVENT_NOTE: {
      const uint8_t noteNumber = statusByte - parentSeq.STATUS_NOTE_MIN;
      const uint8_t duration = getEffectiveNoteDuration();
      restoreNonPercussionProgramIfNeeded();
      addNoteByDur(beginOffset, curOffset - beginOffset, noteNumber, shared->spcNoteVolume / 2,
                   duration, "Note");
      m_lastNoteWasPercussion = false;
      addTime(shared->spcNoteDuration);
      return true;
    }

    case EVENT_TIE: {
      uint8_t duration = (shared->spcNoteDuration * shared->spcNoteDurRate) >> 8;
      duration = std::min(std::max(duration, static_cast<uint8_t>(1)),
                          static_cast<uint8_t>(shared->spcNoteDuration - 2));
      desc = fmt::format("Duration: {:d}", duration);
      makePrevDurNoteEnd(getTime() + duration);
      addTie(beginOffset, curOffset - beginOffset, duration, "Tie", desc);
      addTime(shared->spcNoteDuration);
      return true;
    }

    case EVENT_REST:
      addRest(beginOffset, curOffset - beginOffset, shared->spcNoteDuration);
      return true;

    case EVENT_PERCUSSION_NOTE: {
      const uint8_t slot = statusByte - parentSeq.STATUS_PERCUSSION_NOTE_MIN;
      const uint8_t duration = getEffectiveNoteDuration();

      if (isIntelliTablePercussionVersion(parentSeq.profileId) &&
          slot < NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT) {
        const auto percussionState = getIntelliPercussionNoteState(parentSeq, slot);
        applyIntelliPercussionState(percussionState.instrumentByte, percussionState.panByte,
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

      uint8_t instrIndex = 0;
      parentSeq.resolveProgramNumber(static_cast<uint8_t>(slot + parentSeq.spcPercussionBase),
                                     &instrIndex);

      const uint8_t noteIndex = 0x24 + instrIndex - parentSeq.spcPercussionBase;
      parentSeq.addPercussionInstrNoteMapping(instrIndex, noteIndex, parentSeq.globalTranspose);

      switchToPercussionProgramIfNeeded();
      addPercNoteByDur(beginOffset, curOffset - beginOffset,
                       static_cast<int8_t>(noteIndex - cKeyCorrection - parentSeq.globalTranspose),
                       shared->spcNoteVolume / 2, duration, "Percussion Note");
      m_lastNoteWasPercussion = true;
      addTime(shared->spcNoteDuration);
      return true;
    }

    case EVENT_PROGCHANGE: {
      const uint8_t instrumentByte = readByte(curOffset++);
      uint8_t logicalProgNum = 0;
      const uint32_t newProgNum = parentSeq.resolveProgramNumber(instrumentByte, &logicalProgNum);
      addProgramChangeEvent(beginOffset, curOffset - beginOffset, newProgNum, true,
                            "Program Change", logicalProgNum);
      return true;
    }

    case EVENT_CALL: {
      const uint16_t dest = getShortAddress(curOffset);
      curOffset += 2;
      const uint8_t times = readByte(curOffset++);

      shared->loopReturnAddress = curOffset;
      shared->loopStartAddress = dest;
      shared->loopCount = times;

      desc = fmt::format("Destination: ${:04X}  Times: {:d}", dest, times);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern Play", desc,
                      Type::RepeatStart);
      addPendingEndEvent(statusByte, desc);
      curOffset = shared->loopStartAddress;
      return true;
    }

    default:
      return false;
  }
}

bool NinSnesTrack::handleControllerEvent(NinSnesSeqEventType eventType, uint32_t beginOffset,
                                         std::string& desc) {
  auto& parentSeq = seq();

  switch (eventType) {
    case EVENT_PAN: {
      const uint8_t newPan = readByte(curOffset++);
      double volumeScale;
      bool reverseLeft;
      bool reverseRight;
      const int8_t midiPan = calculatePanValue(newPan, volumeScale, reverseLeft, reverseRight);
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));
      return true;
    }

    case EVENT_PAN_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      const uint8_t newPan = readByte(curOffset++);
      double volumeLeft;
      double volumeRight;
      getVolumeBalance(newPan << 8, volumeLeft, volumeRight);
      const uint8_t midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight);
      addPanSlide(beginOffset, curOffset - beginOffset, fadeLength, midiPan);
      return true;
    }

    case EVENT_VIBRATO_ON: {
      const uint8_t vibratoDelay = readByte(curOffset++);
      const uint8_t vibratoRate = readByte(curOffset++);
      const uint8_t vibratoDepth = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}", vibratoDelay, vibratoRate,
                         vibratoDepth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Vibrato);
      return true;
    }

    case EVENT_VIBRATO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Off", desc, Type::Vibrato);
      return true;

    case EVENT_MASTER_VOLUME: {
      const uint8_t newVol = readByte(curOffset++);
      addMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
      return true;
    }

    case EVENT_MASTER_VOLUME_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      const uint8_t newVol = readByte(curOffset++);
      desc = fmt::format("Length: {:d}  Volume: {:d}", fadeLength, newVol);
      addMastVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
      return true;
    }

    case EVENT_TEMPO: {
      const uint8_t newTempo = readByte(curOffset++);
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq.getTempoInBPM(newTempo));
      return true;
    }

    case EVENT_TEMPO_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      const uint8_t newTempo = readByte(curOffset++);
      addTempoSlide(beginOffset, curOffset - beginOffset, fadeLength,
                    static_cast<int>(60000000 / parentSeq.getTempoInBPM(newTempo)));
      return true;
    }

    case EVENT_GLOBAL_TRANSPOSE: {
      const int8_t semitones = readByte(curOffset++);
      parentSeq.globalTranspose = semitones;
      addGlobalTranspose(beginOffset, curOffset - beginOffset, semitones);
      return true;
    }

    case EVENT_TRANSPOSE: {
      const int8_t semitones = readByte(curOffset++);
      shared->spcTranspose = semitones;
      addTranspose(beginOffset, curOffset - beginOffset, semitones);
      return true;
    }

    case EVENT_TREMOLO_ON: {
      const uint8_t tremoloDelay = readByte(curOffset++);
      const uint8_t tremoloRate = readByte(curOffset++);
      const uint8_t tremoloDepth = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}", tremoloDelay, tremoloRate,
                         tremoloDepth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo", desc, Type::Tremelo);
      return true;
    }

    case EVENT_TREMOLO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Off", desc, Type::Tremelo);
      return true;

    case EVENT_VOLUME: {
      const uint8_t newVol = readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, newVol / 2);
      return true;
    }

    case EVENT_VOLUME_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      const uint8_t newVol = readByte(curOffset++);
      addVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
      return true;
    }

    case EVENT_VIBRATO_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      desc = fmt::format("Length: {:d}", fadeLength);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Fade", desc, Type::Vibrato);
      return true;
    }

    case EVENT_PITCH_ENVELOPE_TO: {
      const uint8_t pitchEnvDelay = readByte(curOffset++);
      const uint8_t pitchEnvLength = readByte(curOffset++);
      const int8_t pitchEnvSemitones = static_cast<int8_t>(readByte(curOffset++));
      desc = fmt::format("Delay: {:d}  Length: {:d}  Semitones: {:d}", pitchEnvDelay,
                         pitchEnvLength, pitchEnvSemitones);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope (To)", desc,
                      Type::PitchEnvelope);
      return true;
    }

    case EVENT_PITCH_ENVELOPE_FROM: {
      const uint8_t pitchEnvDelay = readByte(curOffset++);
      const uint8_t pitchEnvLength = readByte(curOffset++);
      const int8_t pitchEnvSemitones = static_cast<int8_t>(readByte(curOffset++));
      desc = fmt::format("Delay: {:d}  Length: {:d}  Semitones: {:d}", pitchEnvDelay,
                         pitchEnvLength, pitchEnvSemitones);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope (From)", desc,
                      Type::PitchEnvelope);
      return true;
    }

    case EVENT_PITCH_ENVELOPE_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope Off", desc,
                      Type::PitchEnvelope);
      return true;

    case EVENT_TUNING: {
      const uint8_t newTuning = readByte(curOffset++);
      addFineTuning(beginOffset, curOffset - beginOffset, (newTuning / 256.0) * 100.0);
      return true;
    }

    case EVENT_ECHO_ON: {
      const uint8_t spcEON = readByte(curOffset++);
      const uint8_t spcEVOL_L = readByte(curOffset++);
      const uint8_t spcEVOL_R = readByte(curOffset++);

      desc = "Channels: ";
      for (int channelNo = MAX_TRACKS - 1; channelNo >= 0; channelNo--) {
        if ((spcEON & (1 << channelNo)) != 0) {
          fmt::format_to(std::back_inserter(desc), "{}", channelNo);
          parentSeq.aTracks[channelNo]->addReverbNoItem(40);
        } else {
          desc.push_back('-');
          parentSeq.aTracks[channelNo]->addReverbNoItem(0);
        }
      }

      fmt::format_to(std::back_inserter(desc), "  Volume Left: {:d}  Volume Right: {:d}", spcEVOL_L,
                     spcEVOL_R);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo", desc, Type::Reverb);
      return true;
    }

    case EVENT_ECHO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Off", desc, Type::Reverb);
      return true;

    case EVENT_ECHO_PARAM: {
      const uint8_t spcEDL = readByte(curOffset++);
      const uint8_t spcEFB = readByte(curOffset++);
      const uint8_t spcFIR = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Feedback: {:d}  FIR: {:d}", spcEDL, spcEFB, spcFIR);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Param", desc, Type::Reverb);
      return true;
    }

    case EVENT_ECHO_VOLUME_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      const uint8_t spcEVOL_L = readByte(curOffset++);
      const uint8_t spcEVOL_R = readByte(curOffset++);
      desc = fmt::format("Length: {:d}  Volume Left: {:d}  Volume Right: {:d}", fadeLength,
                         spcEVOL_L, spcEVOL_R);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume Fade", desc,
                      Type::Reverb);
      return true;
    }

    case EVENT_PITCH_SLIDE: {
      const uint8_t pitchSlideDelay = readByte(curOffset++);
      const uint8_t pitchSlideLength = readByte(curOffset++);
      const uint8_t pitchSlideTargetNote = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Length: {:d}  Note: {:d}", pitchSlideDelay,
                         pitchSlideLength, static_cast<int>(pitchSlideTargetNote - 0x80));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide", desc,
                      Type::PitchBendSlide);
      return true;
    }

    case EVENT_PERCUSSION_PATCH_BASE: {
      const uint8_t percussionBase = readByte(curOffset++);
      parentSeq.spcPercussionBase = percussionBase;
      if (intelliMode() == NinSnesIntelliModeId::Ta) {
        parentSeq.setIntelliCustomPercTableEnabled(false);
      }

      desc = fmt::format("Percussion Base: {:d}", percussionBase);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion Base", desc,
                      Type::ChangeState);
      return true;
    }

    default:
      return false;
  }
}

bool NinSnesTrack::handleVariantEvent(NinSnesSeqEventType eventType, uint32_t beginOffset,
                                      uint8_t statusByte, std::string& desc) {
  auto& parentSeq = seq();

  switch (eventType) {
    case EVENT_RD2_PROGCHANGE_AND_ADSR: {
      const uint8_t instrumentByte = readByte(curOffset++);
      curOffset += 2;
      uint8_t logicalProgNum = 0;
      const uint32_t newProgNum = parentSeq.resolveProgramNumber(instrumentByte, &logicalProgNum);
      addProgramChangeEvent(beginOffset, curOffset - beginOffset, newProgNum, true,
                            "Program Change & ADSR", logicalProgNum);
      return true;
    }

    case EVENT_KONAMI_LOOP_START:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", desc, Type::RepeatStart);
      shared->konamiLoopStart = curOffset;
      shared->konamiLoopCount = 0;
      return true;

    case EVENT_KONAMI_LOOP_END: {
      const uint8_t times = readByte(curOffset++);
      const int8_t volumeDelta = readByte(curOffset++);
      const int8_t pitchDelta = readByte(curOffset++);
      desc = fmt::format("Times: {:d}  Volume Delta: {:d}  Pitch Delta: {:d}/16 semitones", times,
                         volumeDelta, pitchDelta);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc, Type::RepeatEnd);

      shared->konamiLoopCount++;
      if (shared->konamiLoopCount != times) {
        curOffset = shared->konamiLoopStart;
      }
      return true;
    }

    case EVENT_KONAMI_ADSR_AND_GAIN: {
      const uint8_t adsr1 = readByte(curOffset++);
      const uint8_t adsr2 = readByte(curOffset++);
      const uint8_t gain = readByte(curOffset++);
      desc = fmt::format("ADSR(1): ${:02X}  ADSR(2): ${:02X}  GAIN: ${:02X}", adsr1, adsr2, gain);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR/GAIN", desc, Type::Adsr);
      return true;
    }

    case EVENT_LEMMINGS_NOTE_PARAM:
      readLemmingsNoteParam(beginOffset, statusByte, desc);
      return true;

    case EVENT_QUINTET_TUNING: {
      const uint8_t newTuning = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Fine Tuning", desc, Type::FineTune);
      addFineTuningNoItem((newTuning / 256.0) * 61.8);
      return true;
    }

    case EVENT_QUINTET_ADSR: {
      const uint8_t adsr1 = readByte(curOffset++);
      const uint8_t sustainRate = readByte(curOffset++);
      const uint8_t sustainLevel = readByte(curOffset++);
      const uint8_t adsr2 = (sustainLevel << 5) | sustainRate;
      desc =
          fmt::format("ADSR(1): ${:02X}  Sustain Rate: {:d} Sustain Level: {}  (ADSR(2): ${:02X})",
                      adsr1, sustainRate, sustainLevel, adsr2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      return true;
    }

    default:
      return false;
  }
}

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
        addGenericEvent(beginOffset, curOffset - beginOffset, "Voice Param Table", desc,
                        Type::Misc);

        if (readMode == READMODE_ADD_TO_UI) {
          if (SeqEvent* event = findSeqEventAtOffset(beginOffset, curOffset - beginOffset);
              event != nullptr && event->children().empty()) {
            const bool packedVoiceParams = currentIntelliMode == NinSnesIntelliModeId::Ta ||
                                           currentIntelliMode == NinSnesIntelliModeId::Fe4;
            const auto& labels =
                packedVoiceParams ? INTELLI_PACKED_VOICE_PARAM_LABELS : INTELLI_VOICE_PARAM_LABELS;
            for (uint8_t itemIndex = 0; itemIndex < parentSeq.intelliVoiceParam.size; itemIndex++) {
              const uint32_t recordOffset = tableOffset + itemIndex * 4u;
              addIntelliTableItem(event, recordOffset, fmt::format("Voice Param {:d}", itemIndex),
                                  labels,
                                  {readByte(recordOffset), readByte(recordOffset + 1),
                                   readByte(recordOffset + 2), readByte(recordOffset + 3)});
            }
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
            setTrackedProgramState(newProgNum, instrNum);
            if (!m_lastNoteWasPercussion) {
              addProgramChangeNoItem(newProgNum, true);
            }
          }
        }

        addGenericEvent(beginOffset, curOffset - beginOffset, "Overwrite Instrument Region", desc,
                        Type::Misc);

        if (readMode == READMODE_ADD_TO_UI) {
          if (SeqEvent* event = findSeqEventAtOffset(beginOffset, curOffset - beginOffset);
              event != nullptr && event->children().empty()) {
            addIntelliTableItem(event, regionOffset, fmt::format("Instrument {:d}", instrNum),
                                INTELLI_OVERWRITE_LABELS, regionData);
          }
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
      const bool taStyleVoiceParam = currentIntelliMode == NinSnesIntelliModeId::Ta ||
                                     currentIntelliMode == NinSnesIntelliModeId::Fe4;
      const bool canReadVoiceParam =
          getIntelliVoiceParamAddress(parentSeq, paramIndex, addrVoiceParam, outOfRange);
      if (taStyleVoiceParam && outOfRange) {
        fmt::format_to(std::back_inserter(desc), "  Out of Declared Range");
      }

      if (canReadVoiceParam) {
        const uint8_t instrByte = readByte(addrVoiceParam);
        const uint8_t newVol = readByte(addrVoiceParam + 1);
        const uint8_t packedPanByte = readByte(addrVoiceParam + 2);
        const uint8_t lastByte = readByte(addrVoiceParam + 3);

        addVolNoItem(newVol / 2);

        double volumeScale;
        bool reverseLeft;
        bool reverseRight;
        uint8_t panValue = packedPanByte;
        double fineTuningCents = this->fineTuningCents;

        uint8_t logicalProgNum = 0;
        uint32_t resolvedProgNum = instrByte;

        if (taStyleVoiceParam) {
          panValue = packedPanByte & 0x1f;
          fineTuningCents = (((packedPanByte >> 5) & 0x07) * 5 / 256.0) * 100.0;
          resolvedProgNum = parentSeq.resolveProgramNumber(instrByte, &logicalProgNum);
        } else {
          uint8_t resolvedLogicalProgNum = instrByte;
          if (currentIntelliMode != NinSnesIntelliModeId::Fe3) {
            resolvedLogicalProgNum &= 0x1f;
          }
          logicalProgNum = resolvedLogicalProgNum;
          resolvedProgNum = resolvedLogicalProgNum;
        }

        const int8_t midiPan = calculatePanValue(panValue, volumeScale, reverseLeft, reverseRight);
        addPanNoItem(midiPan);
        addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));

        switch (currentIntelliMode) {
          case NinSnesIntelliModeId::Fe3: {
            const uint8_t tuningIndex = lastByte & 15;
            const uint8_t transposeIndex = (lastByte & 0x70) >> 4;

            if (tuningIndex != 0) {
              const uint8_t newTuning = (tuningIndex - 1) * 5;
              fineTuningCents = (newTuning / 256.0) * 100.0;
            }

            if (transposeIndex != 0) {
              constexpr std::array<int8_t, 7> FE3_TRANSPOSE_TABLE = {-24, -12, -1, 0, 1, 12, 24};
              const int8_t newTranspose = FE3_TRANSPOSE_TABLE[transposeIndex - 1];
              shared->spcTranspose = newTranspose;
              transpose = newTranspose;
            }
            break;
          }

          case NinSnesIntelliModeId::Fe4:
          case NinSnesIntelliModeId::Ta: {
            const int8_t newTranspose = lastByte;
            shared->spcTranspose = newTranspose;
            transpose = newTranspose;
            break;
          }

          default:
            break;
        }

        addFineTuningNoItem(fineTuningCents);

        setTrackedProgramState(resolvedProgNum, logicalProgNum);
        if (!m_lastNoteWasPercussion) {
          addProgramChangeNoItem(resolvedProgNum, true);
        }

        fmt::format_to(std::back_inserter(desc),
                       "  Instrument: ${:02X}  Volume: {:d}  Pan: {:d}  Tuning: {:.3f} semitones  "
                       "Transpose: {:d}",
                       instrByte, newVol, panValue, fineTuningCents / 100.0, shared->spcTranspose);
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
        addGenericEvent(beginOffset, curOffset - beginOffset, "Custom Percussion Table", desc,
                        Type::ChangeState);

        if (readMode == READMODE_ADD_TO_UI) {
          if (SeqEvent* event = findSeqEventAtOffset(beginOffset, curOffset - beginOffset);
              event != nullptr && event->children().empty()) {
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
          updateIntelliFlags(parentSeq.intelliPerc.flags, param, bitValue);

          if (param == 0x40) {
            desc = fmt::format("Status: {}", (bitValue ? "On" : "Off"));
            addGenericEvent(beginOffset, curOffset - beginOffset, "Use Custom Percussion Table",
                            desc, Type::ChangeState);
          } else {
            desc = fmt::format("Mask: ${:02X}", param);
            addGenericEvent(beginOffset, curOffset - beginOffset,
                            (type == 0x01) ? "Set Global Flags" : "Clear Global Flags", desc,
                            Type::ChangeState);
          }
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
          updateIntelliFlags(parentSeq.intelliPerc.flags, param, bitValue);
          desc = fmt::format("Mask: ${:02X}", param);
          addGenericEvent(beginOffset, curOffset - beginOffset,
                          (type == 0x01) ? "Set Global Flags" : "Clear Global Flags", desc,
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

void NinSnesTrack::addPendingEndEvent(uint8_t statusByte, const std::string& desc) {
  const auto& parentSeq = seq();
  if (curOffset + 1 <= 0x10000 && statusByte != parentSeq.STATUS_END &&
      readByte(curOffset) == parentSeq.STATUS_END) {
    addGenericEvent(curOffset, 1, (shared->loopCount == 0) ? "Section End" : "Pattern End", desc,
                    (shared->loopCount == 0) ? Type::TrackEnd : Type::RepeatEnd);
  }
}

bool NinSnesTrack::readEvent() {
  if (!available) {
    return false;
  }

  const auto& parentSeq = seq();
  const uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  const uint8_t statusByte = readByte(curOffset++);
  bool continueReading = true;
  std::string desc;

  NinSnesSeqEventType eventType = static_cast<NinSnesSeqEventType>(0);
  if (const auto pEventType = parentSeq.EventMap.find(statusByte);
      pEventType != parentSeq.EventMap.end()) {
    eventType = pEventType->second;
  }

  if (!handleCoreEvent(eventType, beginOffset, statusByte, desc, continueReading) &&
      !handleControllerEvent(eventType, beginOffset, desc) &&
      !handleVariantEvent(eventType, beginOffset, statusByte, desc) &&
      !handleIntelliEvent(eventType, beginOffset, statusByte, desc)) {
    const auto descr = logEvent(statusByte);
    addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
    continueReading = false;
  }

  addPendingEndEvent(statusByte, desc);
  return continueReading;
}

uint16_t NinSnesTrack::convertToApuAddress(uint16_t offset) {
  return seq().convertToAPUAddress(offset);
}

uint16_t NinSnesTrack::getShortAddress(uint32_t offset) {
  return seq().getShortAddress(offset);
}

void NinSnesTrack::getVolumeBalance(uint16_t pan, double& volumeLeft, double& volumeRight) {
  const auto& parentSeq = seq();
  getNinSnesVolumeBalance(parentSeq.profile(), parentSeq.panTable, pan, volumeLeft, volumeRight);
}

uint8_t NinSnesTrack::readPanTable(uint16_t pan) {
  const auto& parentSeq = seq();
  return readNinSnesPanTable(parentSeq.profile(), parentSeq.panTable, pan);
}

int8_t NinSnesTrack::calculatePanValue(uint8_t pan, double& volumeScale, bool& reverseLeft,
                                       bool& reverseRight) {
  const auto& parentSeq = seq();
  const auto panState = decodeNinSnesPanValue(parentSeq.profile(), pan);
  reverseLeft = panState.reverseLeft;
  reverseRight = panState.reverseRight;

  double volumeLeft;
  double volumeRight;
  getVolumeBalance(panState.panIndex << 8, volumeLeft, volumeRight);

  // TODO: correct volume scale of TOSE sequence
  int8_t midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &volumeScale);
  volumeScale = std::min(volumeScale, 1.0);  // workaround

  return midiPan;
}
