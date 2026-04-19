#pragma once
#include <array>
#include <optional>
#include "VGMMultiSectionSeq.h"
#include "SeqTrack.h"
#include "NinSnesFormat.h"
#include "NinSnesScanner.h"

enum NinSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_NOP,
  EVENT_NOP1,
  EVENT_END,
  EVENT_NOTE_PARAM,
  EVENT_NOTE,
  EVENT_TIE,
  EVENT_REST,
  EVENT_PERCUSSION_NOTE,
  EVENT_PROGCHANGE,
  EVENT_PAN,
  EVENT_PAN_FADE,
  EVENT_VIBRATO_ON,
  EVENT_VIBRATO_OFF,
  EVENT_MASTER_VOLUME,
  EVENT_MASTER_VOLUME_FADE,
  EVENT_TEMPO,
  EVENT_TEMPO_FADE,
  EVENT_GLOBAL_TRANSPOSE,
  EVENT_TRANSPOSE,
  EVENT_TREMOLO_ON,
  EVENT_TREMOLO_OFF,
  EVENT_VOLUME,
  EVENT_VOLUME_FADE,
  EVENT_CALL,
  EVENT_VIBRATO_FADE,
  EVENT_PITCH_ENVELOPE_TO,
  EVENT_PITCH_ENVELOPE_FROM,
  EVENT_PITCH_ENVELOPE_OFF,
  EVENT_TUNING,
  EVENT_ECHO_ON,
  EVENT_ECHO_OFF,
  EVENT_ECHO_PARAM,
  EVENT_ECHO_VOLUME_FADE,
  EVENT_PITCH_SLIDE,
  EVENT_PERCUSSION_PATCH_BASE,

  // Nintendo RD2:
      EVENT_RD2_PROGCHANGE_AND_ADSR,

  // Konami:
      EVENT_KONAMI_LOOP_START,
  EVENT_KONAMI_LOOP_END,
  EVENT_KONAMI_ADSR_AND_GAIN,

  // Lemmings:
      EVENT_LEMMINGS_NOTE_PARAM,

  // Intelligent Systems:
  // Fire Emblem 3 & 4:
      EVENT_INTELLI_NOTE_PARAM,
  EVENT_INTELLI_ECHO_ON,
  EVENT_INTELLI_ECHO_OFF,
  EVENT_INTELLI_LEGATO_ON,
  EVENT_INTELLI_LEGATO_OFF,
  EVENT_INTELLI_JUMP_SHORT_CONDITIONAL,
  EVENT_INTELLI_JUMP_SHORT,
  EVENT_INTELLI_FE3_EVENT_F5,
  EVENT_INTELLI_WRITE_APU_PORT,
  EVENT_INTELLI_FE3_EVENT_F9,
  EVENT_INTELLI_DEFINE_VOICE_PARAM,
  EVENT_INTELLI_LOAD_VOICE_PARAM,
  EVENT_INTELLI_ADSR,
  EVENT_INTELLI_GAIN_SUSTAIN_TIME_AND_RATE,
  EVENT_INTELLI_GAIN_SUSTAIN_TIME,
  EVENT_INTELLI_GAIN,
  EVENT_INTELLI_FE4_EVENT_FC,
  EVENT_INTELLI_TA_SUBEVENT,
  EVENT_INTELLI_FE4_SUBEVENT,

  // Quintet:
  EVENT_QUINTET_TUNING,
  EVENT_QUINTET_ADSR,
};

class NinSnesTrackSharedData {
 public:
  NinSnesTrackSharedData();

  virtual void resetVars();

  uint8_t spcNoteDuration;
  uint8_t spcNoteDurRate;
  uint8_t spcNoteVolume;
  int8_t spcTranspose;
  uint16_t loopReturnAddress;
  uint16_t loopStartAddress;
  uint8_t loopCount;

  // Konami:
  uint16_t konamiLoopStart;
  uint8_t konamiLoopCount;
};

struct NinSnesPercussionDef {
  uint8_t noteIndex;
  int8_t globalTranspose;
};

constexpr size_t NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT = 16;

struct NinSnesIntelliTACustomPercEntry {
  uint8_t patchByte = 0;
  uint8_t noteByte = 0;
  uint8_t panByte = 0;
};

struct NinSnesIntelliTAInstrumentOverride {
  uint8_t logicalInstrIndex = 0;
  uint32_t progNum = 0;
  std::array<uint8_t, 6> regionData {};
};

struct NinSnesIntelliTADrumKitSlot {
  bool active = false;
  uint32_t sourceProgNum = 0;
  uint8_t playedNoteByte = 0xa4;

  bool operator==(const NinSnesIntelliTADrumKitSlot& other) const = default;
};

struct NinSnesIntelliTADrumKitDef {
  uint8_t program = 0;
  std::array<NinSnesIntelliTADrumKitSlot, NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT> slots {};
};

class NinSnesSeq:
    public VGMMultiSectionSeq {
 public:
  NinSnesSeq(RawFile *file,
             NinSnesVersion ver,
             uint32_t offset,
             uint8_t percussion_base = 0,
             const std::vector<uint8_t> &theVolumeTable = std::vector<uint8_t>(),
             const std::vector<uint8_t> &theDurRateTable = std::vector<uint8_t>(),
             std::string theName = "NinSnes Seq");
  virtual ~NinSnesSeq();

  virtual bool parseHeader();
  virtual void resetVars();
  virtual bool readEvent(long stopTime);

  double getTempoInBPM();
  double getTempoInBPM(uint8_t tempo);

  uint16_t convertToAPUAddress(uint16_t offset);
  uint16_t getShortAddress(uint32_t offset);
  uint32_t resolveProgramNumber(uint8_t instrumentByte,
                                uint8_t* logicalInstrIndex = nullptr) const;
  uint32_t registerIntelliTAInstrumentOverride(uint8_t logicalInstrIndex,
                                               const std::array<uint8_t, 6>& regionData);
  uint8_t ensureIntelliTADrumKitProgram();

  NinSnesVersion version;
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
  uint16_t intelliVoiceParamTable;
  uint8_t intelliVoiceParamTableSize;
  std::array<NinSnesIntelliTACustomPercEntry, NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT> intelliCustomPercTable;
  std::array<uint32_t, 0x80> intelliInstrumentProgramMap;

  // Quintet
  uint8_t quintetBGMInstrBase;
  uint16_t quintetAddrBGMInstrLookup;

  // Falcom:
  uint16_t falcomBaseOffset;

  void addPercussionInstrNoteMapping(uint8_t instrIndex, uint8_t noteIndex, int8_t globalTranspose) {
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
  VGMHeader *header;

 private:
  void loadEventMap();
  void loadStandardVcmdMap(uint8_t statusByte);
  NinSnesIntelliTADrumKitDef buildIntelliTADrumKitDef() const;

  uint8_t spcPercussionBaseInit;
  std::map<uint8_t, NinSnesPercussionDef> m_percussionInstrNoteMap;
  uint32_t m_nextIntelliTAOverrideProgram;
  std::vector<NinSnesIntelliTAInstrumentOverride> m_intelliTAInstrumentOverrides;
  std::vector<NinSnesIntelliTADrumKitDef> m_intelliTADrumKitDefs;
};

class NinSnesSection
    : public VGMSeqSection {
 public:
  NinSnesSection(NinSnesSeq *parentFile, uint32_t offset = 0, uint32_t length = 0);

  virtual bool parseTrackPointers();

  uint16_t convertToApuAddress(uint16_t offset);
  uint16_t getShortAddress(uint32_t offset);
};

class NinSnesTrack
    : public SeqTrack {
 public:
  NinSnesTrack
      (NinSnesSection *parentSection, uint32_t offset = 0, uint32_t length = 0, const std::string &theName = "NinSnes Track");

  virtual void resetVars();
  virtual bool readEvent();

  uint16_t convertToApuAddress(uint16_t offset);
  uint16_t getShortAddress(uint32_t offset);
  void getVolumeBalance(uint16_t pan, double &volumeLeft, double &volumeRight);
  uint8_t readPanTable(uint16_t pan);
  int8_t calculatePanValue(uint8_t pan, double &volumeScale, bool &reverseLeft, bool &reverseRight);

  NinSnesSection *parentSection;
  NinSnesTrackSharedData *shared = nullptr;
  bool available = true;

 private:
  uint8_t getEffectiveNoteDuration() const;
  void restoreNonPercussionProgramIfNeeded();
  void switchToPercussionProgramIfNeeded(uint8_t program = 0);
  void addPercussionPanNoItem(uint8_t midiPan, uint8_t expressionLevel);
  void addPercussionReverbNoItem(uint8_t reverbLevel);
  void addProgramChangeEvent(uint32_t offset,
                             uint32_t length,
                             uint32_t progNum,
                             bool requireBank,
                             const std::string &eventName = "Program Change",
                             std::optional<uint8_t> logicalProgram = std::nullopt);

  bool m_lastNoteWasPercussion = false;
  uint32_t nonPercussionProgram = 0;
  uint8_t currentPercussionProgram = 0;
  std::optional<uint8_t> currentLogicalProgram;
  bool intelliLegato = false;
};
