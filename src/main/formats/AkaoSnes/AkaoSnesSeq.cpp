/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "AkaoSnesSeq.h"
#include "AkaoSnesInstr.h"
#include "ScaleConversion.h"
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(AkaoSnes);

static constexpr uint16_t SEQ_PPQN = 48;
static constexpr int MAX_TRACKS = 8;
static constexpr uint8_t NOTE_VELOCITY = 100;

//  **********
//  AkaoSnesSeq
//  **********

AkaoSnesSeq::AkaoSnesSeq(RawFile *file,
                         AkaoSnesVersion ver,
                         AkaoSnesMinorVersion minorVer,
                         uint32_t seqdataOffset,
                         uint32_t addrAPURelocBase,
                         std::string name)
    : VGMSeq(AkaoSnesFormat::name, file, seqdataOffset, 0, std::move(name)),
      version(ver),
      minorVersion(minorVer),
      addrAPURelocBase(addrAPURelocBase),
      addrROMRelocBase(addrAPURelocBase),
      addrSequenceEnd(0) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  LoadEventMap();
}

AkaoSnesSeq::~AkaoSnesSeq() {}

void AkaoSnesSeq::resetVars() {
  VGMSeq::resetVars();
}

bool AkaoSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(dwOffset, 0);
  uint32_t curOffset = dwOffset;
  if (version == AKAOSNES_V1 || version == AKAOSNES_V2) {
    // Earlier versions are not relocatable
    // All addresses are plain APU addresses
    addrROMRelocBase = addrAPURelocBase;
    addrSequenceEnd = romAddressToApuAddress(0);
  }
  else {
    // Later versions are relocatable
    if (version == AKAOSNES_V3) {
      header->addChild(curOffset, 2, "ROM Address Base");
      addrROMRelocBase = readShort(curOffset);
      if (minorVersion != AKAOSNES_V3_FFMQ) {
        curOffset += 2;
      }

      header->addChild(curOffset + MAX_TRACKS * 2, 2, "End Address");
      addrSequenceEnd = getShortAddress(curOffset + MAX_TRACKS * 2);
    }
    else if (version == AKAOSNES_V4) {
      header->addChild(curOffset, 2, "ROM Address Base");
      addrROMRelocBase = readShort(curOffset);
      curOffset += 2;

      header->addChild(curOffset, 2, "End Address");
      addrSequenceEnd = getShortAddress(curOffset);
      curOffset += 2;
    }

    // calculate sequence length
    if (addrSequenceEnd < dwOffset) {
      return false;
    }
    unLength = addrSequenceEnd - dwOffset;
  }

  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t addrTrackStart = getShortAddress(curOffset);
    if (addrTrackStart != addrSequenceEnd) {
      header->addChild(curOffset, 2, fmt::format("Track Pointer {}", trackIndex + 1));
    }
    else {
      header->addChild(curOffset, 2, "NULL");
    }
    curOffset += 2;
  }

  header->setGuessedLength();

  return true;
}


bool AkaoSnesSeq::parseTrackPointers(void) {
  uint32_t curOffset = dwOffset;
  if (version == AKAOSNES_V3) {
    if (minorVersion != AKAOSNES_V3_FFMQ) {
      curOffset += 2;
    }
  }
  else if (version == AKAOSNES_V4) {
    curOffset += 4;
  }

  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t addrTrackStart = getShortAddress(curOffset);
    if (addrTrackStart != addrSequenceEnd) {
      AkaoSnesTrack *track = new AkaoSnesTrack(this, addrTrackStart);
      aTracks.push_back(track);
    }
    curOffset += 2;
  }

  return true;
}

void AkaoSnesSeq::LoadEventMap() {
  // opcode min/max
  switch (version) {
    case AKAOSNES_V1:
      STATUS_NOTE_MAX = 0xd1;
      STATUS_NOTEINDEX_REST = 12;
      STATUS_NOTEINDEX_TIE = 13;
      break;

    case AKAOSNES_V2:
      STATUS_NOTE_MAX = 0xd1;
      STATUS_NOTEINDEX_REST = 12;
      STATUS_NOTEINDEX_TIE = 13;
      break;

    case AKAOSNES_V3:
      STATUS_NOTE_MAX = 0xd1;
      STATUS_NOTEINDEX_TIE = 12;
      STATUS_NOTEINDEX_REST = 13;
      break;

    case AKAOSNES_V4:
      STATUS_NOTE_MAX = 0xc3;
      STATUS_NOTEINDEX_TIE = 12;
      STATUS_NOTEINDEX_REST = 13;
      break;

    default:
      break;
  }

  // duration table
  if (version == AKAOSNES_V1) {
    const uint8_t NOTE_DUR_TABLE_FF4[15] = {
        0xc0, 0x90, 0x60, 0x48, 0x40, 0x30, 0x24, 0x20, 0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03
    };
    NOTE_DUR_TABLE.assign(std::begin(NOTE_DUR_TABLE_FF4), std::end(NOTE_DUR_TABLE_FF4));
  }
  else if (version == AKAOSNES_V2 || version == AKAOSNES_V3) {
    const uint8_t NOTE_DUR_TABLE_RS1[15] = {
        0xc0, 0x90, 0x60, 0x40, 0x48, 0x30, 0x20, 0x24, 0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03
    };
    NOTE_DUR_TABLE.assign(std::begin(NOTE_DUR_TABLE_RS1), std::end(NOTE_DUR_TABLE_RS1));
  }
  else { // AKAOSNES_V4
    const uint8_t NOTE_DUR_TABLE_RS2[14] = {
        0xc0, 0x60, 0x40, 0x48, 0x30, 0x20, 0x24, 0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03
    };
    NOTE_DUR_TABLE.assign(std::begin(NOTE_DUR_TABLE_RS2), std::end(NOTE_DUR_TABLE_RS2));
  }

  // timer 0 frequency
  if (version == AKAOSNES_V4) {
    if (minorVersion == AKAOSNES_V4_RS2 || minorVersion == AKAOSNES_V4_LAL) {
      TIMER0_FREQUENCY = 0x24;
    }
    else if (minorVersion == AKAOSNES_V4_FM || minorVersion == AKAOSNES_V4_CT) {
      TIMER0_FREQUENCY = 0x2a;
    }
    else {
      TIMER0_FREQUENCY = 0x27;
    }
  }
  else { // AKAOSNES_V1, AKAOSNES_V2, AKAOSNES_V3
    TIMER0_FREQUENCY = 0x24;
  }

  // pan bitdepth
  if (version == AKAOSNES_V1 || version == AKAOSNES_V3) {
    PAN_8BIT = true;
  }
  else if (version == AKAOSNES_V2) {
    PAN_8BIT = false;
  }
  else { // AKAOSNES_V4
    if (minorVersion == AKAOSNES_V4_RS2 || minorVersion == AKAOSNES_V4_LAL) {
      PAN_8BIT = true;
    }
    else {
      PAN_8BIT = false;
    }
  }

  for (uint8_t statusByte = 0x00; statusByte <= STATUS_NOTE_MAX; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  if (version == AKAOSNES_V1) {
    EventMap[0xd2] = EVENT_TEMPO_FADE;
    EventMap[0xd3] = EVENT_NOP1;
    EventMap[0xd4] = EVENT_ECHO_VOLUME;
    EventMap[0xd5] = EVENT_ECHO_FEEDBACK_FIR;
    EventMap[0xd6] = EVENT_PITCH_SLIDE_ON;
    EventMap[0xd7] = EVENT_TREMOLO_ON;
    EventMap[0xd8] = EVENT_VIBRATO_ON;
    EventMap[0xd9] = EVENT_PAN_LFO_ON_WITH_DELAY;
    EventMap[0xda] = EVENT_OCTAVE;
    EventMap[0xdb] = EVENT_PROGCHANGE;
    EventMap[0xdc] = EVENT_VOLUME_ENVELOPE;
    EventMap[0xdd] = EVENT_GAIN_RELEASE;
    EventMap[0xde] = EVENT_DURATION_RATE;
    EventMap[0xdf] = EVENT_NOISE_FREQ;
    EventMap[0xe0] = EVENT_LOOP_START;
    EventMap[0xe1] = EVENT_OCTAVE_UP;
    EventMap[0xe2] = EVENT_OCTAVE_DOWN;
    EventMap[0xe3] = EVENT_NOP;
    EventMap[0xe4] = EVENT_NOP;
    EventMap[0xe5] = EVENT_NOP;
    EventMap[0xe6] = EVENT_PITCH_SLIDE_OFF;
    EventMap[0xe7] = EVENT_TREMOLO_OFF;
    EventMap[0xe8] = EVENT_VIBRATO_OFF;
    EventMap[0xe9] = EVENT_PAN_LFO_OFF;
    EventMap[0xea] = EVENT_ECHO_ON;
    EventMap[0xeb] = EVENT_ECHO_OFF;
    EventMap[0xec] = EVENT_NOISE_ON;
    EventMap[0xed] = EVENT_NOISE_OFF;
    EventMap[0xee] = EVENT_PITCHMOD_ON;
    EventMap[0xef] = EVENT_PITCHMOD_OFF;
    EventMap[0xf0] = EVENT_LOOP_END;
    EventMap[0xf1] = EVENT_END;
    EventMap[0xf2] = EVENT_VOLUME_FADE;
    EventMap[0xf3] = EVENT_PAN_FADE;
    EventMap[0xf4] = EVENT_GOTO;
    EventMap[0xf5] = EVENT_LOOP_BREAK;
    EventMap[0xf6] = EVENT_UNKNOWN0; // cpu-controlled jump?
    EventMap[0xf7] = EVENT_END; // duplicated
    EventMap[0xf8] = EVENT_END; // duplicated
    EventMap[0xf9] = EVENT_END; // duplicated
    EventMap[0xfa] = EVENT_END; // duplicated
    EventMap[0xfb] = EVENT_END; // duplicated
    EventMap[0xfc] = EVENT_END; // duplicated
    EventMap[0xfd] = EVENT_END; // duplicated
    EventMap[0xfe] = EVENT_END; // duplicated
    EventMap[0xff] = EVENT_END; // duplicated
  }
  else if (version == AKAOSNES_V2) {
    EventMap[0xd2] = EVENT_TEMPO;
    EventMap[0xd3] = EVENT_TEMPO_FADE;
    EventMap[0xd4] = EVENT_VOLUME;
    EventMap[0xd5] = EVENT_VOLUME_FADE;
    EventMap[0xd6] = EVENT_PAN;
    EventMap[0xd7] = EVENT_PAN_FADE;
    EventMap[0xd8] = EVENT_ECHO_VOLUME;
    EventMap[0xd9] = EVENT_ECHO_VOLUME_FADE;
    EventMap[0xda] = EVENT_TRANSPOSE_ABS;
    EventMap[0xdb] = EVENT_PITCH_SLIDE_ON;
    EventMap[0xdc] = EVENT_PITCH_SLIDE_OFF;
    EventMap[0xdd] = EVENT_TREMOLO_ON;
    EventMap[0xde] = EVENT_TREMOLO_OFF;
    EventMap[0xdf] = EVENT_VIBRATO_ON;
    EventMap[0xe0] = EVENT_VIBRATO_OFF;
    EventMap[0xe1] = EVENT_NOISE_FREQ;
    EventMap[0xe2] = EVENT_NOISE_ON;
    EventMap[0xe3] = EVENT_NOISE_OFF;
    EventMap[0xe4] = EVENT_PITCHMOD_ON;
    EventMap[0xe5] = EVENT_PITCHMOD_OFF;
    EventMap[0xe6] = EVENT_ECHO_FEEDBACK_FIR;
    EventMap[0xe7] = EVENT_ECHO_ON;
    EventMap[0xe8] = EVENT_ECHO_OFF;
    EventMap[0xe9] = EVENT_PAN_LFO_ON;
    EventMap[0xea] = EVENT_PAN_LFO_OFF;
    EventMap[0xeb] = EVENT_OCTAVE;
    EventMap[0xec] = EVENT_OCTAVE_UP;
    EventMap[0xed] = EVENT_OCTAVE_DOWN;
    EventMap[0xee] = EVENT_LOOP_START;
    EventMap[0xef] = EVENT_LOOP_END;
    EventMap[0xf0] = EVENT_LOOP_BREAK;
    EventMap[0xf1] = EVENT_GOTO;
    EventMap[0xf2] = EVENT_SLUR_ON;
    EventMap[0xf3] = EVENT_PROGCHANGE;
    EventMap[0xf4] = EVENT_VOLUME_ENVELOPE;
    EventMap[0xf5] = EVENT_SLUR_OFF;
    EventMap[0xf6] = EVENT_UNKNOWN2; // cpu-controlled jump?
    EventMap[0xf7] = EVENT_TUNING;
    EventMap[0xf8] = EVENT_END; // duplicated
    EventMap[0xf9] = EVENT_END; // duplicated
    EventMap[0xfa] = EVENT_END; // duplicated
    EventMap[0xfb] = EVENT_END; // duplicated
    EventMap[0xfc] = EVENT_END; // duplicated
    EventMap[0xfd] = EVENT_END; // duplicated
    EventMap[0xfe] = EVENT_END; // duplicated
    EventMap[0xff] = EVENT_END; // duplicated
  }
  else if (version == AKAOSNES_V3) {
    // V3-V4 common
    EventMap[0xd2] = EVENT_VOLUME;
    EventMap[0xd3] = EVENT_VOLUME_FADE;
    EventMap[0xd4] = EVENT_PAN;
    EventMap[0xd5] = EVENT_PAN_FADE;
    EventMap[0xd6] = EVENT_PITCH_SLIDE;
    EventMap[0xd7] = EVENT_VIBRATO_ON;
    EventMap[0xd8] = EVENT_VIBRATO_OFF;
    EventMap[0xd9] = EVENT_TREMOLO_ON;
    EventMap[0xda] = EVENT_TREMOLO_OFF;
    EventMap[0xdb] = EVENT_PAN_LFO_ON;
    EventMap[0xdc] = EVENT_PAN_LFO_OFF;
    EventMap[0xdd] = EVENT_NOISE_FREQ;
    EventMap[0xde] = EVENT_NOISE_ON;
    EventMap[0xdf] = EVENT_NOISE_OFF;
    EventMap[0xe0] = EVENT_PITCHMOD_ON;
    EventMap[0xe1] = EVENT_PITCHMOD_OFF;
    EventMap[0xe2] = EVENT_ECHO_ON;
    EventMap[0xe3] = EVENT_ECHO_OFF;
    EventMap[0xe4] = EVENT_OCTAVE;
    EventMap[0xe5] = EVENT_OCTAVE_UP;
    EventMap[0xe6] = EVENT_OCTAVE_DOWN;
    EventMap[0xe7] = EVENT_TRANSPOSE_ABS;
    EventMap[0xe8] = EVENT_TRANSPOSE_REL;
    EventMap[0xe9] = EVENT_TUNING;
    EventMap[0xea] = EVENT_PROGCHANGE;
    EventMap[0xeb] = EVENT_ADSR_AR;
    EventMap[0xec] = EVENT_ADSR_DR;
    EventMap[0xed] = EVENT_ADSR_SL;
    EventMap[0xee] = EVENT_ADSR_SR;
    EventMap[0xef] = EVENT_ADSR_DEFAULT;
    EventMap[0xf0] = EVENT_LOOP_START;
    EventMap[0xf1] = EVENT_LOOP_END;

    // V3 common
    EventMap[0xf2] = EVENT_END;
    EventMap[0xf3] = EVENT_TEMPO;
    EventMap[0xf4] = EVENT_TEMPO_FADE;
    EventMap[0xf5] = EVENT_ECHO_VOLUME; // No Operation in Seiken Densetsu 2
    EventMap[0xf6] = EVENT_ECHO_VOLUME_FADE; // No Operation in Seiken Densetsu 2
    EventMap[0xf7] = EVENT_ECHO_FEEDBACK_FIR; // No Operation in Seiken Densetsu 2
    EventMap[0xf8] = EVENT_MASTER_VOLUME;
    EventMap[0xf9] = EVENT_LOOP_BREAK;
    EventMap[0xfa] = EVENT_GOTO;
    EventMap[0xfb] = EVENT_CPU_CONTROLED_JUMP;

    // game-specific mappings
    if (minorVersion == AKAOSNES_V3_SD2) {
      EventMap[0xfc] = EVENT_LOOP_RESTART;
      EventMap[0xfd] = EVENT_IGNORE_MASTER_VOLUME_BY_PROGNUM;
      EventMap[0xfe] = EVENT_END;
      EventMap[0xff] = EVENT_END;
    }
    else {
      EventMap[0xfc] = EVENT_END;
      EventMap[0xfd] = EVENT_END;
      EventMap[0xfe] = EVENT_END;
      EventMap[0xff] = EVENT_END;
    }
  }
  else if (version == AKAOSNES_V4) {
    // V3-V4 common
    EventMap[0xc4] = EVENT_VOLUME;
    EventMap[0xc5] = EVENT_VOLUME_FADE;
    EventMap[0xc6] = EVENT_PAN;
    EventMap[0xc7] = EVENT_PAN_FADE;
    EventMap[0xc8] = EVENT_PITCH_SLIDE;
    EventMap[0xc9] = EVENT_VIBRATO_ON;
    EventMap[0xca] = EVENT_VIBRATO_OFF;
    EventMap[0xcb] = EVENT_TREMOLO_ON;
    EventMap[0xcc] = EVENT_TREMOLO_OFF;
    EventMap[0xcd] = EVENT_PAN_LFO_ON;
    EventMap[0xce] = EVENT_PAN_LFO_OFF;
    EventMap[0xcf] = EVENT_NOISE_FREQ;
    EventMap[0xd0] = EVENT_NOISE_ON;
    EventMap[0xd1] = EVENT_NOISE_OFF;
    EventMap[0xd2] = EVENT_PITCHMOD_ON;
    EventMap[0xd3] = EVENT_PITCHMOD_OFF;
    EventMap[0xd4] = EVENT_ECHO_ON;
    EventMap[0xd5] = EVENT_ECHO_OFF;
    EventMap[0xd6] = EVENT_OCTAVE;
    EventMap[0xd7] = EVENT_OCTAVE_UP;
    EventMap[0xd8] = EVENT_OCTAVE_DOWN;
    EventMap[0xd9] = EVENT_TRANSPOSE_ABS;
    EventMap[0xda] = EVENT_TRANSPOSE_REL;
    EventMap[0xdb] = EVENT_TUNING;
    EventMap[0xdc] = EVENT_PROGCHANGE;
    EventMap[0xdd] = EVENT_ADSR_AR;
    EventMap[0xde] = EVENT_ADSR_DR;
    EventMap[0xdf] = EVENT_ADSR_SL;
    EventMap[0xe0] = EVENT_ADSR_SR;
    EventMap[0xe1] = EVENT_ADSR_DEFAULT;
    EventMap[0xe2] = EVENT_LOOP_START;
    EventMap[0xe3] = EVENT_LOOP_END;

    // V4 common
    EventMap[0xe4] = EVENT_SLUR_ON;
    EventMap[0xe5] = EVENT_SLUR_OFF;
    EventMap[0xe6] = EVENT_LEGATO_ON;
    EventMap[0xe7] = EVENT_LEGATO_OFF;
    EventMap[0xe8] = EVENT_ONETIME_DURATION;
    EventMap[0xe9] = EVENT_JUMP_TO_SFX_LO;
    EventMap[0xea] = EVENT_JUMP_TO_SFX_HI;
    EventMap[0xeb] = EVENT_END; // exception: Gun Hazard
    EventMap[0xec] = EVENT_END; // duplicated
    EventMap[0xed] = EVENT_END; // duplicated
    EventMap[0xee] = EVENT_END; // duplicated
    EventMap[0xef] = EVENT_END; // duplicated
    EventMap[0xf0] = EVENT_TEMPO;
    EventMap[0xf1] = EVENT_TEMPO_FADE;
    EventMap[0xf2] = EVENT_ECHO_VOLUME;
    EventMap[0xf3] = EVENT_ECHO_VOLUME_FADE;

    // game-specific mappings
    if (minorVersion == AKAOSNES_V4_RS2) {
      EventMap[0xf4] = EVENT_ECHO_FEEDBACK_FIR;
      EventMap[0xf5] = EVENT_MASTER_VOLUME;
      EventMap[0xf6] = EVENT_LOOP_BREAK;
      EventMap[0xf7] = EVENT_GOTO;
      EventMap[0xf8] = EVENT_INC_CPU_SHARED_COUNTER;
      EventMap[0xf9] = EVENT_ZERO_CPU_SHARED_COUNTER;
      EventMap[0xfa] = EVENT_IGNORE_MASTER_VOLUME_BROKEN;
      EventMap[0xfb] = EVENT_END; // duplicated
      EventMap[0xfc] = EVENT_END; // duplicated
      EventMap[0xfd] = EVENT_END; // duplicated
      EventMap[0xfe] = EVENT_END; // duplicated
      EventMap[0xff] = EVENT_END; // duplicated
    }
    else if (minorVersion == AKAOSNES_V4_LAL) {
      EventMap[0xf4] = EVENT_ECHO_FEEDBACK_FIR;
      EventMap[0xf5] = EVENT_MASTER_VOLUME;
      EventMap[0xf6] = EVENT_LOOP_BREAK;
      EventMap[0xf7] = EVENT_GOTO;
      EventMap[0xf8] = EVENT_INC_CPU_SHARED_COUNTER;
      EventMap[0xf9] = EVENT_ZERO_CPU_SHARED_COUNTER;
      EventMap[0xfa] = EVENT_IGNORE_MASTER_VOLUME;
      EventMap[0xfb] = EVENT_CPU_CONTROLED_JUMP;
      EventMap[0xfc] = EVENT_END; // duplicated
      EventMap[0xfd] = EVENT_END; // duplicated
      EventMap[0xfe] = EVENT_END; // duplicated
      EventMap[0xff] = EVENT_END; // duplicated
    }
    else if (minorVersion == AKAOSNES_V4_FF6) {
      EventMap[0xf4] = EVENT_MASTER_VOLUME;
      EventMap[0xf5] = EVENT_LOOP_BREAK;
      EventMap[0xf6] = EVENT_GOTO;
      EventMap[0xf7] = EVENT_ECHO_FEEDBACK_FADE;
      EventMap[0xf8] = EVENT_ECHO_FIR_FADE;
      EventMap[0xf9] = EVENT_INC_CPU_SHARED_COUNTER;
      EventMap[0xfa] = EVENT_ZERO_CPU_SHARED_COUNTER;
      EventMap[0xfb] = EVENT_IGNORE_MASTER_VOLUME;
      EventMap[0xfc] = EVENT_CPU_CONTROLED_JUMP;
      EventMap[0xfd] = EVENT_END; // duplicated
      EventMap[0xfe] = EVENT_END; // duplicated
      EventMap[0xff] = EVENT_END; // duplicated
    }
    else if (minorVersion == AKAOSNES_V4_FM) {
      EventMap[0xf4] = EVENT_MASTER_VOLUME;
      EventMap[0xf5] = EVENT_LOOP_BREAK;
      EventMap[0xf6] = EVENT_GOTO;
      EventMap[0xf7] = EVENT_ECHO_FEEDBACK_FADE;
      EventMap[0xf8] = EVENT_ECHO_FIR_FADE;
      EventMap[0xf9] = EVENT_UNKNOWN1;
      EventMap[0xfa] = EVENT_CPU_CONTROLED_JUMP_V2;
      EventMap[0xfb] = EVENT_PERC_ON;
      EventMap[0xfc] = EVENT_PERC_OFF;
      EventMap[0xfd] = EVENT_VOLUME_ALT;
      EventMap[0xfe] = EVENT_END; // duplicated
      EventMap[0xff] = EVENT_END; // duplicated
    }
    else if (minorVersion == AKAOSNES_V4_CT) {
      EventMap[0xf4] = EVENT_MASTER_VOLUME;
      EventMap[0xf5] = EVENT_LOOP_BREAK;
      EventMap[0xf6] = EVENT_GOTO;
      EventMap[0xf7] = EVENT_ECHO_FEEDBACK_FADE;
      EventMap[0xf8] = EVENT_ECHO_FIR_FADE;
      EventMap[0xf9] = EVENT_CPU_CONTROLED_SET_VALUE;
      EventMap[0xfa] = EVENT_CPU_CONTROLED_JUMP_V2;
      EventMap[0xfb] = EVENT_PERC_ON;
      EventMap[0xfc] = EVENT_PERC_OFF;
      EventMap[0xfd] = EVENT_VOLUME_ALT;
      EventMap[0xfe] = EVENT_END; // duplicated
      EventMap[0xff] = EVENT_END; // duplicated
    }
    else if (minorVersion == AKAOSNES_V4_RS3) {
      EventMap[0xf4] = EVENT_VOLUME_ALT;
      EventMap[0xf5] = EVENT_LOOP_BREAK;
      EventMap[0xf6] = EVENT_GOTO;
      EventMap[0xf7] = EVENT_ECHO_FEEDBACK;
      EventMap[0xf8] = EVENT_ECHO_FIR;
      EventMap[0xf9] = EVENT_CPU_CONTROLED_SET_VALUE;
      EventMap[0xfa] = EVENT_CPU_CONTROLED_JUMP_V2;
      EventMap[0xfb] = EVENT_PERC_ON;
      EventMap[0xfc] = EVENT_PERC_OFF;
      EventMap[0xfd] = EVENT_PLAY_SFX;
      EventMap[0xfe] = EVENT_END; // duplicated
      EventMap[0xff] = EVENT_END; // duplicated
    }
    else if (minorVersion == AKAOSNES_V4_GH) {
      EventMap[0xeb] = EVENT_UNKNOWN1;
      EventMap[0xec] = EVENT_END;

      EventMap[0xf4] = EVENT_VOLUME_ALT;
      EventMap[0xf5] = EVENT_LOOP_BREAK;
      EventMap[0xf6] = EVENT_GOTO;
      EventMap[0xf7] = EVENT_ECHO_FEEDBACK;
      EventMap[0xf8] = EVENT_ECHO_FIR;
      EventMap[0xf9] = EVENT_CPU_CONTROLED_SET_VALUE;
      EventMap[0xfa] = EVENT_CPU_CONTROLED_JUMP_V2;
      EventMap[0xfb] = EVENT_PERC_ON;
      EventMap[0xfc] = EVENT_PERC_OFF;
      EventMap[0xfd] = EVENT_END; // duplicated
      EventMap[0xfe] = EVENT_END; // duplicated
      EventMap[0xff] = EVENT_END; // duplicated
    }
    else if (minorVersion == AKAOSNES_V4_BSGAME) {
      EventMap[0xf4] = EVENT_VOLUME_ALT;
      EventMap[0xf5] = EVENT_LOOP_BREAK;
      EventMap[0xf6] = EVENT_GOTO;
      EventMap[0xf7] = EVENT_ECHO_FEEDBACK;
      EventMap[0xf8] = EVENT_ECHO_FIR;
      EventMap[0xf9] = EVENT_CPU_CONTROLED_SET_VALUE;
      EventMap[0xfa] = EVENT_CPU_CONTROLED_JUMP_V2;
      EventMap[0xfb] = EVENT_PERC_ON;
      EventMap[0xfc] = EVENT_PERC_OFF;
      EventMap[0xfd] = EVENT_UNKNOWN1;
      EventMap[0xfe] = EVENT_UNKNOWN0;
      EventMap[0xff] = EVENT_END; // duplicated
    }
  }
}

double AkaoSnesSeq::getTempoInBPM(uint8_t tempo) const {
  if (tempo != 0 && TIMER0_FREQUENCY != 0) {
    return 60000000.0 / (SEQ_PPQN * (125 * TIMER0_FREQUENCY)) * (tempo / 256.0);
  }
  else {
    // since tempo 0 cannot be expressed, this function returns a very small value.
    return 1.0;
  }
}

uint16_t AkaoSnesSeq::romAddressToApuAddress(uint16_t romAddress) const {
  return (romAddress - addrROMRelocBase) + addrAPURelocBase;
}

uint16_t AkaoSnesSeq::getShortAddress(uint32_t offset) const {
  return romAddressToApuAddress(readShort(offset));
}

//  ************
//  AkaoSnesTrack
//  ************

AkaoSnesTrack::AkaoSnesTrack(AkaoSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  AkaoSnesTrack::resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void AkaoSnesTrack::resetVars() {
  SeqTrack::resetVars();

  octave = 6;
  onetimeDuration = 0;
  loopLevel = 0;
  slur = false;
  legato = false;
  percussion = false;
  nonPercussionProgram = 0;
  jumpActivatedByMainCpu = true; // it should be false in the actual driver, but for convenience
  condBranches.clear();
  condJumpTargetValue = 0;

  ignoreMasterVolumeProgNum = 0xff;
}


SeqTrack::State AkaoSnesTrack::readEvent() {
  AkaoSnesSeq *parentSeq = static_cast<AkaoSnesSeq*>(this->parentSeq);

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return State::Finished;
  }

  uint8_t statusByte = readByte(curOffset++);

  std::string desc;

  AkaoSnesSeqEventType eventType = static_cast<AkaoSnesSeqEventType>(0);
  std::map<uint8_t, AkaoSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc = describeUnknownEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      uint8_t arg4 = readByte(curOffset++);
      desc = describeUnknownEvent(statusByte, arg1, arg2, arg3, arg4);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_NOTE: // 0x00..0xc3
    {
      uint8_t durIndex = statusByte % parentSeq->NOTE_DUR_TABLE.size();
      uint8_t noteIndex = static_cast<uint8_t>(statusByte / parentSeq->NOTE_DUR_TABLE.size());
      uint8_t len = parentSeq->NOTE_DUR_TABLE[durIndex];

      if (onetimeDuration != 0) {
        len = onetimeDuration;
        onetimeDuration = 0;
      }

      uint8_t dur = len;
      if (!slur && !legato) {
        dur = (dur > 2) ? dur - 2 : 1;
      }

      if (noteIndex < 12) {
        uint8_t note = octave * 12 + noteIndex;

        if (percussion) {
          addNoteByDur(beginOffset, curOffset - beginOffset, noteIndex + AkaoSnesDrumKitRgn::KEY_BIAS - transpose, NOTE_VELOCITY, dur, "Percussion Note with Duration");
        }
        else {
          addNoteByDur(beginOffset, curOffset - beginOffset, note, NOTE_VELOCITY, dur);
        }

        addTime(len);
      }
      else if (noteIndex == parentSeq->STATUS_NOTEINDEX_TIE) {
        makePrevDurNoteEnd(getTime() + dur);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc, Type::Tie);
        addTime(len);
      }
      else {
        addRest(beginOffset, curOffset - beginOffset, len);
      }

      break;
    }

    case EVENT_NOP: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc, Type::Misc);
      break;
    }

    case EVENT_NOP1: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc, Type::Misc);
      break;
    }

    case EVENT_VOLUME: {
      // TODO: correct volume bitdepth?
      uint8_t vol = readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, vol >> 1);
      break;
    }

    case EVENT_VOLUME_FADE: {
      // TODO: correct volume bitdepth?
      uint16_t fadeLength;
      if (parentSeq->version == AKAOSNES_V1) {
        fadeLength = readShort(curOffset);
        curOffset += 2;
      }
      else {
        fadeLength = readByte(curOffset++);
      }
      uint8_t vol = readByte(curOffset++);

      if (fadeLength != 0) {
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Volume Fade",
                        fmt::format("Fade Length: {}  Volume: {}", fadeLength, vol),
                        Type::VolumeSlide);
      }
      else {
        addVol(beginOffset, curOffset - beginOffset, vol >> 1);
      }
      break;
    }

    case EVENT_PAN: {
      uint8_t pan = readByte(curOffset++) << (parentSeq->PAN_8BIT ? 0 : 1);

      uint8_t midiPan = convertLinearPercentPanValToStdMidiVal(pan / 255.0);

      // TODO: apply volume scale
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      break;
    }

    case EVENT_PAN_FADE: {
      uint16_t fadeLength;
      if (parentSeq->version == AKAOSNES_V1) {
        fadeLength = readShort(curOffset);
        curOffset += 2;
      }
      else {
        fadeLength = readByte(curOffset++);
      }
      uint8_t pan = readByte(curOffset++) << (parentSeq->PAN_8BIT ? 0 : 1);

      uint8_t midiPan = convertLinearPercentPanValToStdMidiVal(pan / 255.0);

      // TODO: apply volume scale
      if (fadeLength != 0) {
        desc = fmt::format("Fade Length: {}  Pan: {}", fadeLength, pan);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Pan Fade", desc, Type::PanSlide);
      }
      else {
        addPan(beginOffset, curOffset - beginOffset, midiPan);
      }
      break;
    }

    case EVENT_PITCH_SLIDE_ON: {
      int8_t pitchSlideSemitones;
      uint8_t pitchSlideDelay;
      uint8_t pitchSlideLength;

      if (parentSeq->version == AKAOSNES_V1) {
        pitchSlideDelay = readByte(curOffset++);
        pitchSlideLength = readByte(curOffset++);
        pitchSlideSemitones = readByte(curOffset++);
        desc = fmt::format("Delay: {}  Length: {}  Key: {} semitones", pitchSlideDelay,
          pitchSlideLength, pitchSlideSemitones);
      }
      else { // AKAOSNES_V2
        pitchSlideSemitones = readByte(curOffset++);
        pitchSlideDelay = readByte(curOffset++);
        pitchSlideLength = readByte(curOffset++);
        desc = fmt::format("Key: {} semitones  Delay: {}  Length: {}", pitchSlideSemitones,
          pitchSlideDelay, pitchSlideLength);
      }

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Slide On",
                      desc,
                      Type::PitchBendSlide);
      break;
    }

    case EVENT_PITCH_SLIDE_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Slide Off",
                      desc,
                      Type::PitchBendSlide);
      break;
    }

    case EVENT_PITCH_SLIDE: {
      uint8_t pitchSlideLength = readByte(curOffset++);
      int8_t pitchSlideSemitones = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Slide",
                      fmt::format("Length: {}  Key: {} semitones", pitchSlideLength, pitchSlideSemitones),
                      Type::PitchBendSlide);
      break;
    }

    case EVENT_VIBRATO_ON: {
      uint8_t lfoDelay = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      uint8_t lfoDepth = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato",
                      fmt::format("Delay: {}  Rate: {}  Depth: {}", lfoDelay, lfoRate, lfoDepth),
                      Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato Off",
                      desc,
                      Type::Vibrato);
      break;
    }

    case EVENT_TREMOLO_ON: {
      uint8_t lfoDelay = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      uint8_t lfoDepth = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tremolo",
                      fmt::format("Delay {}  Rate: {}  Depth {}", lfoDelay, lfoRate, lfoDepth),
                      Type::Tremelo);
      break;
    }

    case EVENT_TREMOLO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tremolo Off",
                      desc,
                      Type::Tremelo);
      break;
    }

    case EVENT_PAN_LFO_ON: {
      uint8_t lfoDepth = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pan LFO",
                      fmt::format("Depth: {}  Rate: {}", lfoDepth, lfoRate),
                      Type::PanLfo);
      break;
    }

    case EVENT_PAN_LFO_ON_WITH_DELAY: {
      uint8_t lfoDelay = readByte(curOffset++);
      uint8_t lfoRate = readByte(curOffset++);
      uint8_t lfoDepth = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pan LFO",
                      fmt::format("Delay: {}  Rate: {}  Depth: {}", lfoDelay, lfoRate, lfoDepth),
                      Type::PanLfo);
      break;
    }

    case EVENT_PAN_LFO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pan LFO Off",
                      desc,
                      Type::PanLfo);
      break;
    }

    case EVENT_NOISE_FREQ: {
      uint8_t newNCK = readByte(curOffset++) & 0x1f;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Noise Frequency",
                      fmt::format("Noise Frequency (NCK): {}", newNCK),
                      Type::ChangeState);
      break;
    }

    case EVENT_NOISE_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Noise On",
                      desc,
                      Type::ChangeState);
      break;
    }

    case EVENT_NOISE_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Noise Off",
                      desc,
                      Type::ChangeState);
      break;
    }

    case EVENT_PITCHMOD_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Modulation On",
                      desc,
                      Type::ChangeState);
      break;
    }

    case EVENT_PITCHMOD_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Modulation Off",
                      desc,
                      Type::ChangeState);
      break;
    }

    case EVENT_ECHO_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo On", desc, Type::Reverb);
      break;
    }

    case EVENT_ECHO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Off", desc, Type::Reverb);
      break;
    }

    case EVENT_OCTAVE: {
      uint8_t newOctave = readByte(curOffset++);
      addSetOctave(beginOffset, curOffset - beginOffset, newOctave);
      break;
    }

    case EVENT_OCTAVE_UP: {
      addIncrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_OCTAVE_DOWN: {
      addDecrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_TRANSPOSE_ABS: {
      int8_t newTranspose = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, newTranspose);
      break;
    }

    case EVENT_TRANSPOSE_REL: {
      int8_t newTranspose = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, transpose + newTranspose, "Transpose (Relative)");
      break;
    }

    case EVENT_TUNING: {
      uint8_t newTuning = readByte(curOffset++);

      double pitchScale;
      if (newTuning <= 0x7f) {
        pitchScale = 1.0 + (newTuning / 256.0);
      }
      else {
        pitchScale = newTuning / 256.0;
      }

      double semitones = (log(pitchScale) / log(2.0)) * 12.0;
      addFineTuning(beginOffset, curOffset - beginOffset, semitones * 100.0);
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t newProg = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, newProg);
      nonPercussionProgram = newProg;
      break;
    }

    case EVENT_VOLUME_ENVELOPE: {
      uint8_t envelopeIndex = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Volume Envelope",
                      fmt::format("Envelope: {}", envelopeIndex),
                      Type::VolumeEnvelope);
      break;
    }

    case EVENT_GAIN_RELEASE: {
      uint8_t gain = readByte(curOffset++) & 0x1f;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Release Rate (GAIN)",
                      fmt::format("GAIN: {}", gain),
                      Type::Adsr);
      break;
    }

    case EVENT_DURATION_RATE: {
      uint8_t rate = readByte(curOffset++);
      if (rate == 0 || rate == 100) {
        rate = 100;
      }

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Duration Rate",
                      fmt::format("Note Length: {} %", rate),
                      Type::DurationChange);
      break;
    }

    case EVENT_ADSR_AR: {
      uint8_t newAR = readByte(curOffset++) & 15;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "ADSR Attack Rate",
                      fmt::format("AR: {}", newAR),
                      Type::Adsr);
      break;
    }

    case EVENT_ADSR_DR: {
      uint8_t newDR = readByte(curOffset++) & 7;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "ADSR Decay Rate",
                      fmt::format("DR: {}", newDR),
                      Type::Adsr);
      break;
    }

    case EVENT_ADSR_SL: {
      uint8_t newSL = readByte(curOffset++) & 7;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "ADSR Sustain Level",
                      fmt::format("SL: {}", newSL),
                      Type::Adsr);
      break;
    }

    case EVENT_ADSR_SR: {
      uint8_t newSR = readByte(curOffset++) & 15;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "ADSR Sustain Rate",
                      fmt::format("SR: {}", newSR),
                      Type::Adsr);
      break;
    }

    case EVENT_ADSR_DEFAULT: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Default ADSR",
                      desc,
                      Type::Adsr);
      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = readByte(curOffset++);
      if (count != 0) {
        count++;
      }

      desc = fmt::format("Loop Count: {}", count);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", desc, Type::RepeatStart);

      loopStart[loopLevel] = curOffset;
      if (parentSeq->version == AKAOSNES_V4) {
        loopIncCount[loopLevel] = 1;
      }
      else { // AKAOSNES_V1, AKAOSNES_V2, AKAOSNES_V3
        loopIncCount[loopLevel] = 0;
      }
      loopDecCount[loopLevel] = count;
      loopLevel = (loopLevel + 1) % AKAOSNES_LOOP_LEVEL_MAX;
      break;
    }

    case EVENT_LOOP_END: {
      uint8_t prevLoopLevel = (loopLevel != 0 ? loopLevel : AKAOSNES_LOOP_LEVEL_MAX) - 1;
      if (parentSeq->version == AKAOSNES_V4) {
        loopIncCount[prevLoopLevel]++;
      }

      if (loopDecCount[prevLoopLevel] == 0) {
        // infinite loop
        auto newState = addLoopForever(beginOffset, curOffset - beginOffset);
        curOffset = loopStart[prevLoopLevel];
        return newState;
      }
      else {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc, Type::RepeatEnd);

        if (loopDecCount[prevLoopLevel] - 1 == 0) {
          // repeat end
          loopLevel = prevLoopLevel;
        }
        else {
          // repeat again
          loopDecCount[prevLoopLevel]--;
          curOffset = loopStart[prevLoopLevel];
        }
      }

      break;
    }

    case EVENT_LOOP_RESTART: {
      uint8_t prevLoopLevel = (loopLevel != 0 ? loopLevel : AKAOSNES_LOOP_LEVEL_MAX) - 1;

      // reset loop count, but keep the nest level
      loopIncCount[prevLoopLevel] = 0;

      break;
    }

    case EVENT_SLUR_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Slur On (No Key Off/On)",
                      desc,
                      Type::ChangeState);
      slur = true;
      break;
    }

    case EVENT_SLUR_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Slur Off",
                      desc,
                      Type::ChangeState);
      slur = false;
      break;
    }

    case EVENT_LEGATO_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Legato On (No Key Off)",
                      desc,
                      Type::ChangeState);
      legato = true;
      break;
    }

    case EVENT_LEGATO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Legato Off",
                      desc,
                      Type::ChangeState);
      legato = false;
      break;
    }

    case EVENT_ONETIME_DURATION: {
      uint8_t dur = readByte(curOffset++);
      onetimeDuration = dur;
      desc = fmt::format("Duration: {:d}", dur);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration (One-Time)",
        desc, Type::DurationChange);
      break;
    }

    case EVENT_JUMP_TO_SFX_LO: {
      // TODO: EVENT_JUMP_TO_SFX_LO
      uint8_t sfxIndex = readByte(curOffset++);
      desc = fmt::format("SFX: {:d}", sfxIndex);
      addUnknown(beginOffset, curOffset - beginOffset, "Jump to SFX (LOWORD)", desc);
      return State::Finished;
    }

    case EVENT_JUMP_TO_SFX_HI: {
      // TODO: EVENT_JUMP_TO_SFX_HI
      uint8_t sfxIndex = readByte(curOffset++);
      desc = fmt::format("SFX: {:d}", sfxIndex);
      addUnknown(beginOffset, curOffset - beginOffset, "Jump to SFX (HIWORD)", desc);
      return State::Finished;
    }

    case EVENT_PLAY_SFX: {
      // TODO: EVENT_PLAY_SFX
      uint8_t arg1 = readByte(curOffset++);
      desc = fmt::format("Arg1: {:d}", arg1);
      addUnknown(beginOffset, curOffset - beginOffset, "Play SFX", desc);
      break;
    }

    case EVENT_END: {
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      return State::Finished;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = readByte(curOffset++);
      if (parentSeq->minorVersion == AKAOSNES_V4_FM || parentSeq->minorVersion == AKAOSNES_V4_CT) {
        newTempo += (newTempo * 0x14) >> 8;
      }
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM(newTempo));
      break;
    }

    case EVENT_TEMPO_FADE: {
      uint16_t fadeLength;
      if (parentSeq->version == AKAOSNES_V1) {
        fadeLength = readShort(curOffset);
        curOffset += 2;
      }
      else {
        fadeLength = readByte(curOffset++);
      }

      uint8_t newTempo = readByte(curOffset++);
      if (parentSeq->minorVersion == AKAOSNES_V4_FM || parentSeq->minorVersion == AKAOSNES_V4_CT) {
        newTempo += (newTempo * 0x14) >> 8;
      }

      if (fadeLength != 0) {
        desc = fmt::format("Fade Length: {}  BPM: {}", fadeLength, parentSeq->getTempoInBPM(newTempo));
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tempo Fade", desc, Type::Tempo);
      }
      else {
        addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM(newTempo));
      }
      break;
    }

    case EVENT_ECHO_VOLUME: {
      uint8_t vol = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Volume",
                      fmt::format("Volume: {}", vol),
                      Type::Reverb);
      break;
    }

    case EVENT_ECHO_VOLUME_FADE: {
      uint16_t fadeLength = readByte(curOffset++);
      int8_t vol = readByte(curOffset++);

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Volume Fade",
                      fmt::format("Fade Length: {}  Volume {}", fadeLength, vol),
                      Type::Reverb);
      break;
    }

    case EVENT_ECHO_FEEDBACK_FIR: {
      int8_t feedback = readByte(curOffset++);
      uint8_t filterIndex = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Feedback & FIR",
                      fmt::format("Feedback: {},  FIR: {}", feedback, filterIndex),
                      Type::Reverb);
      break;
    }

    case EVENT_MASTER_VOLUME: {
      uint8_t newVol = readByte(curOffset++);
      addMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
      break;
    }

    case EVENT_LOOP_BREAK: {
      uint8_t count = readByte(curOffset++);
      uint16_t dest = getShortAddress(curOffset);
      curOffset += 2;

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Loop Break / Jump to Volta",
                      fmt::format("Count: {}  Destination: ${:04X}", count, dest),
                      Type::RepeatEnd);

      uint8_t prevLoopLevel = (loopLevel != 0 ? loopLevel : AKAOSNES_LOOP_LEVEL_MAX) - 1;
      if (parentSeq->version != AKAOSNES_V4) { // AKAOSNES_V1, AKAOSNES_V2, AKAOSNES_V3, and Romancing SaGa 2
        // (Romancing SaGa 2 also increments the counter here actually, unlike other V4 games. VGMTrans doesn't try to emulate it at this time.)
        loopIncCount[prevLoopLevel]++;
      }

      // Branch if the incremental loop count matches the specified count.
      if (count == loopIncCount[prevLoopLevel]) {
        // The last iteration will end the loop (i.e. decreases the loop level)
        //
        // Note that V2 and V3 does not do this!
        // If you use an opcode to exit the innermost loop in a nested loop, the loop-level mismatch will break the nested loop.
        // (I don't think the songs officially released by Square use this opcode in a nested loop. That was probably not recommended.)
        if (parentSeq->version == AKAOSNES_V1) {
          if (loopDecCount[prevLoopLevel] != 0) {
            // Only Final Fantasy 4 decreases the remaining count of the most-inner repeat.
            loopDecCount[prevLoopLevel]--;
            if (loopDecCount[prevLoopLevel] == 0) {
              loopLevel = prevLoopLevel;
            }
          }
        } else if (parentSeq->version != AKAOSNES_V2 && parentSeq->version != AKAOSNES_V3) {
          if (loopDecCount[prevLoopLevel] - 1 == 0) {
            loopLevel = prevLoopLevel;
          }
        }

        curOffset = dest;
      }

      break;
    }

    case EVENT_GOTO: {
      uint16_t dest = getShortAddress(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      uint32_t length = curOffset - beginOffset;

      curOffset = dest;
      if (!isOffsetUsed(dest)) {
        addGenericEvent(beginOffset, length, "Jump", desc, Type::LoopForever);
      }
      else {
        return addLoopForever(beginOffset, length, "Jump");
      }
      break;
    }

    case EVENT_INC_CPU_SHARED_COUNTER: {
      addUnknown(beginOffset, curOffset - beginOffset, "Increment CPU-Shared Counter", desc);
      break;
    }

    case EVENT_ZERO_CPU_SHARED_COUNTER: {
      addUnknown(beginOffset, curOffset - beginOffset, "Zero CPU-Shared Counter", desc);
      break;
    }

    case EVENT_ECHO_FEEDBACK_FADE: {
      uint16_t fadeLength = readByte(curOffset++);
      uint8_t feedback = readByte(curOffset++);

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Feedback Fade",
                      fmt::format("Fade Length: {}  Feedback: {}", fadeLength, feedback),
                      Type::Reverb);
      break;
    }

    case EVENT_ECHO_FIR_FADE: {
      uint16_t fadeLength = readByte(curOffset++);
      uint8_t filterIndex = readByte(curOffset++);

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo FIR Fade",
                      fmt::format("Fade Length: {}  FIR: {}", fadeLength, vol),
                      Type::Reverb);
      break;
    }

    case EVENT_ECHO_FEEDBACK: {
      uint8_t feedback = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Feedback",
                      fmt::format("Feedback: {}", feedback),
                      Type::Reverb);
      break;
    }

    case EVENT_ECHO_FIR: {
      uint8_t filterIndex = readByte(curOffset++);
      addGenericEvent(beginOffset,
        curOffset - beginOffset,
        "Echo FIR",
        fmt::format("FIR: {}", vol),
        Type::Reverb);
      break;
    }

    case EVENT_CPU_CONTROLED_SET_VALUE: {
      uint8_t value = readByte(curOffset++);
      addUnknown(beginOffset, curOffset - beginOffset, "Set Value for CPU-Controlled Jump", desc);
      break;
    }

    case EVENT_CPU_CONTROLED_JUMP: {
      uint16_t dest = getShortAddress(curOffset);
      curOffset += 2;
      if (!condBranches.contains(curOffset)) {
        ++condJumpTargetValue;
        condBranches.insert(curOffset);
        // }

        CondBranchEvt info;
        info.absTime     = getTime();   // global tick
        info.srcOffset   = curOffset;
        info.dstOffset   = dest;
        info.expectValue = condJumpTargetValue;
        parentSeq->registerConditionalBranch(info);
      }

      if (readMode == READMODE_ADD_TO_UI) {
        desc = fmt::format("Destination: ${:04X}", dest);
        addGenericEvent(beginOffset, curOffset - beginOffset, "CPU-Controlled Jump", desc,
                        Type::Loop);
        // m_offsetStack.push_back(dest);
      }

      printf("shouldTakeBranch? condJumpTargetValue: %d  readMode: %d  time: %d\n", condJumpTargetValue, readMode, getTime());
      if (shouldTakeBranch(condJumpTargetValue)) {
        printf("Taking branch to: %X\n", dest);
        curOffset = dest;
      }

      // if (jumpActivatedByMainCpu) {
      //   curOffset = dest;
      //
      //   if (parentSeq->version == AKAOSNES_V3 ||
      //       (parentSeq->version == AKAOSNES_V4 && parentSeq->minorVersion == AKAOSNES_V4_FF6))
      //     jumpActivatedByMainCpu = false;
      // }

      break;
    }

    case EVENT_CPU_CONTROLED_JUMP_V2: {
      uint8_t arg1 = readByte(curOffset++) & 15;
      uint16_t dest = getShortAddress(curOffset);
      curOffset += 2;
      const u8 targetValue = arg1;

      CondBranchEvt info;
      info.absTime     = getTime();   // global tick
      info.srcOffset   = curOffset;
      info.dstOffset   = dest;
      info.expectValue = targetValue;
      parentSeq->registerConditionalBranch(info);

      if (readMode == READMODE_ADD_TO_UI) {
        desc = fmt::format("Arg1: {}  Destination: ${:04X}", arg1, dest);
        // desc = fmt::format("Destination: ${:04X}", dest);
        addGenericEvent(beginOffset, curOffset - beginOffset, "CPU-Controlled Jump", desc,
                        Type::Loop);
        // addUnknown(beginOffset, curOffset - beginOffset, "CPU-Controlled Jump", desc);

        // m_offsetStack.push_back(dest);
      }

      if (shouldTakeBranch(targetValue)) {
        curOffset = dest;
      }

      // desc = fmt::format("Arg1: {}  Destination: ${:04X}", arg1, dest);
      // addUnknown(beginOffset, curOffset - beginOffset, "CPU-Controlled Jump", desc);
      break;
    }

    case EVENT_PERC_ON: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Percussion On",
                      desc,
                      Type::ChangeState);
      percussion = true;
      addProgramChangeNoItem(AkaoSnesInstrSet::DRUMKIT_PROGRAM, true);
      break;
    }

    case EVENT_PERC_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Percussion Off",
                      desc,
                      Type::ChangeState);
      percussion = false;
      addProgramChangeNoItem(nonPercussionProgram, true);
      break;
    }

    case EVENT_IGNORE_MASTER_VOLUME_BROKEN: {
      // multiply (1 << channel) / 256.0 to channel volume, instead of applying master volume
      // apparently, it is caused by wrong destination address of conditional branch
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Ignore Master Volume (Broken)",
                      desc,
                      Type::ChangeState);
      break;
    }

    case EVENT_IGNORE_MASTER_VOLUME: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Ignore Master Volume",
                      desc,
                      Type::ChangeState);
      break;
    }

    case EVENT_IGNORE_MASTER_VOLUME_BY_PROGNUM: {
      ignoreMasterVolumeProgNum = readByte(curOffset++);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Ignore Master Volume By Program Number",
                      fmt::format("Program Number: {}", ignoreMasterVolumeProgNum),
                      Type::ChangeState);
      break;
    }

    case EVENT_VOLUME_ALT: {
      uint8_t vol = readByte(curOffset++) & 0x7f;
      addExpression(beginOffset, curOffset - beginOffset, vol);
      break;
    }

    default:
      auto description = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", description);
      return State::Finished;
  }

  //ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return State::Active;
}

uint16_t AkaoSnesTrack::romAddressToApuAddress(uint16_t romAddress) const {
  AkaoSnesSeq *parentSeq = static_cast<AkaoSnesSeq*>(this->parentSeq);
  return parentSeq->romAddressToApuAddress(romAddress);
}

uint16_t AkaoSnesTrack::getShortAddress(uint32_t offset) const {
  return romAddressToApuAddress(readShort(offset));
}
