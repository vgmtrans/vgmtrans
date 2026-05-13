#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum NinSnesSeqEventType {
  // start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get
  // confused with a legit event
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

struct NinSnesIntelliVoiceParamState {
  uint16_t addr = 0;
  uint8_t size = 0;
  bool defined = false;

  void clear() {
    addr = 0;
    size = 0;
    defined = false;
  }
};

struct NinSnesIntelliPercussionState {
  std::array<NinSnesIntelliTACustomPercEntry, NINSNES_INTELLI_TA_PERCUSSION_SLOT_COUNT> table {};
  uint8_t flags = 0;
  uint8_t unknownByte = 0;

  void clear() {
    table = {};
    flags = 0;
    unknownByte = 0;
  }
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
