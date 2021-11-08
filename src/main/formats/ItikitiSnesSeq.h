#pragma once
#include <unordered_map>
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "ItikitiSnesFormat.h"

constexpr uint16_t kItikitiSnesSeqTimebase = 48;
constexpr uint8_t kItikitiSnesSeqTimerFreq = 0x27;

constexpr uint8_t kItikitiSnesSeqMinNoteByte = 0x30;
constexpr uint8_t kItikitiSnesSeqMaxLoopLevel = 4;

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
  ItikitiSnesSeq(RawFile *file, uint32_t offset, std::wstring new_name = L"Square ITIKITI SNES Seq");

  bool GetHeaderInfo() override;
  bool GetTrackPointers() override;
  void ResetVars() override;

  [[nodiscard]] uint16_t base_offset() const { return m_base_offset; }
  [[nodiscard]] uint16_t decode_offset(uint16_t offset) const { return offset + m_base_offset; }
  [[nodiscard]] uint16_t ReadDecodedOffset(uint32_t offset) {
    return decode_offset(GetShort(offset));
  }

  [[nodiscard]] static double GetTempoInBpm(uint8_t tempo) {
    constexpr auto ppqn = kItikitiSnesSeqTimebase;
    constexpr auto timer_freq = kItikitiSnesSeqTimerFreq;
    if (tempo == 0) {
      // since tempo 0 cannot be expressed, this function returns a very small value.
      return 1.0;
    }
    return 60000000.0 / (ppqn * (125 * timer_freq)) * (tempo / 256.0);
  }

  [[nodiscard]] ItikitiSnesSeqEventType GetEventType(uint8_t command) const {
    const auto event_type_iterator = m_event_map.find(command);
    return event_type_iterator != m_event_map.end() ? event_type_iterator->second
                                                    : ItikitiSnesSeqEventType::EVENT_UNDEFINED;
  }

 private:
  static void LoadEventMap(std::unordered_map<uint8_t, ItikitiSnesSeqEventType> &event_map);
  std::unordered_map<uint8_t, ItikitiSnesSeqEventType> m_event_map{};
  uint16_t m_base_offset{};
};

class ItikitiSnesTrack : public SeqTrack {
 public:
  ItikitiSnesTrack(ItikitiSnesSeq *seq, long offset = 0, long length = 0);
  void ResetVars() override;
  bool ReadEvent() override;

  [[nodiscard]] ItikitiSnesSeq *seq() const {
    return reinterpret_cast<ItikitiSnesSeq *>(parentSeq);
  }

  [[nodiscard]] uint16_t decode_offset(uint16_t offset) const { return seq()->decode_offset(offset); }
  [[nodiscard]] uint16_t ReadDecodedOffset(uint32_t offset) const {
    return seq()->ReadDecodedOffset(offset);
  }

 private:
  uint8_t m_note_number_base{};
  std::array<uint8_t, 7> m_note_length_table{};

  int8_t m_loop_level{};
  std::array<uint8_t, kItikitiSnesSeqMaxLoopLevel> m_loop_counts{};
  std::array<uint16_t, kItikitiSnesSeqMaxLoopLevel> m_loop_start_addresses{};
  uint8_t m_alt_loop_count{};
};
