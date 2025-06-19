/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "ItikitiSnesSeq.h"
#include "ScaleConversion.h"
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(ItikitiSnes);

static constexpr uint8_t NOTE_VELOCITY = 100;
static constexpr std::array<uint8_t, 16> kMasterNoteLengths = {
    0xc0, 0x90, 0x60, 0x48, 0x40, 0x30, 0x24, 0x20, 0x18, 0x12, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03};
static constexpr std::array<uint8_t, 7> kDefaultNoteLengths = {0xc0, 0x60, 0x48, 0x30,
                                                               0x24, 0x18, 0x0c};

ItikitiSnesSeq::ItikitiSnesSeq(RawFile *file, uint32_t offset, std::string new_name)
    : VGMSeq(ItikitiSnesFormat::name, file, offset, 0, std::move(new_name)) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap(m_event_map);
}

void ItikitiSnesSeq::resetVars() {
  VGMSeq::resetVars();
}

bool ItikitiSnesSeq::parseHeader() {
  setPPQN(kItikitiSnesSeqTimebase);

  VGMHeader *header = addHeader(dwOffset, 0);
  header->addUnknownChild(dwOffset, 1);
  header->addChild(dwOffset + 1, 1, "Number of Tracks");

  nNumTracks = readByte(dwOffset + 1);
  if (nNumTracks == 0 || nNumTracks > 8)
    return false;

  const uint16_t first_offset = readShort(dwOffset + 2);
  const auto first_address = dwOffset + 2 + (2 * nNumTracks);
  m_base_offset = static_cast<uint16_t>(first_address) - first_offset;

  for (unsigned int track_index = 0; track_index < nNumTracks; track_index++) {
    std::string track_name = fmt::format("Track {}", track_index + 1);

    const uint32_t offset_to_pointer = dwOffset + 2 + (2 * track_index);
    header->addChild(offset_to_pointer, 2, track_name);
  }

  header->setGuessedLength();
  return true;
}


bool ItikitiSnesSeq::parseTrackPointers() {
  for (unsigned int track_index = 0; track_index < nNumTracks; track_index++) {
    const uint32_t offset_to_pointer = dwOffset + 2 + (2 * track_index);
    const uint32_t offset = readDecodedOffset(offset_to_pointer);
    auto track = std::make_unique<ItikitiSnesTrack>(this, offset);
    aTracks.push_back(track.release());
  }
  return true;
}

void ItikitiSnesSeq::loadEventMap(std::unordered_map<uint8_t, ItikitiSnesSeqEventType> &event_map) {
  event_map.clear();

  for (unsigned int command = kItikitiSnesSeqMinNoteByte; command <= 0xff; command++) {
    event_map[static_cast<uint8_t>(command)] = ItikitiSnesSeqEventType::EVENT_NOTE;
  }

  event_map[0x00] = ItikitiSnesSeqEventType::EVENT_END;
  event_map[0x01] = ItikitiSnesSeqEventType::EVENT_MASTER_VOLUME;
  event_map[0x02] = ItikitiSnesSeqEventType::EVENT_ECHO_VOLUME;
  event_map[0x03] = ItikitiSnesSeqEventType::EVENT_CHANNEL_VOLUME;
  event_map[0x04] = ItikitiSnesSeqEventType::EVENT_ECHO_FEEDBACK_FIR;
  event_map[0x05] = ItikitiSnesSeqEventType::EVENT_UNKNOWN0;
  event_map[0x06] = ItikitiSnesSeqEventType::EVENT_TEMPO;
  event_map[0x07] = ItikitiSnesSeqEventType::EVENT_TEMPO_FADE;
  event_map[0x08] = ItikitiSnesSeqEventType::EVENT_NOISE_FREQ;
  event_map[0x09] = ItikitiSnesSeqEventType::EVENT_SELECT_NOTE_LENGTH_PATTERN;
  event_map[0x0a] = ItikitiSnesSeqEventType::EVENT_CUSTOM_NOTE_LENGTH_PATTERN;
  event_map[0x0b] = ItikitiSnesSeqEventType::EVENT_NOTE_NUMBER_BASE;
  event_map[0x0c] = ItikitiSnesSeqEventType::EVENT_VOLUME;
  event_map[0x0d] = ItikitiSnesSeqEventType::EVENT_VOLUME_FADE;
  event_map[0x0e] = ItikitiSnesSeqEventType::EVENT_PAN;
  event_map[0x0f] = ItikitiSnesSeqEventType::EVENT_PAN_FADE;
  event_map[0x10] = ItikitiSnesSeqEventType::EVENT_PROGRAM_CHANGE;
  event_map[0x11] = ItikitiSnesSeqEventType::EVENT_TUNING;
  event_map[0x12] = ItikitiSnesSeqEventType::EVENT_ADSR_AR;
  event_map[0x13] = ItikitiSnesSeqEventType::EVENT_ADSR_DR;
  event_map[0x14] = ItikitiSnesSeqEventType::EVENT_ADSR_SL;
  event_map[0x15] = ItikitiSnesSeqEventType::EVENT_ADSR_SR;
  event_map[0x16] = ItikitiSnesSeqEventType::EVENT_ADSR_DEFAULT;
  event_map[0x17] = ItikitiSnesSeqEventType::EVENT_TRANSPOSE_ABS;
  event_map[0x18] = ItikitiSnesSeqEventType::EVENT_TRANSPOSE_REL;
  event_map[0x19] = ItikitiSnesSeqEventType::EVENT_VIBRATO_ON;
  event_map[0x1a] = ItikitiSnesSeqEventType::EVENT_VIBRATO_OFF;
  event_map[0x1b] = ItikitiSnesSeqEventType::EVENT_TREMOLO_ON;
  event_map[0x1c] = ItikitiSnesSeqEventType::EVENT_TREMOLO_OFF;
  event_map[0x1d] = ItikitiSnesSeqEventType::EVENT_PAN_LFO_ON;
  event_map[0x1e] = ItikitiSnesSeqEventType::EVENT_PAN_LFO_OFF;
  event_map[0x1f] = ItikitiSnesSeqEventType::EVENT_NOISE_ON;
  event_map[0x20] = ItikitiSnesSeqEventType::EVENT_NOISE_OFF;
  event_map[0x21] = ItikitiSnesSeqEventType::EVENT_PITCH_MOD_ON;
  event_map[0x22] = ItikitiSnesSeqEventType::EVENT_PITCH_MOD_OFF;
  event_map[0x23] = ItikitiSnesSeqEventType::EVENT_ECHO_ON;
  event_map[0x24] = ItikitiSnesSeqEventType::EVENT_ECHO_OFF;
  event_map[0x25] = ItikitiSnesSeqEventType::EVENT_PORTAMENTO_ON;
  event_map[0x26] = ItikitiSnesSeqEventType::EVENT_PORTAMENTO_OFF;
  event_map[0x27] = ItikitiSnesSeqEventType::EVENT_SPECIAL;
  event_map[0x28] = ItikitiSnesSeqEventType::EVENT_NOTE_RANDOMIZATION_OFF;
  event_map[0x29] = ItikitiSnesSeqEventType::EVENT_PITCH_SLIDE;
  event_map[0x2a] = ItikitiSnesSeqEventType::EVENT_LOOP_START;
  event_map[0x2b] = ItikitiSnesSeqEventType::EVENT_UNKNOWN2;
  event_map[0x2c] = ItikitiSnesSeqEventType::EVENT_GOTO;
  event_map[0x2d] = ItikitiSnesSeqEventType::EVENT_UNDEFINED; // conditional jump (length=2)
  event_map[0x2e] = ItikitiSnesSeqEventType::EVENT_LOOP_END;
  event_map[0x2f] = ItikitiSnesSeqEventType::EVENT_LOOP_BREAK;
}

ItikitiSnesTrack::ItikitiSnesTrack(ItikitiSnesSeq *seq, uint32_t offset, uint32_t length)
    : SeqTrack(seq, offset, length) {
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void ItikitiSnesTrack::resetVars() {
  SeqTrack::resetVars();
  m_note_number_base = 0;
  m_note_length_table = kDefaultNoteLengths;
  m_loop_level = -1;
  m_alt_loop_count = 0; // actual driver does not initialize this
}

bool ItikitiSnesTrack::readEvent() {
  auto *seq = this->seq();

  const auto start = curOffset;
  if (curOffset >= 0x10000)
    return false;

  const uint8_t command = readByte(curOffset++);
  const auto event_type = seq->getEventType(command);

  bool stop_parser = false;
  std::string desc;
  switch (event_type) {
    case ItikitiSnesSeqEventType::EVENT_UNKNOWN0:
      desc = logEvent(command, spdlog::level::debug);
      addUnknown(start, curOffset - start, "Unknown Event", desc);
      break;

    case ItikitiSnesSeqEventType::EVENT_UNKNOWN1: {
      const auto arg1 = readByte(curOffset++);
      desc = logEvent(command, spdlog::level::debug, "Event", arg1);
      addUnknown(start, curOffset - start, "Unknown Event", desc);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_UNKNOWN2: {
      const auto arg1 = readByte(curOffset++);
      const auto arg2 = readByte(curOffset++);
      desc = logEvent(command, spdlog::level::debug, "Event", arg1, arg2);
      addUnknown(start, curOffset - start, "Unknown Event", desc);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_UNKNOWN3: {
      const auto arg1 = readByte(curOffset++);
      const auto arg2 = readByte(curOffset++);
      const auto arg3 = readByte(curOffset++);
      desc = logEvent(command, spdlog::level::debug, "Event", arg1, arg2, arg3);
      addUnknown(start, curOffset - start, "Unknown Event", desc);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOTE: {
      const uint8_t len_index = command & 7;

      uint8_t len;
      if (len_index == 7)
        len = readByte(curOffset++);
      else
        len = m_note_length_table[len_index];

      const uint8_t dur = len - 2;

      const bool rest = command >= 0xf8;
      const bool tie = command >= 0xf0 && command <= 0xf7;
      if (rest) {
        addRest(start, curOffset - start, len);
      } else if (tie) {
        makePrevDurNoteEnd(getTime() + dur);
        addGenericEvent(start, curOffset - start, "Tie", desc, Type::Tie);
        addTime(len);
      } else {
        const uint8_t key_index = command >> 3;
        const int8_t key = static_cast<int8_t>(kItikitiSnesSeqNoteKeyBias + key_index - 6 + m_note_number_base);
        addNoteByDur(start, curOffset - start, key, NOTE_VELOCITY, dur);
        addTime(len);
      }
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_END: {
      addEndOfTrack(start, curOffset - start);
      stop_parser = true;
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_MASTER_VOLUME: {
      const uint8_t vol = readByte(curOffset++);
      desc = fmt::format("Volume: {:d}", vol);
      addGenericEvent(start, curOffset - start, "Master Volume", desc, Type::MasterVolume);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ECHO_VOLUME: {
      const uint8_t vol = readByte(curOffset++);
      desc = fmt::format("Volume: {:d}", vol);
      addGenericEvent(start, curOffset - start, "Echo Volume", desc, Type::Reverb);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_CHANNEL_VOLUME: {
      const uint8_t vol = readByte(curOffset++);
      addVol(start, curOffset - start, vol >> 1);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ECHO_FEEDBACK_FIR: {
      const int8_t feedback = static_cast<int8_t>(readByte(curOffset++));
      const uint8_t filter_index = readByte(curOffset++);
      desc = fmt::format("Feedback: {:d}  FIR: {:d}", feedback, filter_index);
      addGenericEvent(start, curOffset - start, "Echo Feedback & FIR", desc, Type::Reverb);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TEMPO: {
      const uint8_t tempo = readByte(curOffset++);
      addTempoBPM(start, curOffset - start, seq->getTempoInBpm(tempo));
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TEMPO_FADE: {
      const uint8_t fade_length = readByte(curOffset++);
      const uint8_t tempo = readByte(curOffset++);
      addTempoBPMSlide(start, curOffset - start, fade_length, seq->getTempoInBpm(tempo));
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOISE_FREQ: {
      // Driver bug: This command sets the noise frequency for the next track from the current.
      const uint8_t nck = readByte(curOffset++) & 0x1f;
      desc = fmt::format("Noise Frequency (NCK): {:d}", nck);
      addGenericEvent(start, curOffset - start, "Noise Frequency", desc, Type::Noise);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_SELECT_NOTE_LENGTH_PATTERN: {
      const uint16_t bits = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Length Bits: 0x{:04X}", bits);
      addGenericEvent(start, curOffset - start, "Note Length Pattern", desc, Type::DurationNote);

      size_t size_written = 0;
      for (unsigned int index = 0; index < 16 && size_written < m_note_length_table.size();
           index++) {
        if (const uint16_t bit = static_cast<uint16_t>(0x8000) >> index; (bits & bit) != 0) {
          m_note_length_table[size_written] = kMasterNoteLengths[index];
          size_written++;
        }
      }
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_CUSTOM_NOTE_LENGTH_PATTERN: {
      const uint8_t arg1 = readByte(curOffset++);
      const uint8_t arg2 = readByte(curOffset++);
      const uint8_t arg3 = readByte(curOffset++);
      const uint8_t arg4 = readByte(curOffset++);
      const uint8_t arg5 = readByte(curOffset++);
      const uint8_t arg6 = readByte(curOffset++);
      const uint8_t arg7 = readByte(curOffset++);
      desc = fmt::format("Lengths: {:d}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}",
                               arg1, arg2, arg3, arg4, arg5, arg6, arg7);
      addGenericEvent(start, curOffset - start, "Custom Note Length Pattern", desc,
                      Type::DurationNote);

      m_note_length_table[0] = arg1;
      m_note_length_table[1] = arg2;
      m_note_length_table[2] = arg3;
      m_note_length_table[3] = arg4;
      m_note_length_table[4] = arg5;
      m_note_length_table[5] = arg6;
      m_note_length_table[6] = arg7;
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOTE_NUMBER_BASE: {
      const uint8_t note_number = readByte(curOffset++);
      desc = fmt::format("Note Number: {:d}", note_number);
      addGenericEvent(start, curOffset - start, "Note Number Base", desc, Type::ChangeState);
      m_note_number_base = note_number;
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_VOLUME: {
      const uint8_t vol = readByte(curOffset++);
      addExpression(start, curOffset - start, vol >> 1);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_VOLUME_FADE: {
      const uint8_t fade_length = readByte(curOffset++);
      const uint8_t vol = readByte(curOffset++);
      addExpressionSlide(start, curOffset - start, fade_length, vol >> 1);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PAN: {
      const uint8_t pan = readByte(curOffset++);
      const uint8_t midi_pan = convertLinearPercentPanValToStdMidiVal(pan / 255.0);
      addPan(start, curOffset - start, midi_pan);
      // TODO: apply volume scale
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PAN_FADE: {
      const uint8_t fade_length = readByte(curOffset++);
      const uint8_t pan = readByte(curOffset++);
      const uint8_t midi_pan = convertLinearPercentPanValToStdMidiVal(pan / 255.0);
      addPanSlide(start, curOffset - start, fade_length, midi_pan); // TODO: fix pan curve
      // TODO: apply volume scale
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PROGRAM_CHANGE: {
      const uint8_t instrument = readByte(curOffset++);
      addProgramChange(start, curOffset - start, instrument);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TUNING: {
      const int8_t tuning = static_cast<int8_t>(readByte(curOffset++));
      const double semitones = tuning / 128.0; // not very well verified
      addFineTuning(start, curOffset - start, semitones * 100.0);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ADSR_AR: {
      const uint8_t ar = readByte(curOffset++) & 15;
      desc = fmt::format("AR: {:d}", ar);
      addGenericEvent(start, curOffset - start, "ADSR Attack Rate", desc, Type::Adsr);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ADSR_DR: {
      const uint8_t dr = readByte(curOffset++) & 7;
      desc = fmt::format("DR: {:d}", dr);
      addGenericEvent(start, curOffset - start, "ADSR Decay Rate", desc, Type::Adsr);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ADSR_SL: {
      const uint8_t sl = readByte(curOffset++) & 7;
      desc = fmt::format("SL: {:d}", sl);
      addGenericEvent(start, curOffset - start, "ADSR Sustain Level", desc, Type::Adsr);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ADSR_SR: {
      const uint8_t sr = readByte(curOffset++) & 15;
      desc = fmt::format("SR: {:d}", sr);
      addGenericEvent(start, curOffset - start, "ADSR Sustain Rate", desc, Type::Adsr);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ADSR_DEFAULT: {
      addGenericEvent(start, curOffset - start, "Default ADSR", desc, Type::Adsr);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TRANSPOSE_ABS: {
      const int8_t transpose = static_cast<int8_t>(readByte(curOffset++));
      addTranspose(start, curOffset - start, transpose);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TRANSPOSE_REL: {
      const int8_t transpose_amount = static_cast<int8_t>(readByte(curOffset++));
      const int8_t new_transpose = static_cast<int8_t>(std::max(-128, std::min(127, transpose + transpose_amount)));
      addTranspose(start, curOffset - start, new_transpose, "Transpose (Relative)");
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_VIBRATO_ON: {
        const uint8_t delay = readByte(curOffset++);
        const uint8_t rate = readByte(curOffset++);
        const uint8_t depth = readByte(curOffset++);
        desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}", delay, rate, depth);
        addGenericEvent(start, curOffset - start, "Vibrato", desc, Type::Vibrato);
        break;
    }

    case ItikitiSnesSeqEventType::EVENT_VIBRATO_OFF: {
      // The command changes vibrato depth to 0.
      addGenericEvent(start, curOffset - start, "Vibrato Off", desc, Type::Vibrato);
      break;
    }


    case ItikitiSnesSeqEventType::EVENT_TREMOLO_ON: {
      const uint8_t delay = readByte(curOffset++);
      const uint8_t rate = readByte(curOffset++);
      const uint8_t depth = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}", delay, rate, depth);
      addGenericEvent(start, curOffset - start, "Tremolo", desc, Type::Tremelo);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TREMOLO_OFF: {
      addGenericEvent(start, curOffset - start, "Tremolo Off", desc, Type::Tremelo);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PAN_LFO_ON: {
      const uint8_t depth = readByte(curOffset++);
      const uint8_t rate = readByte(curOffset++);
      desc = fmt::format("Depth: {:d}  Rate: {:d}", depth, rate);
      addGenericEvent(start, curOffset - start, "Pan LFO", desc, Type::PanLfo);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PAN_LFO_OFF: {
      addGenericEvent(start, curOffset - start, "Pan LFO Off", desc, Type::PanLfo);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOISE_ON: {
      addGenericEvent(start, curOffset - start, "Noise On", desc, Type::Noise);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOISE_OFF: {
      addGenericEvent(start, curOffset - start, "Noise Off", desc, Type::Noise);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PITCH_MOD_ON: {
      addGenericEvent(start, curOffset - start, "Pitch Modulation On", desc,
                      Type::Modulation);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PITCH_MOD_OFF: {
      addGenericEvent(start, curOffset - start, "Pitch Modulation Off", desc,
                      Type::Modulation);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ECHO_ON: {
      addReverb(start, curOffset - start, 40, "Echo On");
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ECHO_OFF: {
      addReverb(start, curOffset - start, 0, "Echo Off");
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PORTAMENTO_ON: {
      const uint8_t speed = readByte(curOffset++); // in ticks
      addPortamentoTime(start, curOffset - start, speed, "Portamento");
      addPortamentoNoItem(true);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PORTAMENTO_OFF: {
      addPortamentoNoItem(false);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_SPECIAL: {
      const uint8_t value = readByte(curOffset++);
      if (value <= 0x7f) {
        desc = fmt::format("Range: {:d} semitones", value);
        addGenericEvent(start, curOffset - start, "Note Randomization On", desc,
                        Type::ChangeState);
      } else if (value == 0x83) {
        // Detailed behavior has not yet been analyzed
        addGenericEvent(start, curOffset - start, "Change Volume Mode (Global)", desc,
                        Type::Volume);
      } else {
        desc = logEvent(command, spdlog::level::debug, "Event", value);
        addUnknown(start, curOffset - start, "Unknown Event", desc);
      }
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOTE_RANDOMIZATION_OFF: {
      addGenericEvent(start, curOffset - start, "Note Randomization Off", desc,
                      Type::ChangeState);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PITCH_SLIDE: {
      const uint8_t length = readByte(curOffset++); // in ticks
      const int8_t semitones = static_cast<int8_t>(readByte(curOffset++));
      desc = fmt::format("Length: {:d}  Key: {:d} semitones", length, semitones);
      addGenericEvent(start, curOffset - start, "Pitch Slide", desc, Type::PitchBendSlide);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_LOOP_START: {
      const uint8_t count = readByte(curOffset++);
      desc = (count == 0) ? std::string("Repeat Count: Infinite")
                                 : fmt::format("Repeat Count: {:d}", count + 1);
      addGenericEvent(start, curOffset - start, "Loop Start", desc, Type::RepeatStart);

      if (m_loop_level + 1 >= kItikitiSnesSeqMaxLoopLevel) {
        L_WARN("Loop Start: too many nesting loops");
        stop_parser = true;
        break;
      }
      m_loop_level++;
      m_loop_counts[m_loop_level] = (count == 0) ? 0 : count + 1;
      m_loop_start_addresses[m_loop_level] = static_cast<uint16_t>(curOffset);
      m_alt_loop_count = 0;
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_GOTO: {
      const uint16_t dest = readDecodedOffset(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      const auto length = curOffset - start;

      curOffset = dest;
      if (!isOffsetUsed(dest)) {
        addGenericEvent(start, length, "Jump", desc, Type::LoopForever);
      } else {
        stop_parser = !addLoopForever(start, length, "Jump");
      }
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_LOOP_END: {
      if (m_loop_counts[m_loop_level] == 0) {
        // infinite loop
        stop_parser = !addLoopForever(start, curOffset - start);
        curOffset = m_loop_start_addresses[m_loop_level];
      } else {
        addGenericEvent(start, curOffset - start, "Loop End", desc, Type::RepeatEnd);

        m_loop_counts[m_loop_level]--;
        if (m_loop_counts[m_loop_level] == 0) {
          // repeat end
          m_loop_level--;
        } else {
          // repeat again
          curOffset = m_loop_start_addresses[m_loop_level];
        }
      }
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_LOOP_BREAK: {
      const uint8_t target_count = readByte(curOffset++);
      const uint16_t dest = readDecodedOffset(curOffset);
      curOffset += 2;

      desc = fmt::format("Count: {:d}  Destination: ${:04X}", target_count, dest);
      addGenericEvent(start, curOffset - start, "Loop Break / Jump to Volta", desc,
                      Type::LoopBreak);
      
      if (++m_alt_loop_count == target_count) {
        curOffset = dest;
      }
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_UNDEFINED:
    default:
      desc = logEvent(command);
      addUnknown(start, curOffset - start, "Unknown Event", desc);
      stop_parser = true;
      break;
  }

  return !stop_parser;
}
