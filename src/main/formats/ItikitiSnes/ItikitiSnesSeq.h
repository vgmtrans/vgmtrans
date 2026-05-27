/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"
#include <unordered_map>
#include <array>
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "ItikitiSnesFormat.h"

constexpr u16 kItikitiSnesSeqTimebase = 48;
constexpr u8 kItikitiSnesSeqTimerFreq = 0x27;

constexpr u8 kItikitiSnesSeqMinNoteByte = 0x30;
constexpr u8 kItikitiSnesSeqMaxLoopLevel = 4;

enum class ItikitiSnesSeqEventType {
  EVENT_UNDEFINED = 0,
  EVENT_UNKNOWN0,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_NOTE,
  EVENT_END,
  EVENT_MASTER_VOLUME,
  EVENT_ECHO_VOLUME,
  EVENT_CHANNEL_VOLUME,
  EVENT_ECHO_FEEDBACK_FIR,
  EVENT_TEMPO,
  EVENT_TEMPO_FADE,
  EVENT_NOISE_FREQ,
  EVENT_SELECT_NOTE_LENGTH_PATTERN,
  EVENT_CUSTOM_NOTE_LENGTH_PATTERN,
  EVENT_NOTE_NUMBER_BASE,
  EVENT_VOLUME,
  EVENT_VOLUME_FADE,
  EVENT_PAN,
  EVENT_PAN_FADE,
  EVENT_PROGRAM_CHANGE,
  EVENT_TUNING,
  EVENT_ADSR_AR,
  EVENT_ADSR_DR,
  EVENT_ADSR_SL,
  EVENT_ADSR_SR,
  EVENT_ADSR_DEFAULT,
  EVENT_TRANSPOSE_ABS,
  EVENT_TRANSPOSE_REL,
  EVENT_VIBRATO_ON,
  EVENT_VIBRATO_OFF,
  EVENT_TREMOLO_ON,
  EVENT_TREMOLO_OFF,
  EVENT_PAN_LFO_ON,
  EVENT_PAN_LFO_OFF,
  EVENT_NOISE_ON,
  EVENT_NOISE_OFF,
  EVENT_PITCH_MOD_ON,
  EVENT_PITCH_MOD_OFF,
  EVENT_ECHO_ON,
  EVENT_ECHO_OFF,
  EVENT_PORTAMENTO_ON,
  EVENT_PORTAMENTO_OFF,
  EVENT_SPECIAL,
  EVENT_NOTE_RANDOMIZATION_OFF,
  EVENT_PITCH_SLIDE,
  EVENT_LOOP_START,
  EVENT_GOTO,
  EVENT_LOOP_END,
  EVENT_LOOP_BREAK,
};

class ItikitiSnesSeq : public VGMSeq {
 public:
  ItikitiSnesSeq(RawFile *file, u32 offset, std::string new_name = "Square ITIKITI SNES Seq");

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  [[nodiscard]] u16 base_offset() const { return m_base_offset; }
  [[nodiscard]] u16 decode_offset(u16 offset) const { return offset + m_base_offset; }
  [[nodiscard]] u16 readDecodedOffset(u32 offset) {
    return decode_offset(readShort(offset));
  }

  [[nodiscard]] static double getTempoInBpm(u8 tempo) {
    constexpr auto ppqn = kItikitiSnesSeqTimebase;
    constexpr auto timer_freq = kItikitiSnesSeqTimerFreq;
    if (tempo == 0) {
      // since tempo 0 cannot be expressed, this function returns a very small value.
      return 1.0;
    }
    return 60000000.0 / (ppqn * (125 * timer_freq)) * (tempo / 256.0);
  }

  [[nodiscard]] ItikitiSnesSeqEventType getEventType(u8 command) const {
    const auto event_type_iterator = m_event_map.find(command);
    return event_type_iterator != m_event_map.end() ? event_type_iterator->second
                                                    : ItikitiSnesSeqEventType::EVENT_UNDEFINED;
  }

 private:
  static void loadEventMap(std::unordered_map<u8, ItikitiSnesSeqEventType> &event_map);
  std::unordered_map<u8, ItikitiSnesSeqEventType> m_event_map{};
  u16 m_base_offset{};
};

class ItikitiSnesTrack : public SeqTrack {
 public:
  ItikitiSnesTrack(ItikitiSnesSeq *seq, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  bool readEvent() override;

  [[nodiscard]] ItikitiSnesSeq *seq() const {
    return reinterpret_cast<ItikitiSnesSeq *>(parentSeq);
  }

  [[nodiscard]] u16 decode_offset(u16 offset) const { return seq()->decode_offset(offset); }
  [[nodiscard]] u16 readDecodedOffset(u32 offset) const {
    return seq()->readDecodedOffset(offset);
  }

 private:
  u8 m_note_number_base{};
  std::array<u8, 7> m_note_length_table{};

  s8 m_loop_level{};
  std::array<u8, kItikitiSnesSeqMaxLoopLevel> m_loop_counts{};
  std::array<u16, kItikitiSnesSeqMaxLoopLevel> m_loop_start_addresses{};
  u8 m_alt_loop_count{};
};
