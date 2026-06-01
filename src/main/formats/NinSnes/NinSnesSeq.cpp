#include "NinSnesSeq.h"

#include "base/Types.h"
#include "NinSnesVibrato.h"

#include <algorithm>

DECLARE_FORMAT(NinSnes);

//  **********
//  NinSnesSeq
//  **********
constexpr u16 SEQ_PPQN = 48;
constexpr u16 ADDMUSICK_SEQ_PPQN = 24;

namespace {

constexpr size_t MAX_TRACKS = kNinSnesTrackCount;
constexpr u16 kNinSnesDefaultPitchBendRangeCents =
    NinSnesTrackState::kDefaultPitchBendRangeCents;

}  // namespace

NinSnesSeq::NinSnesSeq(RawFile* file, NinSnesProfileId profile, u32 offset,
                       u8 percussion_base, const std::vector<u8>& theVolumeTable,
                       const std::vector<u8>& theDurRateTable, std::string theName)
    : VGMSeq(NinSnesFormat::name, file, offset, 0, theName),
      signature(NinSnesSignatureId::None), profileId(profile),
      volumeTable(theVolumeTable), durRateTable(theDurRateTable),
      tempo(nin_snes::vibrato::kDefaultTempo),
      maxVibratoDepthCents(nin_snes::vibrato::kMinMaxDepthCents),
      maxVibratoRateHz(nin_snes::vibrato::kMinMaxRateHz),
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

  nNumTracks = static_cast<u32>(aTracks.size());
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
    maxVibratoDepthCents = nin_snes::vibrato::kMinMaxDepthCents;
    maxVibratoRateHz = nin_snes::vibrato::kMinMaxRateHz;
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
    intelliInstrumentProgramMap[program] = static_cast<u32>(program);
  }
}

void NinSnesSeq::createTracks() {
  if (!aTracks.empty()) {
    return;
  }

  aTracks.reserve(MAX_TRACKS);
  for (u32 trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    auto trackName = fmt::format("Track {}", trackIndex + 1);
    aTracks.push_back(new NinSnesTrack(this, dwStartOffset, 0, trackName));
  }
}

bool NinSnesSeq::loadTracks(ReadMode readMode, u32 stopTime) {
  this->readMode = readMode;
  for (auto* track : aTracks) {
    track->readMode = readMode;
  }

  curOffset = dwStartOffset;

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    timedEventIndex().clear();
  }

  resetVars();
  for (u32 trackNum = 0; trackNum < nNumTracks; trackNum++) {
    if (!aTracks[trackNum]->loadTrackInit(trackNum, nullptr)) {
      return false;
    }
  }

  const u32 stopOffset = vgmFile()->endOffset();
  while (curOffset < stopOffset && time < stopTime) {
    if (!readPlaylistEvent(stopTime)) {
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

bool NinSnesSeq::loadSection(NinSnesSection *section, u32 stopTime) {
  assert(aTracks.size() == nNumTracks);
  for (u32 trackNum = 0; trackNum < nNumTracks; trackNum++) {
    auto* track = static_cast<NinSnesTrack*>(aTracks[trackNum]);
    track->readMode = readMode;
    if (!track->prepareSectionTrack(*section, trackNum)) {
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
    const u32 distance = offset() - section->offset();
    setOffset(section->offset());
    if (length() != 0) {
      setLength(length() + distance);
    }
  }
  aSections.push_back(section);
}

NinSnesSection *NinSnesSeq::getSectionAtOffset(u32 offset) {
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
    return m_sectionForeverLoops <= conversionContext().sequenceLoops;
  }
  return true;
}

bool NinSnesSeq::parseHeader() {
  setPPQN(profileId == NinSnesProfileId::AddmusicK ? ADDMUSICK_SEQ_PPQN : SEQ_PPQN);
  nNumTracks = MAX_TRACKS;
  createTracks();

  if (dwStartOffset + 2 > 0x10000) {
    return false;
  }

  // validate first section
  u16 firstSectionPtr = dwStartOffset;
  u16 addrFirstSection = readShort(firstSectionPtr);
  if (addrFirstSection + 16 > 0x10000) {
    return false;
  }

  if (addrFirstSection >= 0x0100) {
    addrFirstSection = convertToAPUAddress(addrFirstSection);

    u8 numActiveTracks = 0;
    for (u8 trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
      u16 addrTrackStart = readShort(addrFirstSection + trackIndex * 2);
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

bool NinSnesSeq::readPlaylistEvent(long stopTime) {
  const auto& profile = this->profile();
  u32 beginOffset = curOffset;
  if (curOffset + 1 >= 0x10000) {
    return false;
  }

  const bool playlistOffsetVisited = isItemAtOffset(beginOffset, false);

  if (readMode == READMODE_ADD_TO_UI && curOffset < header->offset()) {
    u32 distance = header->offset() - curOffset;
    header->setOffset(curOffset);
    if (header->length() != 0) {
      header->setLength(header->length() + distance);
    }
  }

  u16 sectionAddress = readShort(curOffset);
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

    u16 repeatCount = sectionAddress;
    u16 dest = getShortAddress(curOffset);
    curOffset += 2;

    bool doJump = false;
    bool infiniteLoop = false;

    if (profile.playlistModel == NinSnesPlaylistModelId::Tose) {
      if (sectionRepeatCount == 0) {
        // set new repeat count
        sectionRepeatCount = static_cast<u8>(repeatCount);
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
        sectionRepeatCount = static_cast<u8>(repeatCount);
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
      header->addChild(beginOffset, 2, "Repeat Count");
      header->addChild(beginOffset + 2, 2, "Playlist Jump");

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
        u32 distance = offset() - curOffset;
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

double NinSnesSeq::getTempoInBPM(u8 tempoValue) {
  if (tempoValue != 0) {
    if (profileId == NinSnesProfileId::AddmusicK) {
      return static_cast<double>(tempoValue) * 2.5;
    }
    return static_cast<double>(60000000) / (SEQ_PPQN * 2000) * (static_cast<double>(tempoValue) / 256);
  } else {
    return 1.0;  // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}

void NinSnesSeq::setImmediateTempo(u8 newTempo) {
  tempo = newTempo;
  tempoFade.setCurrentRaw(newTempo);
}

void NinSnesSeq::startTempoFade(u8 fadeLength, u8 targetTempo) {
  tempoFade.begin(SeqFixedPointMotion<s32>::toRawTarget(targetTempo, fadeLength));
}

void NinSnesSeq::syncTempoDependentTracks() {
  for (auto* track : aTracks) {
    static_cast<NinSnesTrack*>(track)->syncVibratoRateAndDelay();
  }
}

void NinSnesSeq::onTickEnd() {
  // EVENT_TEMPO_FADE applies its first tempo step at the end of the tick that parsed the command.
  tempoFade.tickRaw([this](s32 tempoValue) {
    tempo = static_cast<u8>(std::clamp(tempoValue, 0, 0xff));
    if (!aTracks.empty()) {
      aTracks[0]->addTempoBPMNoItem(getTempoInBPM(tempo));
    }
    syncTempoDependentTracks();
  });
}

u16 NinSnesSeq::convertToAPUAddress(u16 offset) {
  return convertNinSnesAddress(profile(), offset, konamiBaseAddress, falcomBaseOffset);
}

u16 NinSnesSeq::getShortAddress(u32 offset) {
  return readNinSnesAddress(profile(), rawFile(), offset, konamiBaseAddress, falcomBaseOffset);
}

u32 NinSnesSeq::resolveProgramNumber(u8 instrumentByte,
                                          u8* logicalInstrIndex) const {
  return resolveNinSnesProgramNumber(profile(), rawFile(), instrumentByte, STATUS_PERCUSSION_NOTE_MIN,
                                     spcPercussionBase, quintetBGMInstrBase, quintetAddrBGMInstrLookup,
                                     &intelliInstrumentProgramMap, logicalInstrIndex);
}

//  **************
//  NinSnesSection
//  **************

NinSnesSection::NinSnesSection(NinSnesSeq* parentFile, u32 offset, u32 length)
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
  u32 curOffset = offset();

  VGMHeader* header = nullptr;
  if (parentSeq->readMode == READMODE_ADD_TO_UI) {
    header = addHeader(curOffset, 16);
  }

  u8 numActiveTracks = 0;
  for (size_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    if (curOffset + 1 >= 0x10000) {
      return false;
    }

    u16 startAddress = readShort(curOffset);
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
      m_tracks[trackIndex] = new NinSnesSectionTrackItem(parentSeq, curOffset, 2, "NULL");
      m_tracks[trackIndex]->readMode = parentSeq->readMode;
      curOffset += 2;
      continue;
    }

    startAddress = convertToApuAddress(startAddress);
    segment.startOffset = startAddress;
    segment.rangeOffset = startAddress;
    m_tracks[trackIndex] = new NinSnesSectionTrackItem(
        parentSeq, startAddress, 0, fmt::format("Track {}", trackIndex + 1));
    m_tracks[trackIndex]->readMode = parentSeq->readMode;

    // Correct section address. Probably not necessary for regular cases, but just in case.
    if (startAddress < offset()) {
      const u32 distance = offset() - startAddress;
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

u16 NinSnesSection::convertToApuAddress(u16 offset) {
  return parentSeq->convertToAPUAddress(offset);
}

u16 NinSnesSection::getShortAddress(u32 offset) {
  return parentSeq->getShortAddress(offset);
}
