#pragma once

#include "automation/SeqAutomation.h"
#include "base/Types.h"
#include "NinSnesFormat.h"
#include "NinSnesScanResult.h"
#include "NinSnesSeqState.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct NinSnesProfile;
class NinSnesSection;
class NinSnesSectionTrackItem;

constexpr size_t kNinSnesTrackCount = 8;

class NinSnesSeq : public VGMSeq {
public:
  NinSnesSeq(RawFile* file, NinSnesProfileId profile, u32 offset, u8 percussion_base = 0,
             const std::vector<u8>& volumeTable = std::vector<u8>(),
             const std::vector<u8>& durRateTable = std::vector<u8>(),
             std::string name = "NinSnes Seq");
  NinSnesSeq(RawFile* file, const NinSnesScanResult& scanResult);
  virtual ~NinSnesSeq();

  bool load() override;
  bool parseHeader() override;
  void resetVars() override;
  void onTickEnd() override;
  bool readPlaylistEvent(long stopTime);

  const NinSnesProfile& profile() const;
  double getTempoInBPM();
  double getTempoInBPM(u8 tempo);

  u16 convertToAPUAddress(u16 offset);
  u16 getShortAddress(u32 offset);
  u32 resolveProgramNumber(u8 instrumentByte, u8* logicalInstrIndex = nullptr) const;
  u32 registerIntelliTAInstrumentOverride(u8 logicalInstrIndex,
                                               const std::array<u8, 6>& regionData);
  u8 ensureIntelliTADrumKitProgram();
  bool usesIntelliCustomPercTable() const;
  void setIntelliCustomPercTableEnabled(bool enabled);

  NinSnesSignatureId signature;
  NinSnesProfileId profileId;
  u8 STATUS_END;
  u8 STATUS_NOTE_MIN;
  u8 STATUS_NOTE_MAX;
  u8 STATUS_PERCUSSION_NOTE_MIN;
  u8 STATUS_PERCUSSION_NOTE_MAX;
  std::map<u8, NinSnesSeqEventType> EventMap;

  std::vector<u8> volumeTable;
  std::vector<u8> durRateTable;
  std::vector<u8> panTable;

  u8 spcPercussionBase;
  u8 sectionRepeatCount;
  s8 globalTranspose;
  u8 tempo;
  SeqFixedPointAutomation<> tempoFade;
  double maxVibratoDepthCents;
  double maxVibratoRateHz;
  u32 dwStartOffset;
  u32 curOffset;
  std::vector<NinSnesSection *> aSections;

  // Konami:
  u16 konamiBaseAddress;

  // Intelligent Systems:
  std::vector<u8> intelliDurVolTable;
  bool intelliUseCustomNoteParam;
  bool intelliUseCustomPercTable;
  NinSnesIntelliVoiceParamState intelliVoiceParam;
  NinSnesIntelliPercussionState intelliPerc;
  std::array<u32, 0x80> intelliInstrumentProgramMap;

  // Quintet
  u8 quintetBGMInstrBase;
  u16 quintetAddrBGMInstrLookup;

  // Falcom:
  u16 falcomBaseOffset;

  void addPercussionInstrNoteMapping(u8 instrIndex, u8 noteIndex,
                                     s8 transposeSemitones) {
    m_percussionInstrNoteMap[instrIndex] = {noteIndex, transposeSemitones};
  }
  const std::map<u8, NinSnesPercussionDef>& percussionInstrNoteMap() const {
    return m_percussionInstrNoteMap;
  }
  const std::vector<NinSnesIntelliTAInstrumentOverride>& intelliTAInstrumentOverrides() const {
    return m_intelliTAInstrumentOverrides;
  }
  const std::vector<NinSnesIntelliTADrumKitDef>& intelliTADrumKitDefs() const {
    return m_intelliTADrumKitDefs;
  }

protected:
  bool loadTracks(ReadMode readMode, u32 stopTime = 1000000) override;
  bool postLoad() override;

  VGMHeader* header;

private:
  friend class NinSnesTrack;
  void loadEventMap();
  void createTracks();
  bool loadSection(NinSnesSection *section, u32 stopTime = 1000000);
  void addSection(NinSnesSection *section);
  NinSnesSection *getSectionAtOffset(u32 offset);
  bool addLoopForeverNoItem();
  void setImmediateTempo(u8 newTempo);
  void startTempoFade(u8 fadeLength, u8 targetTempo);
  void syncTempoDependentTracks();
  NinSnesIntelliTADrumKitDef buildIntelliTADrumKitDef() const;

  u8 spcPercussionBaseInit;
  int m_sectionForeverLoops = 0;
  std::map<u8, NinSnesPercussionDef> m_percussionInstrNoteMap;
  u32 m_nextIntelliTAOverrideProgram;
  std::vector<NinSnesIntelliTAInstrumentOverride> m_intelliTAInstrumentOverrides;
  std::vector<NinSnesIntelliTADrumKitDef> m_intelliTADrumKitDefs;
};

class NinSnesSection : public VGMItem {
public:
  struct TrackSegment {
    // The pointer target remains the parser entry point. Jumps can expand the
    // discovered byte range before that address, so the UI/stop range is separate.
    u32 startOffset = 0;
    u32 rangeOffset = 0;
    u32 rangeLength = 0;
    bool active = false;
    bool uiEventsLoaded = false;

    [[nodiscard]] u32 stopOffset() const {
      return rangeOffset + rangeLength;
    }

    void include(u32 eventOffset, u32 eventLength) {
      if (rangeLength == 0) {
        rangeOffset = eventOffset;
        rangeLength = eventLength;
        return;
      }

      const u32 start = std::min(rangeOffset, eventOffset);
      const u32 end = std::max(stopOffset(), eventOffset + eventLength);
      rangeOffset = start;
      rangeLength = end - start;
    }
  };

  NinSnesSection(NinSnesSeq* parentFile, u32 offset = 0, u32 length = 0);

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

  u16 convertToApuAddress(u16 offset);
  u16 getShortAddress(u32 offset);

  NinSnesSeq* parentSeq;

 private:
  bool m_loaded = false;
  bool m_tracksAddedToChildren = false;
  std::array<TrackSegment, kNinSnesTrackCount> m_trackSegments {};
  std::array<NinSnesSectionTrackItem*, kNinSnesTrackCount> m_tracks {};
};

class NinSnesSectionTrackItem : public SeqTrack {
public:
  NinSnesSectionTrackItem(NinSnesSeq* parentSeq, u32 offset, u32 length,
                          const std::string& name);
};

class NinSnesTrack : public SeqTrack {
public:
  struct PitchSlideEvent {
    u32 offset = 0;
    u32 eventLength = 0;
    u8 delay = 0;
    u8 length = 0;
    u8 targetNote = 0;
  };

  NinSnesTrack(NinSnesSeq* parentSeq, u32 offset = 0, u32 length = 0,
               const std::string& name = "NinSnes Track");

  void resetVars() override;
  void onTickBegin() override;
  bool readEvent() override;

  u16 convertToApuAddress(u16 offset);
  u16 getShortAddress(u32 offset);
  void getVolumeBalance(u16 pan, double& volumeLeft, double& volumeRight);
  u8 readPanTable(u16 pan);
  s8 calculatePanValue(u8 pan, double& volumeScale, bool& reverseLeft, bool& reverseRight);

protected:
  bool onEvent(u32 offset, u32 length) override;

private:
  friend class NinSnesSeq;
  bool prepareSectionTrack(NinSnesSection& section, u32 trackIndex);
  void resetTransientSectionState(u32 trackIndex);
  void resetTransientPlaybackState();
  void resetPersistentRange();
  void includePersistentRange(u32 eventOffset, u32 eventLength);
  NinSnesSeq& seq() const;
  NinSnesIntelliModeId intelliMode() const;
  bool handleIntelliPercussionNote(u32 beginOffset, u8 slot, u8 duration);
  void readStandardNoteParam(u32 beginOffset, u8 statusByte, std::string& desc);
  void readLemmingsNoteParam(u32 beginOffset, u8 statusByte, std::string& desc);
  void readIntelliNoteParam(u32 beginOffset, u8 statusByte, std::string& desc);
  bool handleCoreEvent(NinSnesSeqEventType eventType, u32 beginOffset, u8 statusByte,
                       std::string& desc, bool& continueReading);
  bool handleControllerEvent(NinSnesSeqEventType eventType, u32 beginOffset,
                             std::string& desc);
  bool handleVariantEvent(NinSnesSeqEventType eventType, u32 beginOffset, u8 statusByte,
                          std::string& desc);
  bool handleIntelliEvent(NinSnesSeqEventType eventType, u32 beginOffset, u8 statusByte,
                          std::string& desc);
  PitchSlideEvent readPitchSlide(u32 offset);
  bool consumeQueuedPitchSlide();
  void addPitchSlideEvent(const PitchSlideEvent& slide);
  void beginPitchSlide(const PitchSlideEvent& slide);
  void activatePitchMotion(u8 delay, u8 length, s32 targetPitch);
  void updatePitchSlide();
  void beginNotePitch(u8 note);
  void activateStoredPitchEnvelope();
  void beginNoteVibrato();
  void updateVibratoFade();
  void applyConfiguredVibrato();
  void clearVibratoRateAndDelay();
  void setConfiguredVibratoDepth(u8 depth);
  void resetPitchBendForNewNote();
  void addPendingEndEvent(u8 statusByte, const std::string& desc);
  void syncVibratoRateAndDelay();

  u8 getEffectiveNoteDuration() const;
  void rememberMelodicProgram(u32 progNum,
                              std::optional<u8> logicalProgram = std::nullopt);
  void restoreNonPercussionProgramIfNeeded();
  void switchToPercussionProgramIfNeeded(u8 program = 0);
  void applyIntelliPercussionSlotState(u8 instrumentByte,
                                       std::optional<u8> panByte = std::nullopt,
                                       std::optional<u8> reverbLevel = std::nullopt);
  void addProgramChangeEvent(u32 offset, u32 length, u32 progNum, bool requireBank,
                             const std::string& eventName = "Program Change",
                             std::optional<u8> logicalProgram = std::nullopt);

  bool m_lastNoteWasPercussion = false;
  u32 nonPercussionProgram = 0;
  u8 currentPercussionProgram = 0;
  std::optional<u8> currentLogicalProgram;
  bool intelliLegato = false;
  NinSnesTrackState state;
  NinSnesSection::TrackSegment* currentSegment = nullptr;
  bool m_hasPersistentRange = false;
  u32 m_persistentRangeStart = 0;
  u32 m_persistentRangeEnd = 0;
};
