#include "NinSnesSeq.h"
#include "NinSnesVibrato.h"
#include "Modulation.h"
#include "Options.h"
#include "SeqEvent.h"
#include "ScaleConversion.h"
#include "spdlog/fmt/fmt.h"
#include <algorithm>
#include <cmath>

DECLARE_FORMAT(NinSnes);

//  **********
//  NinSnesSeq
//  **********
#define SEQ_PPQN 48
#define SEQ_KEYOFS 24

namespace {

constexpr size_t MAX_TRACKS = kNinSnesTrackCount;
constexpr uint16_t kNinSnesDefaultPitchBendRangeCents =
    NinSnesTrackState::kDefaultPitchBendRangeCents;

uint8_t convertVibratoDepthToMidi(uint8_t depth, double maxDepthCents) {
  if (depth == 0) {
    return 0;
  }

  const double effectiveMaxDepthCents =
      (maxDepthCents > 0.0) ? maxDepthCents : nin_snes::vibrato::defaultMaxDepthCents();
  const int midiValue = static_cast<int>(
      std::lround(128.0 * nin_snes::vibrato::depthCents(depth) / effectiveMaxDepthCents));
  return static_cast<uint8_t>(std::clamp(midiValue, 0, 127));
}

uint8_t convertVibratoRateToMidi(uint8_t rate, double tempo, double maxRateHz) {
  const double currentRateHz = nin_snes::vibrato::rateHz(rate, tempo);
  if (currentRateHz <= 0.0) {
    return 0;
  }

  const double effectiveMaxRateHz =
      (maxRateHz > 0.0) ? maxRateHz : nin_snes::vibrato::defaultMaxRateHz();
  return midiValueForHertzInRange(currentRateHz,
                                  nin_snes::vibrato::minRateHz(),
                                  effectiveMaxRateHz);
}

uint8_t convertVibratoDelayToMidi(uint8_t delay, double tempo) {
  return midiValueForSecondsInRange(nin_snes::vibrato::delaySeconds(delay, tempo),
                                    nin_snes::vibrato::minDelaySeconds(),
                                    nin_snes::vibrato::maxDelaySeconds());
}

int32_t notePitch(uint8_t note) {
  // NinSnes stores slide pitch in semitone units with an 8-bit fractional part.
  return static_cast<int32_t>(note & 0x7f) << 8;
}

}  // namespace

NinSnesSeq::NinSnesSeq(RawFile* file, NinSnesProfileId profile, uint32_t offset,
                       uint8_t percussion_base, const std::vector<uint8_t>& theVolumeTable,
                       const std::vector<uint8_t>& theDurRateTable, std::string theName)
    : VGMSeq(NinSnesFormat::name, file, offset, 0, theName),
      signature(NinSnesSignatureId::None), profileId(profile),
      volumeTable(theVolumeTable), durRateTable(theDurRateTable),
      tempo(nin_snes::vibrato::kDefaultTempo),
      maxVibratoDepthCents(nin_snes::vibrato::minMaxDepthCents()),
      maxVibratoRateHz(nin_snes::vibrato::minMaxRateHz()),
      dwStartOffset(offset), curOffset(offset),
      konamiBaseAddress(0), quintetBGMInstrBase(0), falcomBaseOffset(0),
      header(NULL), spcPercussionBaseInit(percussion_base), m_nextIntelliTAOverrideProgram(0x80) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);
  setAlwaysWriteInitialPitchBendRange(kNinSnesDefaultPitchBendRangeCents);

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
  for (auto* track : aTracks) {
    delete track;
  }
  aTracks.clear();
}

bool NinSnesSeq::load() {
  readMode = READMODE_ADD_TO_UI;

  if (!parseHeader()) {
    return false;
  }

  nNumTracks = static_cast<uint32_t>(aTracks.size());
  if (nNumTracks == 0) {
    return false;
  }

  return loadTracks(readMode);
}

const NinSnesProfile& NinSnesSeq::profile() const {
  return getNinSnesProfile(profileId);
}

void NinSnesSeq::resetVars() {
  VGMSeq::resetVars();

  spcPercussionBase = spcPercussionBaseInit;
  sectionRepeatCount = 0;
  m_sectionForeverLoops = 0;
  globalTranspose = 0;
  setImmediateTempo(nin_snes::vibrato::kDefaultTempo);

  if (readMode != READMODE_CONVERT_TO_MIDI) {
    maxVibratoDepthCents = nin_snes::vibrato::minMaxDepthCents();
    maxVibratoRateHz = nin_snes::vibrato::minMaxRateHz();
  }
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
}

void NinSnesSeq::createTracks() {
  if (!aTracks.empty()) {
    return;
  }

  aTracks.reserve(MAX_TRACKS);
  for (uint32_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    auto trackName = fmt::format("Track {}", trackIndex + 1);
    aTracks.push_back(new NinSnesTrack(this, dwStartOffset, 0, trackName));
  }
}

bool NinSnesSeq::loadTracks(ReadMode readMode, uint32_t stopTime) {
  this->readMode = readMode;
  for (auto* track : aTracks) {
    track->readMode = readMode;
  }

  curOffset = dwStartOffset;

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    timedEventIndex().clear();
  }

  resetVars();
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (!aTracks[trackNum]->loadTrackInit(trackNum, nullptr)) {
      return false;
    }
  }

  const uint32_t stopOffset = vgmFile()->endOffset();
  while (curOffset < stopOffset && time < stopTime) {
    if (!readEvent(stopTime)) {
      break;
    }
  }

  return postLoad();
}

bool NinSnesSeq::postLoad() {
  if (readMode == READMODE_ADD_TO_UI) {
    std::ranges::sort(aInstrumentsUsed);

    for (auto* section : aSections) {
      section->postLoad();
    }

    addChildren(aSections);
    setGuessedLength();
    if (length() == 0) {
      return false;
    }
  } else if (readMode == READMODE_CONVERT_TO_MIDI) {
    midi->sort();
    timedEventIndex().finalize();
  }

  return true;
}

bool NinSnesSeq::loadSection(NinSnesSection *section, uint32_t stopTime) {
  assert(aTracks.size() == nNumTracks);
  for (uint32_t trackNum = 0; trackNum < nNumTracks; trackNum++) {
    auto* track = static_cast<NinSnesTrack*>(aTracks[trackNum]);
    track->readMode = readMode;
    if (!track->loadSectionSegment(*section, trackNum)) {
      return false;
    }
  }

  loadTracksMain(stopTime);
  if (readMode == READMODE_ADD_TO_UI) {
    section->markUiEventsLoaded();
  }
  return section->postLoad();
}

void NinSnesSeq::addSection(NinSnesSection *section) {
  if (offset() > section->offset()) {
    const uint32_t distance = offset() - section->offset();
    setOffset(section->offset());
    if (length() != 0) {
      setLength(length() + distance);
    }
  }
  aSections.push_back(section);
}

NinSnesSection *NinSnesSeq::getSectionAtOffset(uint32_t offset) {
  for (auto* section : aSections) {
    if (section->offset() == offset) {
      return section;
    }
  }
  return nullptr;
}

bool NinSnesSeq::addLoopForeverNoItem() {
  m_sectionForeverLoops++;
  if (readMode == READMODE_ADD_TO_UI) {
    return false;
  }
  if (readMode == READMODE_FIND_DELTA_LENGTH) {
    return m_sectionForeverLoops <= ConversionOptions::the().numSequenceLoops();
  }
  return true;
}

bool NinSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);
  nNumTracks = MAX_TRACKS;
  createTracks();

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

  const bool playlistOffsetVisited = isItemAtOffset(beginOffset, false);

  if (readMode == READMODE_ADD_TO_UI && curOffset < header->offset()) {
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
    if (readMode == READMODE_ADD_TO_UI && !playlistOffsetVisited) {
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

    bool doJump = false;
    bool infiniteLoop = false;

    if (profile.playlistModel == NinSnesPlaylistModelId::Tose) {
      if (sectionRepeatCount == 0) {
        // set new repeat count
        sectionRepeatCount = static_cast<uint8_t>(repeatCount);
        doJump = true;
      } else {
        // infinite loop?
        if (sectionRepeatCount == 0xff) {
          if (isItemAtOffset(dest, false)) {
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
        sectionRepeatCount = static_cast<uint8_t>(repeatCount);
      }

      // jump if the counter is zero
      if (sectionRepeatCount != 0) {
        doJump = true;
        if (isItemAtOffset(dest, false) && sectionRepeatCount > 0x80) {
          infiniteLoop = true;
        }
      }
    }

    // add event to sequence
    if (readMode == READMODE_ADD_TO_UI && !playlistOffsetVisited) {
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

      if (readMode == READMODE_ADD_TO_UI && curOffset < offset()) {
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
    if (readMode == READMODE_ADD_TO_UI && !playlistOffsetVisited) {
      header->addChild(beginOffset, curOffset - beginOffset, "Section Pointer");
    }

    NinSnesSection* section = getSectionAtOffset(sectionAddress);
    if (section == nullptr) {
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

double NinSnesSeq::getTempoInBPM() {
  return getTempoInBPM(tempo);
}

double NinSnesSeq::getTempoInBPM(uint8_t tempoValue) {
  if (tempoValue != 0) {
    return static_cast<double>(60000000) / (SEQ_PPQN * 2000) *
           (static_cast<double>(tempoValue) / 256);
  } else {
    return 1.0;  // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}

void NinSnesSeq::setImmediateTempo(uint8_t newTempo) {
  tempo = newTempo;
  tempoFade.setCurrent(newTempo);
}

void NinSnesSeq::startTempoFade(uint8_t fadeLength, uint8_t targetTempo) {
  tempoFade.setCurrent(tempo);
  tempoFade.startToTarget(targetTempo, fadeLength);
}

void NinSnesSeq::syncTempoDependentTracks() {
  for (auto* track : aTracks) {
    static_cast<NinSnesTrack*>(track)->syncVibratoRateAndDelay();
  }
}

void NinSnesSeq::onTickEnd() {
  // EVENT_TEMPO_FADE applies its first tempo step at the end of the tick that parsed the command.
  tempoFade.advanceAndApply([this](int32_t) {
    if (!aTracks.empty()) {
      static_cast<NinSnesTrack*>(aTracks[0])->applyCurrentTempo();
    }
  });
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

//  **************
//  NinSnesSection
//  **************

NinSnesSection::NinSnesSection(NinSnesSeq* parentFile, uint32_t offset, uint32_t length)
    : VGMItem(parentFile, offset, length, "Section", Type::Header),
      parentSeq(parentFile) {
}

bool NinSnesSection::load() {
  if (m_loaded) {
    return true;
  }

  if (!parseTrackPointers()) {
    return false;
  }

  m_loaded = true;
  return true;
}

bool NinSnesSection::parseTrackPointers() {
  uint32_t curOffset = offset();

  VGMHeader* header = nullptr;
  if (parentSeq->readMode == READMODE_ADD_TO_UI) {
    header = addHeader(curOffset, 16);
  }

  uint8_t numActiveTracks = 0;
  for (size_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    if (curOffset + 1 >= 0x10000) {
      return false;
    }

    uint16_t startAddress = readShort(curOffset);
    auto& segment = m_trackSegments[trackIndex];
    segment = {};
    segment.active = ((startAddress & 0xff00) != 0);

    if (header != nullptr) {
      header->addChild(curOffset, 2, fmt::format("Track Pointer #{}", trackIndex + 1));
    }

    if (!segment.active) {
      segment.startOffset = curOffset;
      segment.rangeOffset = curOffset;
      segment.rangeLength = 2;
      m_tracks[trackIndex] = new NinSnesSectionTrack(parentSeq, curOffset, 2, "NULL");
      m_tracks[trackIndex]->readMode = parentSeq->readMode;
      curOffset += 2;
      continue;
    }

    startAddress = convertToApuAddress(startAddress);
    segment.startOffset = startAddress;
    segment.rangeOffset = startAddress;
    m_tracks[trackIndex] = new NinSnesSectionTrack(
        parentSeq, startAddress, 0, fmt::format("Track {}", trackIndex + 1));
    m_tracks[trackIndex]->readMode = parentSeq->readMode;

    // Correct section address. Probably not necessary for regular cases, but just in case.
    if (startAddress < offset()) {
      const uint32_t distance = offset() - startAddress;
      setOffset(startAddress);
      if (length() != 0) {
        setLength(length() + distance);
      }
    }

    numActiveTracks++;
    curOffset += 2;
  }

  return numActiveTracks != 0;
}

bool NinSnesSection::postLoad() {
  if (parentSeq->readMode == READMODE_ADD_TO_UI) {
    for (auto* track : m_tracks) {
      if (track != nullptr) {
        track->sortChildrenByOffset();
      }
    }

    if (!m_tracksAddedToChildren) {
      for (auto* track : m_tracks) {
        if (track != nullptr) {
          addChild(track);
        }
      }
      m_tracksAddedToChildren = true;
    }
  }

  return true;
}

void NinSnesSection::markUiEventsLoaded() {
  for (auto& segment : m_trackSegments) {
    segment.uiEventsLoaded = true;
  }
}

uint16_t NinSnesSection::convertToApuAddress(uint16_t offset) {
  return parentSeq->convertToAPUAddress(offset);
}

uint16_t NinSnesSection::getShortAddress(uint32_t offset) {
  return parentSeq->getShortAddress(offset);
}

NinSnesTrackState::NinSnesTrackState() {
  resetVars();
}

void NinSnesTrackState::resetVars(void) {
  loopCount = 0;
  spcTranspose = 0;

  // just in case
  spcNoteDuration = 1;
  spcNoteDurRate = 0xfc;
  spcNoteVolume = 0xfc;

  // Konami:
  konamiLoopStart = 0;
  konamiLoopCount = 0;

  vibrato.reset();
  pitchEnvelope = {};
  pitch.reset(kDefaultPitchBendRangeCents);
}

//  ************
//  NinSnesTrack
//  ************

NinSnesSectionTrack::NinSnesSectionTrack(NinSnesSeq* parentSeq, uint32_t offset, uint32_t length,
                                         const std::string& theName)
    : SeqTrack(parentSeq, offset, length, theName) {
  bDetermineTrackLengthEventByEvent = true;
}

NinSnesTrack::NinSnesTrack(NinSnesSeq* parentSeq, uint32_t offset, uint32_t length,
                           const std::string& theName)
    : SeqTrack(parentSeq, offset, length, theName) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
}

bool NinSnesTrack::loadSectionSegment(NinSnesSection& section, uint32_t trackIndex) {
  resetTransientSectionState(trackIndex);
  currentSegment = &section.trackSegment(trackIndex);
  auto* sectionTrack = section.track(trackIndex);
  const bool loaded = loadTrackSegmentInit(currentSegment->rangeOffset,
                                           currentSegment->rangeLength,
                                           currentSegment->active,
                                           currentSegment->startOffset);
  if (sectionTrack != nullptr) {
    sectionTrack->readMode = readMode;
    sectionTrack->channel = channel;
    sectionTrack->channelGroup = channelGroup;
    setUiEventOwner(sectionTrack);
  } else {
    clearUiEventOwner();
  }
  setUiEventEmissionEnabled(readMode != READMODE_ADD_TO_UI || !currentSegment->uiEventsLoaded);

  if (!currentSegment->active && m_hasPersistentRange) {
    setRange(m_persistentRangeStart, m_persistentRangeEnd - m_persistentRangeStart);
  }
  return loaded;
}

void NinSnesTrack::resetTransientSectionState(uint32_t trackIndex) {
  resetVisitedAddresses();
  SeqTrack::resetVars();

  cKeyCorrection = SEQ_KEYOFS;
  transpose = state.spcTranspose;
  m_lastNoteWasPercussion = false;
  nonPercussionProgram = 0;
  currentPercussionProgram = 0;
  currentLogicalProgram.reset();
  intelliLegato = false;
  currentSegment = nullptr;
  resetPersistentRange();
  setChannelAndGroupFromTrkNum(static_cast<int>(trackIndex));
}

void NinSnesTrack::resetPersistentRange() {
  m_hasPersistentRange = false;
  m_persistentRangeStart = 0;
  m_persistentRangeEnd = 0;
}

void NinSnesTrack::includePersistentRange(uint32_t eventOffset, uint32_t eventLength) {
  const uint32_t eventEnd = eventOffset + eventLength;
  if (!m_hasPersistentRange) {
    m_hasPersistentRange = true;
    m_persistentRangeStart = eventOffset;
    m_persistentRangeEnd = eventEnd;
  } else {
    m_persistentRangeStart = std::min(m_persistentRangeStart, eventOffset);
    m_persistentRangeEnd = std::max(m_persistentRangeEnd, eventEnd);
  }

  setRange(m_persistentRangeStart, m_persistentRangeEnd - m_persistentRangeStart);
}

bool NinSnesTrack::onEvent(uint32_t offset, uint32_t length) {
  const bool isNewOffset = SeqTrack::onEvent(offset, length);
  if (currentSegment != nullptr && readMode == READMODE_ADD_TO_UI) {
    currentSegment->include(offset, length);
  }
  includePersistentRange(offset, length);
  return isNewOffset;
}

void NinSnesTrack::resetVars() {
  SeqTrack::resetVars();

  cKeyCorrection = SEQ_KEYOFS;
  state.resetVars();
  transpose = state.spcTranspose;
  m_lastNoteWasPercussion = false;
  nonPercussionProgram = 0;
  currentPercussionProgram = 0;
  currentLogicalProgram.reset();
  intelliLegato = false;
  currentSegment = nullptr;
  resetPersistentRange();
}

void NinSnesTrack::onTickBegin() {
  updateVibratoFade();
  updatePitchSlide();

  // NinSnes can queue another F9 directly after the current note. If the active pitch motion
  // finishes before the note does, the driver picks up that waiting slide without advancing the
  // main event stream to the next note boundary.
  if (deltaTime > 1 && state.pitch.motionTicksRemaining() == 0) {
    consumeQueuedPitchSlide();
  }
}

uint8_t NinSnesTrack::getEffectiveNoteDuration() const {
  if (intelliLegato) {
    return state.spcNoteDuration;
  }

  uint8_t duration = (state.spcNoteDuration * state.spcNoteDurRate) >> 8;
  return std::min(std::max(duration, static_cast<uint8_t>(1)),
                  static_cast<uint8_t>(state.spcNoteDuration - 2));
}

// Remembers the melodic program restored after percussion notes.
void NinSnesTrack::rememberMelodicProgram(uint32_t progNum,
                                          std::optional<uint8_t> logicalProgram) {
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

void NinSnesTrack::addProgramChangeEvent(uint32_t offset, uint32_t length, uint32_t progNum,
                                         bool requireBank, const std::string& eventName,
                                         std::optional<uint8_t> logicalProgram) {
  rememberMelodicProgram(progNum, logicalProgram);

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

NinSnesTrack::PitchSlideEvent NinSnesTrack::readPitchSlide(uint32_t offset) {
  return PitchSlideEvent {
    offset,
    4,
    readByte(curOffset++),
    readByte(curOffset++),
    readByte(curOffset++),
  };
}

bool NinSnesTrack::consumeQueuedPitchSlide() {
  if (state.pitch.motionTicksRemaining() != 0) {
    return false;
  }

  if (curOffset + 3 >= 0x10000) {
    return false;
  }

  const auto statusByte = readByte(curOffset);
  auto nextEvent = seq().EventMap.find(statusByte);
  if (nextEvent == seq().EventMap.end() || nextEvent->second != EVENT_PITCH_SLIDE) {
    return false;
  }

  const auto slideOffset = curOffset++;
  auto slide = readPitchSlide(slideOffset);
  addPitchSlideEvent(slide);
  beginPitchSlide(slide);
  return true;
}

void NinSnesTrack::addPitchSlideEvent(const PitchSlideEvent& slide) {
  auto desc = fmt::format("Delay: {:d}  Length: {:d}  Target Note: {:d}",
                          slide.delay,
                          slide.length,
                          slide.targetNote & 0x7f);
  addGenericEvent(slide.offset, slide.eventLength, "Pitch Slide", desc, Type::PitchBendSlide);
}

void NinSnesTrack::beginPitchSlide(const PitchSlideEvent& slide) {
  clearActivePitchSlide();
  activatePitchMotion(slide.delay, slide.length, notePitch(slide.targetNote));
}

void NinSnesTrack::activatePitchMotion(uint8_t delay, uint8_t length, int32_t targetPitch) {
  auto& pitch = state.pitch;
  if (!pitch.baseValid() || length == 0) {
    return;
  }

  // F1/F2 and F9 all reduce to the same live pitch-motion state: wait for an optional delay,
  // advance by a signed 8.8 delta each tick, then snap exactly to the stored target.
  const int32_t currentPitch = pitch.currentPitch();
  beginPitchBendLaneMotion(
      pitch,
      pitch.motionToTarget(targetPitch, length, delay),
      pitch.rangeCentsForSlide(currentPitch, targetPitch, kNinSnesDefaultPitchBendRangeCents),
      true);
}

void NinSnesTrack::clearActivePitchSlide() {
  state.pitch.clearMotion();
}

void NinSnesTrack::updatePitchSlide() {
  auto& pitch = state.pitch;
  if (!pitch.motionActive()) {
    return;
  }

  advancePitchBendLane(pitch);
}

void NinSnesTrack::beginNotePitch(uint8_t note) {
  resetPitchBendForNewNote();
  state.pitch.beginNote(notePitch(note));
  activateStoredPitchEnvelope();
  beginNoteVibrato();
}

void NinSnesTrack::activateStoredPitchEnvelope() {
  const auto& pitchEnvelope = state.pitchEnvelope;
  if (!pitchEnvelope.enabled()) {
    return;
  }

  const int32_t semitoneOffset = static_cast<int32_t>(pitchEnvelope.semitones) * 256;
  int32_t targetPitch = state.pitch.basePitch();
  if (pitchEnvelope.mode == NinSnesTrackState::StoredPitchEnvelope::Mode::To) {
    targetPitch += semitoneOffset;
  } else {
    state.pitch.setCurrentPitch(state.pitch.basePitch() - semitoneOffset);
  }

  activatePitchMotion(pitchEnvelope.delay, pitchEnvelope.length, targetPitch);
}

void NinSnesTrack::beginNoteVibrato() {
  auto& vibrato = state.vibrato;
  if (!vibrato.active()) {
    return;
  }

  // EVENT_VIBRATO_FADE is a reusable per-note fade-in for the currently configured E3 vibrato, so a real note-on
  // either restarts that fade from zero depth or restores the steady-state depth immediately.
  if (vibrato.hasReusableFade()) {
    vibrato.startReusableFadeToConfiguredDepth();
    setVibratoDepth(0);
  } else {
    setVibratoDepth(vibrato.depth());
  }
}

void NinSnesTrack::applyConfiguredVibrato() {
  auto& vibrato = state.vibrato;
  if (vibrato.active()) {
    auto& parentSeq = seq();
    if (readMode != READMODE_CONVERT_TO_MIDI) {
      parentSeq.maxVibratoDepthCents =
          std::max(parentSeq.maxVibratoDepthCents,
                   nin_snes::vibrato::depthCents(vibrato.depth()));
    }
    setVibratoDepth(vibrato.depth());
    syncVibratoRateAndDelay();
    return;
  }

  setVibratoDepth(0);
  clearVibratoRateAndDelay();
}

void NinSnesTrack::updateVibratoFade() {
  auto& vibrato = state.vibrato;
  vibrato.advanceFadeAndApplyClamped(0, [this](int32_t depth) {
    setVibratoDepth(static_cast<uint8_t>(depth));
  });
}

void NinSnesTrack::setVibratoDepth(uint8_t depth) {
  auto& vibrato = state.vibrato;
  vibrato.setCurrentDepth(depth);
  const uint8_t midiDepth = convertVibratoDepthToMidi(depth, seq().maxVibratoDepthCents);
  setSynthLfoModulationDepth(vibrato, midiDepth);
}

void NinSnesTrack::clearVibratoRateAndDelay() {
  addChannelPressureNoItem(0);
  addControllerEventNoItem(nin_snes::vibrato::kDelayController, 0);
}

void NinSnesTrack::resetPitchBendForNewNote() {
  clearActivePitchSlide();
  state.pitch.invalidateBase();
  if (state.pitch.atRest(kNinSnesDefaultPitchBendRangeCents)) {
    return;
  }

  resetPitchBendLane(state.pitch, kNinSnesDefaultPitchBendRangeCents);
}

void NinSnesTrack::syncVibratoRateAndDelay() {
  const auto& vibrato = state.vibrato;
  if (!vibrato.active()) {
    return;
  }

  auto& parentSeq = seq();
  const double currentTempo = parentSeq.tempo;
  if (readMode != READMODE_CONVERT_TO_MIDI) {
    // Rate and delay are both tempo-relative, so a sequence-specific max only makes sense once the
    // current tempo has been folded into the exported Hz value.
    parentSeq.maxVibratoRateHz =
        std::max(parentSeq.maxVibratoRateHz,
                 nin_snes::vibrato::rateHz(vibrato.rate(), currentTempo));
  }

  addChannelPressureNoItem(convertVibratoRateToMidi(vibrato.rate(), currentTempo, parentSeq.maxVibratoRateHz));
  addControllerEventNoItem(nin_snes::vibrato::kDelayController,
                           convertVibratoDelayToMidi(vibrato.delay(), currentTempo));
}

void NinSnesTrack::applyCurrentTempo() {
  auto& parentSeq = seq();
  const uint8_t newTempo = static_cast<uint8_t>(
      std::clamp(parentSeq.tempoFade.currentRaw(), 0, 0xff));
  if (newTempo == parentSeq.tempo) {
    return;
  }

  parentSeq.tempo = newTempo;
  addTempoBPMNoItem(parentSeq.getTempoInBPM(newTempo));
  parentSeq.syncTempoDependentTracks();
}

void NinSnesTrack::readStandardNoteParam(uint32_t beginOffset, uint8_t statusByte,
                                         std::string& desc) {
  auto& parentSeq = seq();
  state.spcNoteDuration = statusByte;
  desc = fmt::format("Duration: {:d}", state.spcNoteDuration);

  if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
    const uint8_t quantizeAndVelocity = readByte(curOffset++);
    const uint8_t durIndex = (quantizeAndVelocity >> 4) & 7;
    const uint8_t velIndex = quantizeAndVelocity & 15;

    state.spcNoteDurRate = parentSeq.durRateTable[durIndex];
    state.spcNoteVolume = parentSeq.volumeTable[velIndex];

    fmt::format_to(std::back_inserter(desc),
                   "  Quantize: {:d} ({:d}/256)  Velocity: {:d} ({:d}/256)", durIndex,
                   state.spcNoteDurRate, velIndex, state.spcNoteVolume);
  }

  addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::DurationChange);
}

void NinSnesTrack::readLemmingsNoteParam(uint32_t beginOffset, uint8_t statusByte,
                                         std::string& desc) {
  state.spcNoteDuration = statusByte;
  desc = fmt::format("Duration: {:d}", state.spcNoteDuration);

  if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
    const uint8_t durByte = readByte(curOffset++);
    state.spcNoteDurRate = (durByte << 1) + (durByte >> 1) + (durByte & 1);
    fmt::format_to(std::back_inserter(desc), "  Quantize: {} ({}/256)", durByte,
                   state.spcNoteDurRate);

    if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
      const uint8_t velByte = readByte(curOffset++);
      state.spcNoteVolume = velByte << 1;
      fmt::format_to(std::back_inserter(desc), "  Velocity: {} ({}/256)", velByte,
                     state.spcNoteVolume);
    }
  }

  addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::DurationChange);
}

void NinSnesTrack::addPendingEndEvent(uint8_t statusByte, const std::string& desc) {
  const auto& parentSeq = seq();
  if (curOffset + 1 <= 0x10000 && statusByte != parentSeq.STATUS_END &&
      readByte(curOffset) == parentSeq.STATUS_END) {
    addGenericEvent(curOffset, 1, (state.loopCount == 0) ? "Section End" : "Pattern End", desc,
                    (state.loopCount == 0) ? Type::TrackEnd : Type::RepeatEnd);
  }
}

bool NinSnesTrack::readEvent() {
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
