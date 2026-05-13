#pragma once
#include <array>
#include <optional>
#include "VGMMultiSectionSeq.h"
#include "SeqTrack.h"
#include "NinSnesFormat.h"
#include "NinSnesScanResult.h"
#include "NinSnesSeqState.h"

struct NinSnesProfile;

class NinSnesSeq : public VGMMultiSectionSeq {
public:
  NinSnesSeq(RawFile* file, NinSnesProfileId profile, uint32_t offset, uint8_t percussion_base = 0,
             const std::vector<uint8_t>& theVolumeTable = std::vector<uint8_t>(),
             const std::vector<uint8_t>& theDurRateTable = std::vector<uint8_t>(),
             std::string theName = "NinSnes Seq");
  NinSnesSeq(RawFile* file, const NinSnesScanResult& scanResult);
  virtual ~NinSnesSeq();

  virtual bool parseHeader();
  virtual void resetVars();
  virtual bool readEvent(long stopTime);

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

  NinSnesTrackSharedData sharedTrackData[8];

  std::vector<uint8_t> volumeTable;
  std::vector<uint8_t> durRateTable;
  std::vector<uint8_t> panTable;

  uint8_t spcPercussionBase;
  uint8_t sectionRepeatCount;
  int8_t globalTranspose;

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
                                     int8_t globalTranspose) {
    m_percussionInstrNoteMap[instrIndex] = {noteIndex, globalTranspose};
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
  VGMHeader* header;

private:
  void loadEventMap();
  NinSnesIntelliTADrumKitDef buildIntelliTADrumKitDef() const;

  uint8_t spcPercussionBaseInit;
  std::map<uint8_t, NinSnesPercussionDef> m_percussionInstrNoteMap;
  uint32_t m_nextIntelliTAOverrideProgram;
  std::vector<NinSnesIntelliTAInstrumentOverride> m_intelliTAInstrumentOverrides;
  std::vector<NinSnesIntelliTADrumKitDef> m_intelliTADrumKitDefs;
};

class NinSnesSection : public VGMSeqSection {
public:
  NinSnesSection(NinSnesSeq* parentFile, uint32_t offset = 0, uint32_t length = 0);

  virtual bool parseTrackPointers();

  uint16_t convertToApuAddress(uint16_t offset);
  uint16_t getShortAddress(uint32_t offset);
};

class NinSnesTrack : public SeqTrack {
public:
  NinSnesTrack(NinSnesSection* parentSection, uint32_t offset = 0, uint32_t length = 0,
               const std::string& theName = "NinSnes Track");

  virtual void resetVars();
  virtual bool readEvent();

  uint16_t convertToApuAddress(uint16_t offset);
  uint16_t getShortAddress(uint32_t offset);
  void getVolumeBalance(uint16_t pan, double& volumeLeft, double& volumeRight);
  uint8_t readPanTable(uint16_t pan);
  int8_t calculatePanValue(uint8_t pan, double& volumeScale, bool& reverseLeft, bool& reverseRight);

  NinSnesSection* parentSection;
  NinSnesTrackSharedData* shared = nullptr;
  bool available = true;

private:
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
  void addPendingEndEvent(uint8_t statusByte, const std::string& desc);

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
};
