/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "ItikitiSnesSeq.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(ItikitiSnes);

static constexpr uint8_t NOTE_VELOCITY = 100;
static constexpr std::array<uint8_t, 16> kMasterNoteLengths = {
    0xc0, 0x90, 0x60, 0x48, 0x40, 0x30, 0x24, 0x20, 0x18, 0x12, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03};
static constexpr std::array<uint8_t, 7> kDefaultNoteLengths = {0xc0, 0x60, 0x48, 0x30,
                                                               0x24, 0x18, 0x0c};

ItikitiSnesSeq::ItikitiSnesSeq(RawFile *file, uint32_t offset, std::string new_name)
    : VGMSeq(ItikitiSnesFormat::name, file, offset, 0, std::move(new_name)) {
  bLoadTickByTick = true;
  bAllowDiscontinuousTrackData = true;
  bUseLinearAmplitudeScale = true;

  UseReverb();
  AlwaysWriteInitialReverb(0);

  LoadEventMap(m_event_map);
}

void ItikitiSnesSeq::ResetVars() {
  VGMSeq::ResetVars();
}

bool ItikitiSnesSeq::GetHeaderInfo() {
  SetPPQN(kItikitiSnesSeqTimebase);

  VGMHeader *header = addHeader(dwOffset, 0);
  header->addUnknownChild(dwOffset, 1);
  header->addSimpleChild(dwOffset + 1, 1, "Number of Tracks");

  nNumTracks = GetByte(dwOffset + 1);
  if (nNumTracks == 0 || nNumTracks > 8)
    return false;

  const uint16_t first_offset = GetShort(dwOffset + 2);
  const auto first_address = dwOffset + 2 + (2 * nNumTracks);
  m_base_offset = static_cast<uint16_t>(first_address) - first_offset;

  for (unsigned int track_index = 0; track_index < nNumTracks; track_index++) {
    std::stringstream track_name;
    track_name << "Track " << (track_index + 1);

    const uint32_t offset_to_pointer = dwOffset + 2 + (2 * track_index);
    header->addSimpleChild(offset_to_pointer, 2, track_name.str());
  }

  header->SetGuessedLength();
  return true;
}


bool ItikitiSnesSeq::GetTrackPointers() {
  for (unsigned int track_index = 0; track_index < nNumTracks; track_index++) {
    const uint32_t offset_to_pointer = dwOffset + 2 + (2 * track_index);
    const uint32_t offset = ReadDecodedOffset(offset_to_pointer);
    auto track = std::make_unique<ItikitiSnesTrack>(this, offset);
    aTracks.push_back(track.release());
  }
  return true;
}

void ItikitiSnesSeq::LoadEventMap(std::unordered_map<uint8_t, ItikitiSnesSeqEventType> &event_map) {
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

void ItikitiSnesTrack::ResetVars() {
  SeqTrack::ResetVars();
  m_note_number_base = 0;
  m_note_length_table = kDefaultNoteLengths;
  m_loop_level = -1;
  m_alt_loop_count = 0; // actual driver does not initialize this
}

bool ItikitiSnesTrack::ReadEvent() {
  auto *seq = this->seq();

  const auto start = curOffset;
  if (curOffset >= 0x10000)
    return false;

  const uint8_t command = GetByte(curOffset++);
  const auto event_type = seq->GetEventType(command);

  bool stop_parser = false;
  std::stringstream description;
  std::string descr;
  switch (event_type) {
    case ItikitiSnesSeqEventType::EVENT_UNKNOWN0:
      descr = logEvent(command, spdlog::level::debug);
      AddUnknown(start, curOffset - start, "Unknown Event", descr);
      break;

    case ItikitiSnesSeqEventType::EVENT_UNKNOWN1: {
      const auto arg1 = GetByte(curOffset++);
      descr = logEvent(command, spdlog::level::debug, "Event", arg1);
      AddUnknown(start, curOffset - start, "Unknown Event", descr);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_UNKNOWN2: {
      const auto arg1 = GetByte(curOffset++);
      const auto arg2 = GetByte(curOffset++);
      descr = logEvent(command, spdlog::level::debug, "Event", arg1, arg2);
      AddUnknown(start, curOffset - start, "Unknown Event", descr);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_UNKNOWN3: {
      const auto arg1 = GetByte(curOffset++);
      const auto arg2 = GetByte(curOffset++);
      const auto arg3 = GetByte(curOffset++);
      descr = logEvent(command, spdlog::level::debug, "Event", arg1, arg2, arg3);
      AddUnknown(start, curOffset - start, "Unknown Event", descr);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOTE: {
      const uint8_t len_index = command & 7;

      uint8_t len;
      if (len_index == 7)
        len = GetByte(curOffset++);
      else
        len = m_note_length_table[len_index];

      const uint8_t dur = len - 2;

      const bool rest = command >= 0xf8;
      const bool tie = command >= 0xf0 && command <= 0xf7;
      if (rest) {
        AddRest(start, curOffset - start, len);
      } else if (tie) {
        MakePrevDurNoteEnd(GetTime() + dur);
        AddGenericEvent(start, curOffset - start, "Tie", description.str(), CLR_TIE, ICON_NOTE);
        AddTime(len);
      } else {
        const uint8_t key_index = command >> 3;
        const int8_t key = static_cast<int8_t>(kItikitiSnesSeqNoteKeyBias + key_index - 6 + m_note_number_base);
        AddNoteByDur(start, curOffset - start, key, NOTE_VELOCITY, dur);
        AddTime(len);
      }
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_END: {
      AddEndOfTrack(start, curOffset - start);
      stop_parser = true;
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_MASTER_VOLUME: {
      const uint8_t vol = GetByte(curOffset++);
      description << "Volume: " << vol;
      AddGenericEvent(start, curOffset - start, "Master Volume", description.str(), CLR_VOLUME,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ECHO_VOLUME: {
      const uint8_t vol = GetByte(curOffset++);
      description << "Volume: " << vol;
      AddGenericEvent(start, curOffset - start, "Echo Volume", description.str(), CLR_REVERB,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_CHANNEL_VOLUME: {
      const uint8_t vol = GetByte(curOffset++);
      AddVol(start, curOffset - start, vol >> 1);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ECHO_FEEDBACK_FIR: {
      const int8_t feedback = static_cast<int8_t>(GetByte(curOffset++));
      const uint8_t filter_index = GetByte(curOffset++);
      description << "Feedback: " << feedback << "  FIR: " << filter_index;
      AddGenericEvent(start, curOffset - start, "Echo Feedback & FIR", description.str(),
                      CLR_REVERB, ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TEMPO: {
      const uint8_t tempo = GetByte(curOffset++);
      AddTempoBPM(start, curOffset - start, seq->GetTempoInBpm(tempo));
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TEMPO_FADE: {
      const uint8_t fade_length = GetByte(curOffset++);
      const uint8_t tempo = GetByte(curOffset++);
      AddTempoBPMSlide(start, curOffset - start, fade_length, seq->GetTempoInBpm(tempo));
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOISE_FREQ: {
      // Driver bug: This command sets the noise frequency for the next track from the current.
      const uint8_t nck = GetByte(curOffset++) & 0x1f;
      description << "Noise Frequency (NCK): " << nck;
      AddGenericEvent(start, curOffset - start, "Noise Frequency", description.str(),
                      CLR_CHANGESTATE, ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_SELECT_NOTE_LENGTH_PATTERN: {
      const uint16_t bits = GetShort(curOffset);
      curOffset += 2;
      description << "Length Bits: 0x" << std::hex << std::setfill('0') << std::setw(4) << bits;
      AddGenericEvent(start, curOffset - start, "Note Length Pattern", description.str(),
                      CLR_DURNOTE);

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
      const uint8_t arg1 = GetByte(curOffset++);
      const uint8_t arg2 = GetByte(curOffset++);
      const uint8_t arg3 = GetByte(curOffset++);
      const uint8_t arg4 = GetByte(curOffset++);
      const uint8_t arg5 = GetByte(curOffset++);
      const uint8_t arg6 = GetByte(curOffset++);
      const uint8_t arg7 = GetByte(curOffset++);
      description << "Lengths: " << arg1 << ", " << arg2 << ", " << arg3 << ", " << arg4
                  << ", " << arg5 << ", " << arg6 << ", " << arg7;
      AddGenericEvent(start, curOffset - start, "Custom Note Length Pattern", description.str(),
                      CLR_DURNOTE);

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
      const uint8_t note_number = GetByte(curOffset++);
      description << "Note Number: " << note_number;
      AddGenericEvent(start, curOffset - start, "Note Number Base", description.str(),
                      CLR_CHANGESTATE, ICON_CONTROL);
      m_note_number_base = note_number;
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_VOLUME: {
      const uint8_t vol = GetByte(curOffset++);
      AddExpression(start, curOffset - start, vol >> 1);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_VOLUME_FADE: {
      const uint8_t fade_length = GetByte(curOffset++);
      const uint8_t vol = GetByte(curOffset++);
      AddExpressionSlide(start, curOffset - start, fade_length, vol >> 1);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PAN: {
      const uint8_t pan = GetByte(curOffset++);
      const uint8_t midi_pan = ConvertLinearPercentPanValToStdMidiVal(pan / 255.0);
      AddPan(start, curOffset - start, midi_pan);
      // TODO: apply volume scale
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PAN_FADE: {
      const uint8_t fade_length = GetByte(curOffset++);
      const uint8_t pan = GetByte(curOffset++);
      const uint8_t midi_pan = ConvertLinearPercentPanValToStdMidiVal(pan / 255.0);
      AddPanSlide(start, curOffset - start, fade_length, midi_pan); // TODO: fix pan curve
      // TODO: apply volume scale
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PROGRAM_CHANGE: {
      const uint8_t instrument = GetByte(curOffset++);
      AddProgramChange(start, curOffset - start, instrument);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TUNING: {
      const int8_t tuning = static_cast<int8_t>(GetByte(curOffset++));
      const double semitones = tuning / 128.0; // not very well verified
      AddFineTuning(start, curOffset - start, semitones * 100.0);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ADSR_AR: {
      const uint8_t ar = GetByte(curOffset++) & 15;
      description << "AR: " << ar;
      AddGenericEvent(start, curOffset - start, "ADSR Attack Rate", description.str(), CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ADSR_DR: {
      const uint8_t dr = GetByte(curOffset++) & 7;
      description << "DR: " << dr;
      AddGenericEvent(start, curOffset - start, "ADSR Decay Rate", description.str(), CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ADSR_SL: {
      const uint8_t sl = GetByte(curOffset++) & 7;
      description << "SL: " << sl;
      AddGenericEvent(start, curOffset - start, "ADSR Sustain Level", description.str(), CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ADSR_SR: {
      const uint8_t sr = GetByte(curOffset++) & 15;
      description << "SR: " << sr;
      AddGenericEvent(start, curOffset - start, "ADSR Sustain Rate", description.str(), CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ADSR_DEFAULT: {
      AddGenericEvent(start, curOffset - start, "Default ADSR", description.str(), CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TRANSPOSE_ABS: {
      const int8_t transpose = static_cast<int8_t>(GetByte(curOffset++));
      AddTranspose(start, curOffset - start, transpose);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TRANSPOSE_REL: {
      const int8_t transpose_amount = static_cast<int8_t>(GetByte(curOffset++));
      const int8_t new_transpose = static_cast<int8_t>(std::max(-128, std::min(127, transpose + transpose_amount)));
      AddTranspose(start, curOffset - start, new_transpose, "Transpose (Relative)");
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_VIBRATO_ON: {
        const uint8_t delay = GetByte(curOffset++);
        const uint8_t rate = GetByte(curOffset++);
        const uint8_t depth = GetByte(curOffset++);
        description << "Delay: " << delay << "  Rate: " << rate << "  Depth: " << depth;
        AddGenericEvent(start, curOffset - start, "Vibrato", description.str(), CLR_MODULATION,
                        ICON_CONTROL);
        break;
    }

    case ItikitiSnesSeqEventType::EVENT_VIBRATO_OFF: {
      // The command changes vibrato depth to 0.
      AddGenericEvent(start, curOffset - start, "Vibrato Off", description.str(), CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }


    case ItikitiSnesSeqEventType::EVENT_TREMOLO_ON: {
      const uint8_t delay = GetByte(curOffset++);
      const uint8_t rate = GetByte(curOffset++);
      const uint8_t depth = GetByte(curOffset++);
      description << "Delay: " << delay << "  Rate: " << rate << "  Depth: " << depth;
      AddGenericEvent(start, curOffset - start, "Tremolo", description.str(), CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_TREMOLO_OFF: {
      AddGenericEvent(start, curOffset - start, "Tremolo Off", description.str(), CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PAN_LFO_ON: {
      const uint8_t depth = GetByte(curOffset++);
      const uint8_t rate = GetByte(curOffset++);
      description << "Depth: " << depth << "  Rate: " << rate;
      AddGenericEvent(start, curOffset - start, "Pan LFO", description.str(), CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PAN_LFO_OFF: {
      AddGenericEvent(start, curOffset - start, "Pan LFO Off", description.str(), CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOISE_ON: {
      AddGenericEvent(start, curOffset - start, "Noise On", description.str(), CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOISE_OFF: {
      AddGenericEvent(start, curOffset - start, "Noise Off", description.str(), CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PITCH_MOD_ON: {
      AddGenericEvent(start, curOffset - start, "Pitch Modulation On", description.str(),
                      CLR_CHANGESTATE, ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PITCH_MOD_OFF: {
      AddGenericEvent(start, curOffset - start, "Pitch Modulation Off", description.str(),
                      CLR_CHANGESTATE, ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ECHO_ON: {
      AddReverb(start, curOffset - start, 40, "Echo On");
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_ECHO_OFF: {
      AddReverb(start, curOffset - start, 0, "Echo Off");
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PORTAMENTO_ON: {
      const uint8_t speed = GetByte(curOffset++); // in ticks
      AddPortamentoTime(start, curOffset - start, speed, "Portamento");
      AddPortamentoNoItem(true);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PORTAMENTO_OFF: {
      AddPortamentoNoItem(false);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_SPECIAL: {
      const uint8_t value = GetByte(curOffset++);
      if (value <= 0x7f) {
        description << "Range: " << value << " semitones";
        AddGenericEvent(start, curOffset - start, "Note Randomization On", description.str(),
                        CLR_CHANGESTATE, ICON_CONTROL);
      } else if (value == 0x83) {
        // Detailed behavior has not yet been analyzed
        AddGenericEvent(start, curOffset - start, "Change Volume Mode (Global)", description.str(),
                        CLR_VOLUME, ICON_CONTROL);
      } else {
        descr = logEvent(command, spdlog::level::debug, "Event", value);
        AddUnknown(start, curOffset - start, "Unknown Event", descr);
      }
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_NOTE_RANDOMIZATION_OFF: {
      AddGenericEvent(start, curOffset - start, "Note Randomization Off", description.str(),
                      CLR_CHANGESTATE, ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_PITCH_SLIDE: {
      const uint8_t length = GetByte(curOffset++); // in ticks
      const int8_t semitones = static_cast<int8_t>(GetByte(curOffset++));
      description << "Length: " << length << "  Key: " << semitones << " semitones";
      AddGenericEvent(start, curOffset - start, "Pitch Slide", description.str(), CLR_PITCHBEND,
                      ICON_CONTROL);
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_LOOP_START: {
      const uint8_t count = GetByte(curOffset++);
      description << "Repeat Count: ";
      if (count == 0)
        description << "Infinite";
      else
        description << (count + 1);
      AddGenericEvent(start, curOffset - start, "Loop Start", description.str(), CLR_LOOP,
                      ICON_STARTREP);

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
      const uint16_t dest = ReadDecodedOffset(curOffset);
      curOffset += 2;
      description << "Destination: $" << std::hex << std::setfill('0') << std::setw(4)
                  << std::uppercase << dest;
      const auto length = curOffset - start;

      curOffset = dest;
      if (!IsOffsetUsed(dest)) {
        AddGenericEvent(start, length, "Jump", description.str(), CLR_LOOPFOREVER);
      } else {
        stop_parser = !AddLoopForever(start, length, "Jump");
      }
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_LOOP_END: {
      if (m_loop_counts[m_loop_level] == 0) {
        // infinite loop
        stop_parser = !AddLoopForever(start, curOffset - start);
        curOffset = m_loop_start_addresses[m_loop_level];
      } else {
        AddGenericEvent(start, curOffset - start, "Loop End", description.str(), CLR_LOOP,
                        ICON_ENDREP);

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
      const uint8_t target_count = GetByte(curOffset++);
      const uint16_t dest = ReadDecodedOffset(curOffset);
      curOffset += 2;

      description << "Count: " << target_count << "  Destination: $" << std::hex << std::setfill('0')
                  << std::setw(4) << std::uppercase << dest;
      AddGenericEvent(start, curOffset - start, "Loop Break / Jump to Volta", description.str(),
                      CLR_LOOP, ICON_ENDREP);
      
      if (++m_alt_loop_count == target_count) {
        curOffset = dest;
      }
      break;
    }

    case ItikitiSnesSeqEventType::EVENT_UNDEFINED:
    default:
      descr = logEvent(command);
      AddUnknown(start, curOffset - start, "Unknown Event", descr);
      stop_parser = true;
      break;
  }

  return !stop_parser;
}
