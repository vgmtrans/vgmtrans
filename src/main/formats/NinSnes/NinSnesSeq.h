#pragma once
#include <algorithm>
#include <array>
#include <optional>
#include "VGMSeq.h"
#include "SeqMotionLanes.h"
#include "SeqTrack.h"
#include "NinSnesFormat.h"
#include "NinSnesScanResult.h"
#include "NinSnesSeqState.h"

struct NinSnesProfile;
class NinSnesSection;
class NinSnesSectionTrackItem;

constexpr size_t kNinSnesTrackCount = 8;

class NinSnesSeq : public VGMSeq {
public:
  NinSnesSeq(RawFile* file, NinSnesProfileId profile, uint32_t offset, uint8_t percussion_base = 0,
             const std::vector<uint8_t>& theVolumeTable = std::vector<uint8_t>(),
             const std::vector<uint8_t>& theDurRateTable = std::vector<uint8_t>(),
             std::string theName = "NinSnes Seq");
  NinSnesSeq(RawFile* file, const NinSnesScanResult& scanResult);
  virtual ~NinSnesSeq();

  bool load() override;
  bool parseHeader() override;
  void resetVars() override;
  void onTickEnd() override;
  bool readPlaylistEvent(long stopTime);

  const NinSnesProfile& profile() const;
  double getTempoInBPM();
  double getTempoInBPM(uint8_t tempo);

  uint16_t convertToAPUAddress(uint16_t offset);
  uint16_t getShortAddress(uint32_t offset);
  uint32_t resolveProgramNumber(uint8_t instrumentByte, uint8_t* logicalInstrIndex = nullptr) const;
  uint32_t registerIntelliTAInstrumentOverride(uint8_t logicalInstrIndex,
                                               const std::array<uint8_t, 6>& regionData);
  uint8_t ensureIntelliTADrumKitProgram();
  bool usesIntelliCustomPercTable() const;
  void setIntelliCustomPercTableEnabled(bool enabled);

  NinSnesSignatureId signature;
  NinSnesProfileId profileId;
  uint8_t STATUS_END;
  uint8_t STATUS_NOTE_MIN;
  uint8_t STATUS_NOTE_MAX;
  uint8_t STATUS_PERCUSSION_NOTE_MIN;
  uint8_t STATUS_PERCUSSION_NOTE_MAX;
  std::map<uint8_t, NinSnesSeqEventType> EventMap;

  std::vector<uint8_t> volumeTable;
  std::vector<uint8_t> durRateTable;
  std::vector<uint8_t> panTable;

  uint8_t spcPercussionBase;
  uint8_t sectionRepeatCount;
  int8_t globalTranspose;
  uint8_t tempo;
  FixedPointControllerLane<> tempoFade;
  double maxVibratoDepthCents;
  double maxVibratoRateHz;
  uint32_t dwStartOffset;
  uint32_t curOffset;
  std::vector<NinSnesSection *> aSections;

  // Konami:
  uint16_t konamiBaseAddress;

  // Intelligent Systems:
  std::vector<uint8_t> intelliDurVolTable;
  bool intelliUseCustomNoteParam;
  bool intelliUseCustomPercTable;
  NinSnesIntelliVoiceParamState intelliVoiceParam;
  NinSnesIntelliPercussionState intelliPerc;
  std::array<uint32_t, 0x80> intelliInstrumentProgramMap;

  // Quintet
  uint8_t quintetBGMInstrBase;
  uint16_t quintetAddrBGMInstrLookup;

  // Falcom:
  uint16_t falcomBaseOffset;

  void addPercussionInstrNoteMapping(uint8_t instrIndex, uint8_t noteIndex,
                                     int8_t transposeSemitones) {
    m_percussionInstrNoteMap[instrIndex] = {noteIndex, transposeSemitones};
  }
  const std::map<uint8_t, NinSnesPercussionDef>& percussionInstrNoteMap() const {
    return m_percussionInstrNoteMap;
  }
  const std::vector<NinSnesIntelliTAInstrumentOverride>& intelliTAInstrumentOverrides() const {
    return m_intelliTAInstrumentOverrides;
  }
  const std::vector<NinSnesIntelliTADrumKitDef>& intelliTADrumKitDefs() const {
    return m_intelliTADrumKitDefs;
  }

protected:
  bool loadTracks(ReadMode readMode, uint32_t stopTime = 1000000) override;
  bool postLoad() override;

  VGMHeader* header;

private:
  friend class NinSnesTrack;
  void loadEventMap();
  void createTracks();
  bool loadSection(NinSnesSection *section, uint32_t stopTime = 1000000);
  void addSection(NinSnesSection *section);
  NinSnesSection *getSectionAtOffset(uint32_t offset);
  bool addLoopForeverNoItem();
  void setImmediateTempo(uint8_t newTempo);
  void startTempoFade(uint8_t fadeLength, uint8_t targetTempo);
  void syncTempoDependentTracks();
  NinSnesIntelliTADrumKitDef buildIntelliTADrumKitDef() const;

  uint8_t spcPercussionBaseInit;
  int m_sectionForeverLoops = 0;
  std::map<uint8_t, NinSnesPercussionDef> m_percussionInstrNoteMap;
  uint32_t m_nextIntelliTAOverrideProgram;
  std::vector<NinSnesIntelliTAInstrumentOverride> m_intelliTAInstrumentOverrides;
  std::vector<NinSnesIntelliTADrumKitDef> m_intelliTADrumKitDefs;
};

class NinSnesSection : public VGMItem {
public:
  struct TrackSegment {
    // The pointer target remains the parser entry point. Jumps can expand the
    // discovered byte range before that address, so the UI/stop range is separate.
    uint32_t startOffset = 0;
    uint32_t rangeOffset = 0;
    uint32_t rangeLength = 0;
    bool active = false;
    bool uiEventsLoaded = false;

    [[nodiscard]] uint32_t stopOffset() const {
      return rangeOffset + rangeLength;
    }

    void include(uint32_t eventOffset, uint32_t eventLength) {
      if (rangeLength == 0) {
        rangeOffset = eventOffset;
        rangeLength = eventLength;
        return;
      }

      const uint32_t start = std::min(rangeOffset, eventOffset);
      const uint32_t end = std::max(stopOffset(), eventOffset + eventLength);
      rangeOffset = start;
      rangeLength = end - start;
    }
  };

  NinSnesSection(NinSnesSeq* parentFile, uint32_t offset = 0, uint32_t length = 0);

  bool load();
  bool parseTrackPointers();
  bool postLoad();
  void markUiEventsLoaded();

  [[nodiscard]] const TrackSegment& trackSegment(size_t index) const {
    return m_trackSegments.at(index);
  }
  TrackSegment& trackSegment(size_t index) {
    return m_trackSegments.at(index);
  }
  NinSnesSectionTrackItem* track(size_t index) {
    return m_tracks.at(index);
  }

  uint16_t convertToApuAddress(uint16_t offset);
  uint16_t getShortAddress(uint32_t offset);

  NinSnesSeq* parentSeq;

 private:
  bool m_loaded = false;
  bool m_tracksAddedToChildren = false;
  std::array<TrackSegment, kNinSnesTrackCount> m_trackSegments {};
  std::array<NinSnesSectionTrackItem*, kNinSnesTrackCount> m_tracks {};
};

class NinSnesSectionTrackItem : public SeqTrack {
public:
  NinSnesSectionTrackItem(NinSnesSeq* parentSeq, uint32_t offset, uint32_t length,
                          const std::string& theName);
};

class NinSnesTrack : public SeqTrack {
public:
  struct PitchSlideEvent {
    uint32_t offset = 0;
    uint32_t eventLength = 0;
    uint8_t delay = 0;
    uint8_t length = 0;
    uint8_t targetNote = 0;
  };

  NinSnesTrack(NinSnesSeq* parentSeq, uint32_t offset = 0, uint32_t length = 0,
               const std::string& theName = "NinSnes Track");

  void resetVars() override;
  void onTickBegin() override;
  bool readEvent() override;

  uint16_t convertToApuAddress(uint16_t offset);
  uint16_t getShortAddress(uint32_t offset);
  void getVolumeBalance(uint16_t pan, double& volumeLeft, double& volumeRight);
  uint8_t readPanTable(uint16_t pan);
  int8_t calculatePanValue(uint8_t pan, double& volumeScale, bool& reverseLeft, bool& reverseRight);

protected:
  bool onEvent(uint32_t offset, uint32_t length) override;

private:
  friend class NinSnesSeq;
  bool prepareSectionTrack(NinSnesSection& section, uint32_t trackIndex);
  void resetTransientSectionState(uint32_t trackIndex);
  void resetTransientPlaybackState();
  void resetPersistentRange();
  void includePersistentRange(uint32_t eventOffset, uint32_t eventLength);
  NinSnesSeq& seq() const;
  NinSnesIntelliModeId intelliMode() const;
  bool handleIntelliPercussionNote(uint32_t beginOffset, uint8_t slot, uint8_t duration);
  void readStandardNoteParam(uint32_t beginOffset, uint8_t statusByte, std::string& desc);
  void readLemmingsNoteParam(uint32_t beginOffset, uint8_t statusByte, std::string& desc);
  void readIntelliNoteParam(uint32_t beginOffset, uint8_t statusByte, std::string& desc);
  bool handleCoreEvent(NinSnesSeqEventType eventType, uint32_t beginOffset, uint8_t statusByte,
                       std::string& desc, bool& continueReading);
  bool handleControllerEvent(NinSnesSeqEventType eventType, uint32_t beginOffset,
                             std::string& desc);
  bool handleVariantEvent(NinSnesSeqEventType eventType, uint32_t beginOffset, uint8_t statusByte,
                          std::string& desc);
  bool handleIntelliEvent(NinSnesSeqEventType eventType, uint32_t beginOffset, uint8_t statusByte,
                          std::string& desc);
  PitchSlideEvent readPitchSlide(uint32_t offset);
  bool consumeQueuedPitchSlide();
  void addPitchSlideEvent(const PitchSlideEvent& slide);
  void beginPitchSlide(const PitchSlideEvent& slide);
  void activatePitchMotion(uint8_t delay, uint8_t length, int32_t targetPitch);
  void clearActivePitchSlide();
  void updatePitchSlide();
  void beginNotePitch(uint8_t note);
  void activateStoredPitchEnvelope();
  void beginNoteVibrato();
  void updateVibratoFade();
  void applyConfiguredVibrato();
  void clearVibratoRateAndDelay();
  void setVibratoDepth(uint8_t depth);
  void resetPitchBendForNewNote();
  void addPendingEndEvent(uint8_t statusByte, const std::string& desc);
  void syncVibratoRateAndDelay();

  uint8_t getEffectiveNoteDuration() const;
  void rememberMelodicProgram(uint32_t progNum,
                              std::optional<uint8_t> logicalProgram = std::nullopt);
  void restoreNonPercussionProgramIfNeeded();
  void switchToPercussionProgramIfNeeded(uint8_t program = 0);
  void applyIntelliPercussionSlotState(uint8_t instrumentByte,
                                       std::optional<uint8_t> panByte = std::nullopt,
                                       std::optional<uint8_t> reverbLevel = std::nullopt);
  void addProgramChangeEvent(uint32_t offset, uint32_t length, uint32_t progNum, bool requireBank,
                             const std::string& eventName = "Program Change",
                             std::optional<uint8_t> logicalProgram = std::nullopt);

  bool m_lastNoteWasPercussion = false;
  uint32_t nonPercussionProgram = 0;
  uint8_t currentPercussionProgram = 0;
  std::optional<uint8_t> currentLogicalProgram;
  bool intelliLegato = false;
  NinSnesTrackState state;
  NinSnesSection::TrackSegment* currentSegment = nullptr;
  bool m_hasPersistentRange = false;
  uint32_t m_persistentRangeStart = 0;
  uint32_t m_persistentRangeEnd = 0;
};
