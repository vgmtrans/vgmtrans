#include "NinSnesSeq.h"
#include "SeqEvent.h"
#include "ScaleConversion.h"
#include "spdlog/fmt/fmt.h"

DECLARE_FORMAT(NinSnes);

//  **********
//  NinSnesSeq
//  **********
#define MAX_TRACKS  8
#define SEQ_PPQN    48
#define SEQ_KEYOFS  24

NinSnesSeq::NinSnesSeq(RawFile *file,
                       NinSnesVersion ver,
                       uint32_t offset,
                       uint8_t percussion_base,
                       const std::vector<uint8_t> &theVolumeTable,
                       const std::vector<uint8_t> &theDurRateTable,
                       std::string theName)
    : VGMMultiSectionSeq(NinSnesFormat::name, file, offset, 0, theName), version(ver),
      header(NULL),
      volumeTable(theVolumeTable),
      durRateTable(theDurRateTable),
      spcPercussionBaseInit(percussion_base),
      konamiBaseAddress(0),
      intelliVoiceParamTable(0),
      intelliVoiceParamTableSize(0),
      quintetBGMInstrBase(0),
      falcomBaseOffset(0) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

NinSnesSeq::~NinSnesSeq() {
}

void NinSnesSeq::resetVars() {
  VGMMultiSectionSeq::resetVars();

  spcPercussionBase = spcPercussionBaseInit;
  sectionRepeatCount = 0;

  // Intelligent Systems:
  intelliUseCustomNoteParam = false;
  intelliUseCustomPercTable = false;
  intelliVoiceParamTable = 0;
  intelliVoiceParamTableSize = 0;

  for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    sharedTrackData[trackIndex].resetVars();
  }
}

bool NinSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);
  nNumTracks = MAX_TRACKS;

  if (dwStartOffset + 2 > 0x10000) {
    return false;
  }

  // validate first section
  uint16_t firstSectionPtr = dwStartOffset;
  uint16_t addrFirstSection = readShort(firstSectionPtr);
  if (addrFirstSection + 16 > 0x10000) {
    return false;
  }

  if (addrFirstSection >= 0x0100) {
    addrFirstSection = convertToAPUAddress(addrFirstSection);

    uint8_t numActiveTracks = 0;
    for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
      uint16_t addrTrackStart = readShort(addrFirstSection + trackIndex * 2);
      if (addrTrackStart != 0) {
        addrTrackStart = convertToAPUAddress(addrTrackStart);

        if (addrTrackStart < addrFirstSection) {
          return false;
        }
        numActiveTracks++;
      }
    }
    if (numActiveTracks == 0) {
      return false;
    }
  }

  // events will be added later, see ReadEvent
  header = addHeader(dwStartOffset, 0);
  return true;
}

bool NinSnesSeq::readEvent(long stopTime) {
  uint32_t beginOffset = curOffset;
  if (curOffset + 1 >= 0x10000) {
    return false;
  }

  if (curOffset < header->dwOffset) {
    uint32_t distance = header->dwOffset - curOffset;
    header->dwOffset = curOffset;
    if (header->unLength != 0) {
      header->unLength += distance;
    }
  }

  uint16_t sectionAddress = readShort(curOffset);
  curOffset += 2;
  bool bContinue = true;

  if (sectionAddress == 0) {
    // End
    if (!isOffsetUsed(beginOffset)) {
      header->addChild(beginOffset, curOffset - beginOffset, "Section Playlist End");
    }
    bContinue = false;
  }
  else if (sectionAddress <= 0xff) {
    // Jump
    if (curOffset + 1 >= 0x10000) {
      return false;
    }

    uint16_t repeatCount = sectionAddress;
    uint16_t dest = getShortAddress(curOffset);
    curOffset += 2;

    bool startNewRepeat = false;
    bool doJump = false;
    bool infiniteLoop = false;

    if (version == NINSNES_TOSE) {
      if (sectionRepeatCount == 0) {
        // set new repeat count
        sectionRepeatCount = (uint8_t) repeatCount;
        startNewRepeat = true;
        doJump = true;
      }
      else {
        // infinite loop?
        if (sectionRepeatCount == 0xff) {
          if (isOffsetUsed(dest)) {
            infiniteLoop = true;
          }
          doJump = true;
        }
        else {
          // decrease 8-bit counter
          sectionRepeatCount--;

          // jump if the counter is zero
          if (sectionRepeatCount != 0) {
            doJump = true;
          }
        }
      }
    }
    else {
      // decrease 8-bit counter
      sectionRepeatCount--;
      // check overflow (end of loop)
      if (sectionRepeatCount >= 0x80) {
        // set new repeat count
        sectionRepeatCount = (uint8_t) repeatCount;
        startNewRepeat = true;
      }

      // jump if the counter is zero
      if (sectionRepeatCount != 0) {
        doJump = true;
        if (isOffsetUsed(dest) && sectionRepeatCount > 0x80) {
          infiniteLoop = true;
        }
      }
    }

    // add event to sequence
    if (!isOffsetUsed(beginOffset)) {
      header->addChild(beginOffset, curOffset - beginOffset, "Playlist Jump");

      // add the last event too, if available
      if (curOffset + 1 < 0x10000 && readShort(curOffset) == 0x0000) {
        header->addChild(curOffset, 2, "Playlist End");
      }
    }

    if (infiniteLoop) {
      bContinue = addLoopForeverNoItem();
    }

    // do actual jump, at last
    if (doJump) {
      curOffset = dest;

      if (curOffset < dwOffset) {
        uint32_t distance = dwOffset - curOffset;
        dwOffset = curOffset;
        if (unLength != 0) {
          unLength += distance;
        }
      }
    }
  }
  else {
    sectionAddress = convertToAPUAddress(sectionAddress);

    // Play the section
    if (!isOffsetUsed(beginOffset)) {
      header->addChild(beginOffset, curOffset - beginOffset, "Section Pointer");
    }

    NinSnesSection *section = (NinSnesSection *) getSectionAtOffset(sectionAddress);
    if (section == NULL) {
      section = new NinSnesSection(this, sectionAddress);
      if (!section->load()) {
        L_ERROR("Failed to load section");
        return false;
      }
      addSection(section);
    }

    if (!loadSection(section, stopTime)) {
      bContinue = false;
    }

    if (version == NINSNES_UNKNOWN) {
      bContinue = false;
    }
  }

  return bContinue;
}

void NinSnesSeq::loadEventMap() {
  int statusByte;

  if (version == NINSNES_UNKNOWN) {
    return;
  }

  const uint8_t NINSNES_VOL_TABLE_EARLIER[16] = {
      0x08, 0x12, 0x1b, 0x24, 0x2c, 0x35, 0x3e, 0x47,
      0x51, 0x5a, 0x62, 0x6b, 0x7d, 0x8f, 0xa1, 0xb3,
  };

  const uint8_t NINSNES_DUR_TABLE_EARLIER[8] = {
      0x33, 0x66, 0x80, 0x99, 0xb3, 0xcc, 0xe6, 0xff,
  };

  const uint8_t NINSNES_PAN_TABLE_EARLIER[21] = {
      0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
      0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
      0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
  };

  const uint8_t NINSNES_VOL_TABLE_STANDARD[16] = {
      0x19, 0x33, 0x4c, 0x66, 0x72, 0x7f, 0x8c, 0x99,
      0xa5, 0xb2, 0xbf, 0xcc, 0xd8, 0xe5, 0xf2, 0xfc,
  };

  const uint8_t NINSNES_DUR_TABLE_STANDARD[8] = {
      0x33, 0x66, 0x7f, 0x99, 0xb2, 0xcc, 0xe5, 0xfc,
  };

  const uint8_t NINSNES_PAN_TABLE_STANDARD[21] = {
      0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
      0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
      0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
  };

  const uint8_t NINSNES_VOL_TABLE_INTELLI[16] = {
      0x19, 0x32, 0x4c, 0x65, 0x72, 0x7f, 0x8c, 0x98,
      0xa5, 0xb2, 0xbf, 0xcb, 0xd8, 0xe5, 0xf2, 0xfc,
  };

  const uint8_t NINSNES_DUR_TABLE_INTELLI[8] = {
      0x32, 0x65, 0x7f, 0x98, 0xb2, 0xcb, 0xe5, 0xfc,
  };

  const uint8_t NINSNES_DURVOL_TABLE_INTELLI_FE3[64] = {
      0x00, 0x0c, 0x19, 0x26, 0x33, 0x3f, 0x4c, 0x59,
      0x66, 0x72, 0x75, 0x77, 0x70, 0x7c, 0x7f, 0x82,
      0x84, 0x87, 0x89, 0x8c, 0x8e, 0x91, 0x93, 0x96,
      0x99, 0x9b, 0x9e, 0xa0, 0xa3, 0xa5, 0xa8, 0xaa,
      0xad, 0xaf, 0xb2, 0xb5, 0xb7, 0xba, 0xbc, 0xbf,
      0xc1, 0xc4, 0xc6, 0xc9, 0xcc, 0xce, 0xd1, 0xd3,
      0xd6, 0xd8, 0xdb, 0xdd, 0xe0, 0xe2, 0xe5, 0xe8,
      0xea, 0xed, 0xef, 0xf2, 0xf4, 0xf7, 0xf9, 0xfc,
  };

  const uint8_t NINSNES_DURVOL_TABLE_INTELLI_FE4[64] = {
      0x19, 0x26, 0x33, 0x3f, 0x4c, 0x59, 0x66, 0x6d,
      0x70, 0x72, 0x75, 0x77, 0x70, 0x7c, 0x7f, 0x82,
      0x84, 0x87, 0x89, 0x8c, 0x8e, 0x91, 0x93, 0x96,
      0x99, 0x9b, 0x9e, 0xa0, 0xa3, 0xa5, 0xa8, 0xaa,
      0xad, 0xaf, 0xb2, 0xb5, 0xb7, 0xba, 0xbc, 0xbf,
      0xc1, 0xc4, 0xc6, 0xc9, 0xcc, 0xce, 0xd1, 0xd3,
      0xd6, 0xd8, 0xdb, 0xdd, 0xe0, 0xe2, 0xe5, 0xe8,
      0xea, 0xed, 0xef, 0xf2, 0xf4, 0xf7, 0xf9, 0xfc,
  };

  switch (version) {
    case NINSNES_EARLIER:
      STATUS_END = 0x00;
      STATUS_NOTE_MIN = 0x80;
      STATUS_NOTE_MAX = 0xc5;
      STATUS_PERCUSSION_NOTE_MIN = 0xd0;
      STATUS_PERCUSSION_NOTE_MAX = 0xd9;
      break;

    default:
      STATUS_END = 0x00;
      STATUS_NOTE_MIN = 0x80;
      STATUS_NOTE_MAX = 0xc7;
      STATUS_PERCUSSION_NOTE_MIN = 0xca;
      STATUS_PERCUSSION_NOTE_MAX = 0xdf;
  }

  EventMap[0x00] = EVENT_END;

  for (statusByte = 0x01; statusByte < STATUS_NOTE_MIN; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE_PARAM;
  }

  for (statusByte = STATUS_NOTE_MIN; statusByte <= STATUS_NOTE_MAX; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  EventMap[STATUS_NOTE_MAX + 1] = EVENT_TIE;
  EventMap[STATUS_NOTE_MAX + 2] = EVENT_REST;

  for (statusByte = STATUS_PERCUSSION_NOTE_MIN; statusByte <= STATUS_PERCUSSION_NOTE_MAX; statusByte++) {
    EventMap[statusByte] = EVENT_PERCUSSION_NOTE;
  }

  switch (version) {
    case NINSNES_EARLIER:
      EventMap[0xda] = EVENT_PROGCHANGE;
      EventMap[0xdb] = EVENT_PAN;
      EventMap[0xdc] = EVENT_PAN_FADE;
      EventMap[0xdd] = EVENT_PITCH_SLIDE;
      EventMap[0xde] = EVENT_VIBRATO_ON;
      EventMap[0xdf] = EVENT_VIBRATO_OFF;
      EventMap[0xe0] = EVENT_MASTER_VOLUME;
      EventMap[0xe1] = EVENT_MASTER_VOLUME_FADE;
      EventMap[0xe2] = EVENT_TEMPO;
      EventMap[0xe3] = EVENT_TEMPO_FADE;
      EventMap[0xe4] = EVENT_GLOBAL_TRANSPOSE;
      EventMap[0xe5] = EVENT_TREMOLO_ON;
      EventMap[0xe6] = EVENT_TREMOLO_OFF;
      EventMap[0xe7] = EVENT_VOLUME;
      EventMap[0xe8] = EVENT_VOLUME_FADE;
      EventMap[0xe9] = EVENT_CALL;
      EventMap[0xea] = EVENT_VIBRATO_FADE;
      EventMap[0xeb] = EVENT_PITCH_ENVELOPE_TO;
      EventMap[0xec] = EVENT_PITCH_ENVELOPE_FROM;
      //EventMap[0xed] = EVENT_PITCH_ENVELOPE_OFF;
      EventMap[0xee] = EVENT_TUNING;
      EventMap[0xef] = EVENT_ECHO_ON;
      EventMap[0xf0] = EVENT_ECHO_OFF;
      EventMap[0xf1] = EVENT_ECHO_PARAM;
      EventMap[0xf2] = EVENT_ECHO_VOLUME_FADE;

      if (volumeTable.empty()) {
        volumeTable.assign(std::begin(NINSNES_VOL_TABLE_EARLIER), std::end(NINSNES_VOL_TABLE_EARLIER));
      }

      if (durRateTable.empty()) {
        durRateTable.assign(std::begin(NINSNES_DUR_TABLE_EARLIER), std::end(NINSNES_DUR_TABLE_EARLIER));
      }

      if (panTable.empty()) {
        panTable.assign(std::begin(NINSNES_PAN_TABLE_EARLIER), std::end(NINSNES_PAN_TABLE_EARLIER));
      }

      break;

    case NINSNES_INTELLI_FE3:
      for (statusByte = 0x01; statusByte < STATUS_NOTE_MIN; statusByte++) {
        EventMap[statusByte] = EVENT_INTELLI_NOTE_PARAM;
      }

      // standard vcmds
      loadStandardVcmdMap(0xd6);

      EventMap[0xf1] = EVENT_INTELLI_ECHO_ON;
      EventMap[0xf2] = EVENT_INTELLI_ECHO_OFF;
      EventMap[0xf3] = EVENT_INTELLI_LEGATO_ON;
      EventMap[0xf4] = EVENT_INTELLI_LEGATO_OFF;
      EventMap[0xf5] = EVENT_INTELLI_FE3_EVENT_F5;
      EventMap[0xf6] = EVENT_INTELLI_WRITE_APU_PORT;
      EventMap[0xf7] = EVENT_INTELLI_JUMP_SHORT_CONDITIONAL;
      EventMap[0xf8] = EVENT_INTELLI_JUMP_SHORT;
      EventMap[0xf9] = EVENT_INTELLI_FE3_EVENT_F9;
      EventMap[0xfa] = EVENT_INTELLI_DEFINE_VOICE_PARAM;
      EventMap[0xfb] = EVENT_INTELLI_LOAD_VOICE_PARAM;
      EventMap[0xfc] = EVENT_INTELLI_ADSR;
      EventMap[0xfd] = EVENT_INTELLI_GAIN_SUSTAIN_TIME_AND_RATE;

      if (volumeTable.empty()) {
        volumeTable.assign(std::begin(NINSNES_VOL_TABLE_INTELLI), std::end(NINSNES_VOL_TABLE_INTELLI));
      }

      if (durRateTable.empty()) {
        durRateTable.assign(std::begin(NINSNES_DUR_TABLE_INTELLI), std::end(NINSNES_DUR_TABLE_INTELLI));
      }

      if (intelliDurVolTable.empty()) {
        intelliDurVolTable.assign(std::begin(NINSNES_DURVOL_TABLE_INTELLI_FE3),
                                  std::end(NINSNES_DURVOL_TABLE_INTELLI_FE3));
      }

      if (panTable.empty()) {
        panTable.assign(std::begin(NINSNES_PAN_TABLE_STANDARD), std::end(NINSNES_PAN_TABLE_STANDARD));
      }

      break;

    case NINSNES_INTELLI_TA:
      // standard vcmds
      loadStandardVcmdMap(0xda);

      EventMap[0xf5] = EVENT_INTELLI_ECHO_ON;
      EventMap[0xf6] = EVENT_INTELLI_ECHO_OFF;
      EventMap[0xf7] = EVENT_INTELLI_ADSR;
      EventMap[0xf8] = EVENT_INTELLI_GAIN_SUSTAIN_TIME_AND_RATE;
      EventMap[0xf9] = EVENT_INTELLI_GAIN_SUSTAIN_TIME;
      EventMap[0xfa] = EVENT_INTELLI_DEFINE_VOICE_PARAM;
      EventMap[0xfb] = EVENT_INTELLI_LOAD_VOICE_PARAM;
      EventMap[0xfc] = EVENT_INTELLI_FE4_EVENT_FC;
      EventMap[0xfd] = EVENT_INTELLI_TA_SUBEVENT;

      if (volumeTable.empty()) {
        volumeTable.assign(std::begin(NINSNES_VOL_TABLE_INTELLI), std::end(NINSNES_VOL_TABLE_INTELLI));
      }

      if (durRateTable.empty()) {
        durRateTable.assign(std::begin(NINSNES_DUR_TABLE_INTELLI), std::end(NINSNES_DUR_TABLE_INTELLI));
      }

      if (panTable.empty()) {
        panTable.assign(std::begin(NINSNES_PAN_TABLE_STANDARD), std::end(NINSNES_PAN_TABLE_STANDARD));
      }

      break;

    case NINSNES_INTELLI_FE4:
      for (statusByte = 0x01; statusByte < STATUS_NOTE_MIN; statusByte++) {
        EventMap[statusByte] = EVENT_INTELLI_NOTE_PARAM;
      }

      // standard vcmds
      loadStandardVcmdMap(0xda);

      EventMap[0xf5] = EVENT_INTELLI_ECHO_ON;
      EventMap[0xf6] = EVENT_INTELLI_ECHO_OFF;
      EventMap[0xf7] = EVENT_INTELLI_GAIN;
      EventMap[0xf8] = EventMap[0xf7];
      EventMap[0xf9] = EVENT_UNKNOWN0;
      EventMap[0xfa] = EVENT_INTELLI_DEFINE_VOICE_PARAM;
      EventMap[0xfb] = EVENT_INTELLI_LOAD_VOICE_PARAM;
      EventMap[0xfc] = EVENT_INTELLI_FE4_EVENT_FC;
      EventMap[0xfd] = EVENT_INTELLI_FE4_SUBEVENT;

      if (intelliDurVolTable.empty()) {
        intelliDurVolTable.assign(std::begin(NINSNES_DURVOL_TABLE_INTELLI_FE4),
                                  std::end(NINSNES_DURVOL_TABLE_INTELLI_FE4));
      }

      if (panTable.empty()) {
        panTable.assign(std::begin(NINSNES_PAN_TABLE_STANDARD), std::end(NINSNES_PAN_TABLE_STANDARD));
      }

      break;

    default: // NINSNES_STANDARD compatible versions
      loadStandardVcmdMap(0xe0);

      if (volumeTable.empty()) {
        volumeTable.assign(std::begin(NINSNES_VOL_TABLE_STANDARD), std::end(NINSNES_VOL_TABLE_STANDARD));
      }

      if (durRateTable.empty()) {
        durRateTable.assign(std::begin(NINSNES_DUR_TABLE_STANDARD), std::end(NINSNES_DUR_TABLE_STANDARD));
      }

      if (panTable.empty()) {
        panTable.assign(std::begin(NINSNES_PAN_TABLE_STANDARD), std::end(NINSNES_PAN_TABLE_STANDARD));
      }
  }

  // Modify mapping for derived versions
  switch (version) {
    case NINSNES_RD1:
      EventMap[0xfb] = EVENT_UNKNOWN2;
      EventMap[0xfc] = EVENT_UNKNOWN0;
      EventMap[0xfd] = EVENT_UNKNOWN0;
      EventMap[0xfe] = EVENT_UNKNOWN0;
      break;

    case NINSNES_RD2:
      EventMap[0xfb] = EVENT_RD2_PROGCHANGE_AND_ADSR;
      EventMap[0xfd] = EVENT_PROGCHANGE; // duplicated
      break;

    case NINSNES_HAL:
      EventMap[0xfa] = EVENT_NOP1;
      break;

    case NINSNES_KONAMI:
      EventMap[0xe4] = EVENT_UNKNOWN2;
      EventMap[0xe5] = EVENT_KONAMI_LOOP_START;
      EventMap[0xe6] = EVENT_KONAMI_LOOP_END;
      EventMap[0xe8] = EVENT_NOP;
      EventMap[0xe9] = EVENT_NOP;
      EventMap[0xf5] = EVENT_UNKNOWN0;
      EventMap[0xf6] = EVENT_UNKNOWN0;
      EventMap[0xf7] = EVENT_UNKNOWN0;
      EventMap[0xf8] = EVENT_UNKNOWN0;
      EventMap[0xfb] = EVENT_KONAMI_ADSR_AND_GAIN;
      EventMap[0xfc] = EVENT_NOP;
      EventMap[0xfd] = EVENT_NOP;
      EventMap[0xfe] = EVENT_NOP;
      break;

    case NINSNES_LEMMINGS:
      for (statusByte = 0x01; statusByte < STATUS_NOTE_MIN; statusByte++) {
        EventMap[statusByte] = EVENT_LEMMINGS_NOTE_PARAM;
      }

      EventMap[0xe5] = EVENT_UNKNOWN1; // master volume NYI?
      EventMap[0xe6] = EVENT_UNKNOWN2; // master volume fade?
      EventMap[0xfb] = EVENT_NOP1;
      EventMap[0xfc] = EVENT_UNKNOWN0;
      EventMap[0xfd] = EVENT_UNKNOWN0;
      EventMap[0xfe] = EVENT_UNKNOWN0;

      volumeTable.clear();
      durRateTable.clear();
      break;

    case NINSNES_HUMAN:
      break;

    case NINSNES_TOSE:
      panTable.clear();
      break;

    case NINSNES_QUINTET_IOG:
    case NINSNES_QUINTET_TS:
      EventMap[0xf4] = EVENT_QUINTET_TUNING;
      EventMap[0xff] = EVENT_QUINTET_ADSR;
      break;

    default:
      break;
  }
}

void NinSnesSeq::loadStandardVcmdMap(uint8_t statusByte) {
  EventMap[statusByte + 0x00] = EVENT_PROGCHANGE;
  EventMap[statusByte + 0x01] = EVENT_PAN;
  EventMap[statusByte + 0x02] = EVENT_PAN_FADE;
  EventMap[statusByte + 0x03] = EVENT_VIBRATO_ON;
  EventMap[statusByte + 0x04] = EVENT_VIBRATO_OFF;
  EventMap[statusByte + 0x05] = EVENT_MASTER_VOLUME;
  EventMap[statusByte + 0x06] = EVENT_MASTER_VOLUME_FADE;
  EventMap[statusByte + 0x07] = EVENT_TEMPO;
  EventMap[statusByte + 0x08] = EVENT_TEMPO_FADE;
  EventMap[statusByte + 0x09] = EVENT_GLOBAL_TRANSPOSE;
  EventMap[statusByte + 0x0a] = EVENT_TRANSPOSE;
  EventMap[statusByte + 0x0b] = EVENT_TREMOLO_ON;
  EventMap[statusByte + 0x0c] = EVENT_TREMOLO_OFF;
  EventMap[statusByte + 0x0d] = EVENT_VOLUME;
  EventMap[statusByte + 0x0e] = EVENT_VOLUME_FADE;
  EventMap[statusByte + 0x0f] = EVENT_CALL;
  EventMap[statusByte + 0x10] = EVENT_VIBRATO_FADE;
  EventMap[statusByte + 0x11] = EVENT_PITCH_ENVELOPE_TO;
  EventMap[statusByte + 0x12] = EVENT_PITCH_ENVELOPE_FROM;
  EventMap[statusByte + 0x13] = EVENT_PITCH_ENVELOPE_OFF;
  EventMap[statusByte + 0x14] = EVENT_TUNING;
  EventMap[statusByte + 0x15] = EVENT_ECHO_ON;
  EventMap[statusByte + 0x16] = EVENT_ECHO_OFF;
  EventMap[statusByte + 0x17] = EVENT_ECHO_PARAM;
  EventMap[statusByte + 0x18] = EVENT_ECHO_VOLUME_FADE;
  EventMap[statusByte + 0x19] = EVENT_PITCH_SLIDE;
  EventMap[statusByte + 0x1a] = EVENT_PERCCUSION_PATCH_BASE;
}

double NinSnesSeq::getTempoInBPM(uint8_t tempo) {
  if (tempo != 0) {
    return (double) 60000000 / (SEQ_PPQN * 2000) * ((double) tempo / 256);
  }
  else {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}

uint16_t NinSnesSeq::convertToAPUAddress(uint16_t offset) {
  if (version == NINSNES_KONAMI) {
    return konamiBaseAddress + offset;
  }
  else if (version == NINSNES_FALCOM_YS4) {
    return falcomBaseOffset + offset;
  }
  else {
    return offset;
  }
}

uint16_t NinSnesSeq::getShortAddress(uint32_t offset) {
  return convertToAPUAddress(readShort(offset));
}

//  **************
//  NinSnesSection
//  **************

NinSnesSection::NinSnesSection(NinSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : VGMSeqSection(parentFile, offset, length) {
}

bool NinSnesSection::parseTrackPointers() {
  NinSnesSeq *parentSeq = (NinSnesSeq *) this->parentSeq;
  uint32_t curOffset = dwOffset;

  VGMHeader *header = addHeader(curOffset, 16);
  uint8_t numActiveTracks = 0;
  for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    if (curOffset + 1 >= 0x10000) {
      return false;
    }

    uint16_t startAddress = readShort(curOffset);

    bool active = ((startAddress & 0xff00) != 0);
    NinSnesTrack *track;
    if (active) {
      startAddress = convertToApuAddress(startAddress);

      // correct sequence address
      // probably it's not necessary for regular case, but just in case...
      if (startAddress < dwOffset) {
        uint32_t distance = dwOffset - startAddress;
        dwOffset = startAddress;
        if (unLength != 0) {
          unLength += distance;
        }
      }

      auto trackName = fmt::format("Track {}", trackIndex + 1);
      track = new NinSnesTrack(this, startAddress, 0, trackName);

      numActiveTracks++;
    }
    else {
      // add an inactive track
      track = new NinSnesTrack(this, curOffset, 2, "NULL");
      track->available = false;
    }
    track->shared = &parentSeq->sharedTrackData[trackIndex];
    aTracks.push_back(track);

    char name[32];
    snprintf(name, 32, "Track Pointer #%d", trackIndex + 1);

    header->addChild(curOffset, 2, name);
    curOffset += 2;
  }

  if (numActiveTracks == 0) {
    return false;
  }

  return true;
}

uint16_t NinSnesSection::convertToApuAddress(uint16_t offset) {
  NinSnesSeq *parentSeq = (NinSnesSeq *) this->parentSeq;
  return parentSeq->convertToAPUAddress(offset);
}

uint16_t NinSnesSection::getShortAddress(uint32_t offset) {
  NinSnesSeq *parentSeq = (NinSnesSeq *) this->parentSeq;
  return parentSeq->getShortAddress(offset);
}

NinSnesTrackSharedData::NinSnesTrackSharedData() {
  resetVars();
}

void NinSnesTrackSharedData::resetVars(void) {
  loopCount = 0;
  spcTranspose = 0;

  // just in case
  spcNoteDuration = 1;
  spcNoteDurRate = 0xfc;
  spcNoteVolume = 0xfc;

  // Konami:
  konamiLoopStart = 0;
  konamiLoopCount = 0;
}

//  ************
//  NinSnesTrack
//  ************

NinSnesTrack::NinSnesTrack(NinSnesSection *parentSection, uint32_t offset, uint32_t length, const std::string &theName)
    : SeqTrack(parentSection->parentSeq, offset, length, theName),
      parentSection(parentSection),
      shared(NULL),
      available(true) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
}

void NinSnesTrack::resetVars(void) {
  SeqTrack::resetVars();

  cKeyCorrection = SEQ_KEYOFS;
  if (shared != NULL) {
    transpose = shared->spcTranspose;
  }
}

bool NinSnesTrack::readEvent(void) {
  if (!available) {
    return false;
  }

  NinSnesSeq *parentSeq = (NinSnesSeq *) this->parentSeq;
  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::stringstream desc;

  NinSnesSeqEventType eventType = (NinSnesSeqEventType) 0;
  std::map<uint8_t, NinSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0: {
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      uint8_t arg4 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3
          << "  Arg4: " << (int) arg4;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_NOP: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc.str().c_str(), Type::Nop);
      break;
    }

    case EVENT_NOP1: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc.str().c_str(), Type::Nop);
      break;
    }

    case EVENT_END: {
      // AddEvent is called at the last of this function

      if (shared->loopCount == 0) {
        // finish this section as soon as possible
        if (readMode == READMODE_FIND_DELTA_LENGTH) {
          for (size_t trackIndex = 0; trackIndex < parentSeq->aTracks.size(); trackIndex++) {
            parentSeq->aTracks[trackIndex]->totalTicks = getTime();
          }
        }
        else if (readMode == READMODE_CONVERT_TO_MIDI) {
          // TODO: cancel all expected notes and fader-output events
          for (size_t trackIndex = 0; trackIndex < parentSeq->aTracks.size(); trackIndex++) {
            parentSeq->aTracks[trackIndex]->limitPrevDurNoteEnd();
          }
        }

        parentSeq->deactivateAllTracks();
        bContinue = false;
        parentSeq->bIncTickAfterProcessingTracks = false;
      }
      else {
        uint32_t eventLength = curOffset - beginOffset;

        shared->loopCount--;
        if (shared->loopCount == 0) {
          // repeat end
          curOffset = shared->loopReturnAddress;
        }
        else {
          // repeat again
          curOffset = shared->loopStartAddress;
        }
      }
      break;
    }

    case EVENT_NOTE_PARAM: {
      OnEventNoteParam:
      // param #0: duration
      shared->spcNoteDuration = statusByte;
      desc << "Duration: " << (int) shared->spcNoteDuration;

      // param #1: quantize and velocity (optional)
      if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
        uint8_t quantizeAndVelocity = readByte(curOffset++);

        uint8_t durIndex = (quantizeAndVelocity >> 4) & 7;
        uint8_t velIndex = quantizeAndVelocity & 15;

        shared->spcNoteDurRate = parentSeq->durRateTable[durIndex];
        shared->spcNoteVolume = parentSeq->volumeTable[velIndex];

        desc << "  Quantize: " << (int) durIndex << " (" << (int) shared->spcNoteDurRate << "/256)"
            << "  Velocity: " << (int) velIndex << " (" << (int) shared->spcNoteVolume << "/256)";
      }

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Note Param",
                      desc.str().c_str(),
                      Type::DurationChange);
      break;
    }

    case EVENT_NOTE: {
      uint8_t noteNumber = statusByte - parentSeq->STATUS_NOTE_MIN;
      uint8_t duration = (shared->spcNoteDuration * shared->spcNoteDurRate) >> 8;
      duration = std::min(std::max(duration, (uint8_t) 1), (uint8_t) (shared->spcNoteDuration - 2));

      // Note: Konami engine can have volume=0
      addNoteByDur(beginOffset, curOffset - beginOffset, noteNumber, shared->spcNoteVolume / 2, duration, "Note");
      addTime(shared->spcNoteDuration);
      break;
    }

    case EVENT_TIE: {
      uint8_t duration = (shared->spcNoteDuration * shared->spcNoteDurRate) >> 8;
      duration = std::min(std::max(duration, (uint8_t) 1), (uint8_t) (shared->spcNoteDuration - 2));
      desc << "Duration: " << (int) duration;
      makePrevDurNoteEnd(getTime() + duration);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc.str().c_str(), Type::Tie);
      addTime(shared->spcNoteDuration);
      break;
    }

    case EVENT_REST: {
      addRest(beginOffset, curOffset - beginOffset, shared->spcNoteDuration);
      break;
    }

    case EVENT_PERCUSSION_NOTE: {
      uint8_t noteNumber = statusByte - parentSeq->STATUS_PERCUSSION_NOTE_MIN;

      noteNumber += parentSeq->spcPercussionBase;

      if (parentSeq->version == NINSNES_QUINTET_ACTR) {
        noteNumber += parentSeq->quintetBGMInstrBase;
      }
      else if (NinSnesFormat::isQuintetVersion(parentSeq->version)) {
        noteNumber = readByte(parentSeq->quintetAddrBGMInstrLookup + noteNumber);
      }

      uint8_t duration = (shared->spcNoteDuration * shared->spcNoteDurRate) >> 8;
      duration = std::min(std::max(duration, (uint8_t) 1), (uint8_t) (shared->spcNoteDuration - 2));

      // Note: Konami engine can have volume=0
      addPercNoteByDur(beginOffset,
                       curOffset - beginOffset,
                       noteNumber,
                       shared->spcNoteVolume / 2,
                       duration,
                       "Percussion Note");
      addTime(shared->spcNoteDuration);
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t newProgNum = readByte(curOffset++);

      if (parentSeq->version != NINSNES_HUMAN) {
        if (newProgNum >= 0x80) {
          // See Star Fox sequence for example, this is necessary
          newProgNum = (newProgNum - parentSeq->STATUS_PERCUSSION_NOTE_MIN) + parentSeq->spcPercussionBase;
        }
      }

      if (parentSeq->version == NINSNES_QUINTET_ACTR) {
        newProgNum += parentSeq->quintetBGMInstrBase;
      }
      else if (NinSnesFormat::isQuintetVersion(parentSeq->version)) {
        newProgNum = readByte(parentSeq->quintetAddrBGMInstrLookup + newProgNum);
      }

      addProgramChange(beginOffset, curOffset - beginOffset, newProgNum, true);
      break;
    }

    case EVENT_PAN: {
      uint8_t newPan = readByte(curOffset++);

      double volumeScale;
      bool reverseLeft;
      bool reverseRight;
      int8_t midiPan = calculatePanValue(newPan, volumeScale, reverseLeft, reverseRight);
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));
      break;
    }

    case EVENT_PAN_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      uint8_t newPan = readByte(curOffset++);

      double volumeLeft;
      double volumeRight;
      getVolumeBalance(newPan << 8, volumeLeft, volumeRight);

      uint8_t midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight);

      // TODO: fade in real curve
      // TODO: apply volume scale
      addPanSlide(beginOffset, curOffset - beginOffset, fadeLength, midiPan);
      break;
    }

    case EVENT_VIBRATO_ON: {
      uint8_t vibratoDelay = readByte(curOffset++);
      uint8_t vibratoRate = readByte(curOffset++);
      uint8_t vibratoDepth = readByte(curOffset++);

      desc << "Delay: " << (int) vibratoDelay << "  Rate: " << (int) vibratoRate << "  Depth: "
          << (int) vibratoDepth;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato",
                      desc.str().c_str(),
                      Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato Off",
                      desc.str().c_str(),
                      Type::Vibrato);
      break;
    }

    case EVENT_MASTER_VOLUME: {
      uint8_t newVol = readByte(curOffset++);
      addMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
      break;
    }

    case EVENT_MASTER_VOLUME_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      uint8_t newVol = readByte(curOffset++);

      desc << "Length: " << (int) fadeLength << "  Volume: " << (int) newVol;
      addMastVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = readByte(curOffset++);
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM(newTempo));
      break;
    }

    case EVENT_TEMPO_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      uint8_t newTempo = readByte(curOffset++);

      addTempoSlide(beginOffset,
                    curOffset - beginOffset,
                    fadeLength,
                    (int) (60000000 / parentSeq->getTempoInBPM(newTempo)));
      break;
    }

    case EVENT_GLOBAL_TRANSPOSE: {
      int8_t semitones = readByte(curOffset++);
      addGlobalTranspose(beginOffset, curOffset - beginOffset, semitones);
      break;
    }

    case EVENT_TRANSPOSE: {
      int8_t semitones = readByte(curOffset++);
      shared->spcTranspose = semitones;
      addTranspose(beginOffset, curOffset - beginOffset, semitones);
      break;
    }

    case EVENT_TREMOLO_ON: {
      uint8_t tremoloDelay = readByte(curOffset++);
      uint8_t tremoloRate = readByte(curOffset++);
      uint8_t tremoloDepth = readByte(curOffset++);

      desc << "Delay: " << (int) tremoloDelay << "  Rate: " << (int) tremoloRate << "  Depth: "
          << (int) tremoloDepth;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tremolo",
                      desc.str().c_str(),
                      Type::Tremelo);
      break;
    }

    case EVENT_TREMOLO_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Tremolo Off",
                      desc.str().c_str(),
                      Type::Tremelo);
      break;
    }

    case EVENT_VOLUME: {
      uint8_t newVol = readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, newVol / 2);
      break;
    }

    case EVENT_VOLUME_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      uint8_t newVol = readByte(curOffset++);
      addVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
      break;
    }

    case EVENT_CALL: {
      uint16_t dest = getShortAddress(curOffset);
      curOffset += 2;
      uint8_t times = readByte(curOffset++);

      shared->loopReturnAddress = curOffset;
      shared->loopStartAddress = dest;
      shared->loopCount = times;

      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest
          << std::dec << std::setfill(' ') << std::setw(0) << "  Times: " << (int) times;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pattern Play",
                      desc.str().c_str(),
                      Type::RepeatStart);

      // Add the next "END" event to UI
      if (curOffset < 0x10000 && readByte(curOffset) == parentSeq->STATUS_END) {
        if (shared->loopCount == 0) {
          addGenericEvent(curOffset, 1, "Section End", desc.str().c_str(), Type::RepeatEnd);
        }
        else {
          addGenericEvent(curOffset, 1, "Pattern End", desc.str().c_str(), Type::RepeatEnd);
        }
      }

      curOffset = shared->loopStartAddress;
      break;
    }

    case EVENT_VIBRATO_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      desc << "Length: " << (int) fadeLength;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato Fade",
                      desc.str().c_str(),
                      Type::Vibrato);
      break;
    }

    case EVENT_PITCH_ENVELOPE_TO: {
      uint8_t pitchEnvDelay = readByte(curOffset++);
      uint8_t pitchEnvLength = readByte(curOffset++);
      int8_t pitchEnvSemitones = (int8_t) readByte(curOffset++);

      desc << "Delay: " << (int) pitchEnvDelay << "  Length: " << (int) pitchEnvLength << "  Semitones: "
          << (int) pitchEnvSemitones;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Envelope (To)",
                      desc.str().c_str(),
                      Type::PitchEnvelope);
      break;
    }

    case EVENT_PITCH_ENVELOPE_FROM: {
      uint8_t pitchEnvDelay = readByte(curOffset++);
      uint8_t pitchEnvLength = readByte(curOffset++);
      int8_t pitchEnvSemitones = (int8_t) readByte(curOffset++);

      desc << "Delay: " << (int) pitchEnvDelay << "  Length: " << (int) pitchEnvLength << "  Semitones: "
          << (int) pitchEnvSemitones;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Envelope (From)",
                      desc.str().c_str(),
                      Type::PitchEnvelope);
      break;
    }

    case EVENT_PITCH_ENVELOPE_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Envelope Off",
                      desc.str().c_str(),
                      Type::PitchEnvelope);
      break;
    }

    case EVENT_TUNING: {
      uint8_t newTuning = readByte(curOffset++);
      addFineTuning(beginOffset, curOffset - beginOffset, (newTuning / 256.0) * 100.0);
      break;
    }

    case EVENT_QUINTET_TUNING: {
      // TODO: Correct fine tuning on Quintet games
      // In Quintet games (at least in Terranigma), the fine tuning command overwrites the fractional part of instrument tuning.
      // In other words, we cannot calculate the tuning amount without reading the instrument table.
      uint8_t newTuning = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Fine Tuning", desc.str().c_str(), Type::FineTune);
      addFineTuningNoItem((newTuning / 256.0) * 61.8); // obviously not correct, but better than nothing?
      break;
    }

    case EVENT_ECHO_ON: {
      uint8_t spcEON = readByte(curOffset++);
      uint8_t spcEVOL_L = readByte(curOffset++);
      uint8_t spcEVOL_R = readByte(curOffset++);

      desc << "Channels: ";
      for (int channelNo = MAX_TRACKS - 1; channelNo >= 0; channelNo--) {
        if ((spcEON & (1 << channelNo)) != 0) {
          desc << (int) channelNo;
          parentSeq->aTracks[channelNo]->addReverbNoItem(40);
        }
        else {
          desc << "-";
          parentSeq->aTracks[channelNo]->addReverbNoItem(0);
        }
      }

      desc << "  Volume Left: " << (int) spcEVOL_L << "  Volume Right: " << (int) spcEVOL_R;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo", desc.str().c_str(), Type::Reverb);
      break;
    }

    case EVENT_ECHO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Off", desc.str().c_str(), Type::Reverb);
      break;
    }

    case EVENT_ECHO_PARAM: {
      uint8_t spcEDL = readByte(curOffset++);
      uint8_t spcEFB = readByte(curOffset++);
      uint8_t spcFIR = readByte(curOffset++);

      desc << "Delay: " << (int) spcEDL << "  Feedback: " << (int) spcEFB << "  FIR: " << (int) spcFIR;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Param", desc.str().c_str(), Type::Reverb);
      break;
    }

    case EVENT_ECHO_VOLUME_FADE: {
      uint8_t fadeLength = readByte(curOffset++);
      uint8_t spcEVOL_L = readByte(curOffset++);
      uint8_t spcEVOL_R = readByte(curOffset++);

      desc << "Length: " << (int) fadeLength << "  Volume Left: " << (int) spcEVOL_L << "  Volume Right: "
          << (int) spcEVOL_R;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Volume Fade",
                      desc.str().c_str(),
                      Type::Reverb);
      break;
    }

    case EVENT_PITCH_SLIDE: {
      uint8_t pitchSlideDelay = readByte(curOffset++);
      uint8_t pitchSlideLength = readByte(curOffset++);
      uint8_t pitchSlideTargetNote = readByte(curOffset++);

      desc << "Delay: " << (int) pitchSlideDelay << "  Length: " << (int) pitchSlideLength << "  Note: "
          << (int) (pitchSlideTargetNote - 0x80);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pitch Slide",
                      desc.str().c_str(),
                      Type::PitchBendSlide);
      break;
    }

    case EVENT_PERCCUSION_PATCH_BASE: {
      uint8_t percussionBase = readByte(curOffset++);
      parentSeq->spcPercussionBase = percussionBase;

      desc << "Percussion Base: " << (int) percussionBase;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Percussion Base",
                      desc.str().c_str(),
                      Type::ChangeState);
      break;
    }

      // NINTENDO RD2 EVENTS START >>

    case EVENT_RD2_PROGCHANGE_AND_ADSR: {
      // This event overwrites ADSR in instrument table
      uint8_t newProgNum = readByte(curOffset++);
      uint8_t adsr1 = readByte(curOffset++);
      uint8_t adsr2 = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, newProgNum, true, "Program Change & ADSR");
      break;
    }

      // << NINTENDO RD2 EVENTS END

      // KONAMI EVENTS START >>

    case EVENT_KONAMI_LOOP_START: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", desc.str().c_str(), Type::RepeatStart);
      shared->konamiLoopStart = curOffset;
      shared->konamiLoopCount = 0;
      break;
    }

    case EVENT_KONAMI_LOOP_END: {
      uint8_t times = readByte(curOffset++);
      int8_t volumeDelta = readByte(curOffset++);
      int8_t pitchDelta = readByte(curOffset++);

      desc << "Times: " << (int) times << "  Volume Delta: " << (int) volumeDelta << "  Pitch Delta: "
          << (int) pitchDelta << "/16 semitones";
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc.str().c_str(), Type::RepeatEnd);

      shared->konamiLoopCount++;
      if (shared->konamiLoopCount != times) {
        // repeat again
        curOffset = shared->konamiLoopStart;
      }

      break;
    }

    case EVENT_KONAMI_ADSR_AND_GAIN: {
      uint8_t adsr1 = readByte(curOffset++);
      uint8_t adsr2 = readByte(curOffset++);
      uint8_t gain = readByte(curOffset++);
      desc << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << "ADSR(1): $" << adsr1
          << "  ADSR(2): $" << adsr2 << "  GAIN: $" << gain;
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR/GAIN", desc.str(), Type::Adsr);
      break;
    }

      // << KONAMI EVENTS END

      // LEMMINGS EVENTS START >>

    case EVENT_LEMMINGS_NOTE_PARAM: {
      // param #0: duration
      shared->spcNoteDuration = statusByte;
      desc << "Duration: " << (int) shared->spcNoteDuration;

      // param #1: quantize (optional)
      if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
        uint8_t durByte = readByte(curOffset++);
        shared->spcNoteDurRate = (durByte << 1) + (durByte >> 1) + (durByte & 1); // approx percent?
        desc << "  Quantize: " << durByte << " (" << shared->spcNoteDurRate << "/256)";

        // param #2: velocity (optional)
        if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
          uint8_t velByte = readByte(curOffset++);
          shared->spcNoteVolume = velByte << 1;
          desc << "  Velocity: " << velByte << " (" << shared->spcNoteVolume << "/256)";
        }
      }

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Note Param",
                      desc.str().c_str(),
                      Type::DurationChange);
      break;
    }

      // << LEMMINGS EVENTS END

      // INTELLIGENT SYSTEMS EVENTS START >>

    case EVENT_INTELLI_NOTE_PARAM: {
      if (parentSeq->version != NINSNES_INTELLI_FE4 && !parentSeq->intelliUseCustomNoteParam) {
        goto OnEventNoteParam;
      }

      // param #0: duration
      shared->spcNoteDuration = statusByte;
      desc << "Duration: " << (int) shared->spcNoteDuration;

      // param #1,2...: quantize/velocity (optional)
      while (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
        uint8_t noteParam = readByte(curOffset++);
        if (noteParam < 0x40) { // 00..3f
          uint8_t durIndex = noteParam & 0x3f;
          shared->spcNoteDurRate = parentSeq->intelliDurVolTable[durIndex];
          desc << "  Quantize: " << durIndex << " (" << shared->spcNoteDurRate << "/256)";
        }
        else { // 40..7f
          uint8_t velIndex = noteParam & 0x3f;
          shared->spcNoteVolume = parentSeq->intelliDurVolTable[velIndex];
          desc << "  Velocity: " << velIndex << " (" << shared->spcNoteVolume << "/256)";
        }
      }

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Note Param",
                      desc.str().c_str(),
                      Type::DurationChange);
      break;
    }

    case EVENT_INTELLI_ECHO_ON: {
      addReverb(beginOffset, curOffset - beginOffset, 40, "Echo On");
      break;
    }

    case EVENT_INTELLI_ECHO_OFF: {
      addReverb(beginOffset, curOffset - beginOffset, 0, "Echo Off");
      break;
    }

    case EVENT_INTELLI_LEGATO_ON: {
      // TODO: cancel keyoff of note (i.e. full duration)
      addGenericEvent(beginOffset, curOffset - beginOffset, "Legato On", desc.str(), Type::Portamento);
      break;
    }

    case EVENT_INTELLI_LEGATO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Legato Off", desc.str(), Type::Portamento);
      break;
    }

    case EVENT_INTELLI_JUMP_SHORT_CONDITIONAL: {
      uint8_t offset = readByte(curOffset++);
      uint16_t dest = curOffset + offset;

      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Conditional Jump (Short)", desc.str().c_str(), Type::Misc);

      // condition for branch has not been researched yet
      curOffset = dest;
      break;
    }

    case EVENT_INTELLI_JUMP_SHORT: {
      uint8_t offset = readByte(curOffset++);
      uint16_t dest = curOffset + offset;

      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Jump (Short)", desc.str().c_str(), Type::Misc);

      curOffset = dest;
      break;
    }

    case EVENT_INTELLI_FE3_EVENT_F5: {
      uint8_t param = readByte(curOffset++);
      if (param < 0xf0) {
        // wait for APU port #2
        desc << "Value: " << param;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Wait for APU Port #2", desc.str(), Type::ChangeState);
      }
      else {
        // set/clear bitflag in $ca
        uint8_t bit = param & 7;
        bool bitValue = (param & 8) == 0;

        desc << "Status: " << (bitValue ? "On" : "Off");
        switch (bit) {
          case 0:
            parentSeq->intelliUseCustomPercTable = bitValue;
            addGenericEvent(beginOffset,
                            curOffset - beginOffset,
                            "Use Custom Percussion Table",
                            desc.str(),
                            Type::ChangeState);
            break;

          case 7:
            parentSeq->intelliUseCustomNoteParam = bitValue;
            addGenericEvent(beginOffset,
                            curOffset - beginOffset,
                            "Use Custom Note Param",
                            desc.str(),
                            Type::ChangeState);
            break;

          default:
            addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
        }
      }
      break;
    }

    case EVENT_INTELLI_WRITE_APU_PORT: {
      uint8_t value = readByte(curOffset++);
      desc << "Value: " << value;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Write APU Port", desc.str(), Type::ChangeState);
      break;
    }

    case EVENT_INTELLI_FE3_EVENT_F9: {
      curOffset += 36;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_INTELLI_DEFINE_VOICE_PARAM: {
      int8_t param = readByte(curOffset++);
      if (param >= 0) {
        parentSeq->intelliVoiceParamTableSize = param;
        parentSeq->intelliVoiceParamTable = curOffset;
        curOffset += parentSeq->intelliVoiceParamTableSize * 4;
        desc << "Number of Items: " << parentSeq->intelliVoiceParamTableSize;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Voice Param Table", desc.str(), Type::Misc);
      }
      else {
        if (parentSeq->version == NINSNES_INTELLI_FE3 || parentSeq->version == NINSNES_INTELLI_TA) {
          uint8_t instrNum = param & 0x3f;
          curOffset += 6;
          desc << "Instrument: " << instrNum;
          addGenericEvent(beginOffset, curOffset - beginOffset, "Overwrite Instrument Region", desc.str(), Type::Misc);
        }
        else {
          addUnknown(beginOffset, curOffset - beginOffset);
        }
      }
      break;
    }

    case EVENT_INTELLI_LOAD_VOICE_PARAM: {
      uint8_t paramIndex = readByte(curOffset++);
      desc << "Index: " << paramIndex;

      if (paramIndex < parentSeq->intelliVoiceParamTableSize) {
        uint16_t addrVoiceParam = parentSeq->intelliVoiceParamTable + (paramIndex * 4);
        uint8_t instrByte = readByte(addrVoiceParam);
        uint8_t newVol = readByte(addrVoiceParam + 1);
        uint8_t newPan = readByte(addrVoiceParam + 2);
        uint8_t tuningByte = readByte(addrVoiceParam + 3);

        uint8_t newProgNum = instrByte;
        if (parentSeq->version != NINSNES_INTELLI_FE3) {
          newProgNum &= 0x1f;
        }

        addProgramChangeNoItem(newProgNum, true);

        addVolNoItem(newVol / 2);

        double volumeScale;
        bool reverseLeft;
        bool reverseRight;
        int8_t midiPan = calculatePanValue(newPan, volumeScale, reverseLeft, reverseRight);
        addPanNoItem(midiPan);
        addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));

        switch (parentSeq->version) {
          case NINSNES_INTELLI_FE3: {
            uint8_t tuningIndex = tuningByte & 15;
            uint8_t transposeIndex = (tuningByte & 0x70) >> 4;

            if (tuningIndex != 0) {
              uint8_t newTuning = (tuningIndex - 1) * 5;
              addFineTuningNoItem((newTuning / 256.0) * 100.0);
            }

            if (transposeIndex != 0) {
              const int8_t FE3_TRANSPOSE_TABLE[7] = {-24, -12, -1, 0, 1, 12, 24};
              int8_t newTranspose = FE3_TRANSPOSE_TABLE[transposeIndex - 1];
              shared->spcTranspose = newTranspose;
              transpose = newTranspose;
            }

            break;
          }

          case NINSNES_INTELLI_TA:
          case NINSNES_INTELLI_FE4: {
            int8_t newTranspose = tuningByte;
            shared->spcTranspose = newTranspose;
            transpose = newTranspose;

            uint8_t newTuning = (instrByte >> 3) * 5;
            addFineTuningNoItem((newTuning / 256.0) * 100.0);

            break;
          }

          default:
            break;
        }
      }

      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Load Voice Param",
                      desc.str(),
                      Type::ProgramChange);
      break;
    }

    case EVENT_INTELLI_ADSR: {
      uint8_t adsr1 = readByte(curOffset++);
      uint8_t adsr2 = readByte(curOffset++);
      desc << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << "ADSR(1): $" << adsr1
          << "  ADSR(2): $" << adsr2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc.str(), Type::Adsr);
      break;
    }

    case EVENT_QUINTET_ADSR: {
      uint8_t adsr1 = readByte(curOffset++);
      uint8_t sustain_rate = readByte(curOffset++);
      uint8_t sustain_level = readByte(curOffset++);
	  uint8_t adsr2 = (sustain_level << 5) | sustain_rate;
      desc << "ADSR(1): $" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << adsr1
          << "  Sustain Rate: " << std::dec << sustain_rate << "  Sustain Level: " << sustain_level
          << "  (ADSR(2): $" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << adsr2 << ")";
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc.str(), Type::Adsr);
      break;
    }

    case EVENT_INTELLI_GAIN_SUSTAIN_TIME_AND_RATE: {
      uint8_t sustainDurRate = readByte(curOffset++);
      uint8_t sustainGAIN = readByte(curOffset++);
      desc << "Duration for Sustain: " << sustainDurRate << "/256" << std::hex << std::setfill('0') << std::setw(2)
          << std::uppercase << "  GAIN for Sustain: $" << sustainGAIN;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "GAIN Sustain Time/Rate",
                      desc.str(),
                      Type::Adsr);
      break;
    }

    case EVENT_INTELLI_GAIN_SUSTAIN_TIME: {
      uint8_t sustainDurRate = readByte(curOffset++);
      desc << "Duration for Sustain: " << sustainDurRate << "/256";
      addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN Sustain Time", desc.str(), Type::Adsr);
      break;
    }

    case EVENT_INTELLI_GAIN: {
      // This event will update GAIN immediately,
      // however, note that Fire Emblem 4 does not switch to GAIN mode until note off.
      uint8_t gain = readByte(curOffset++);
      desc << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << "  GAIN: $" << gain;
      addGenericEvent(beginOffset, curOffset - beginOffset, "GAIN (Release Rate)", desc.str(), Type::Adsr);
      break;
    }

    case EVENT_INTELLI_FE4_EVENT_FC: {
      uint8_t arg1 = readByte(curOffset++);
      curOffset += ((arg1 & 15) + 1) * 3;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_INTELLI_TA_SUBEVENT: {
      uint8_t type = readByte(curOffset++);

      switch (type) {
        case 0x00:
          curOffset += 3;
          addUnknown(beginOffset, curOffset - beginOffset);
          break;

        case 0x01:
        case 0x02: {
          // set/clear bitflag in $ca
          uint8_t param = readByte(curOffset++);
          bool bitValue = (type == 0x01);

          if ((param & 0x80) != 0) {
            parentSeq->intelliUseCustomNoteParam = bitValue;
          }
          if ((param & 0x40) != 0) {
            parentSeq->intelliUseCustomPercTable = bitValue;
          }

          if (param == 0x80) {
            desc << "Status: " << (bitValue ? "On" : "Off");
            addGenericEvent(beginOffset,
                            curOffset - beginOffset,
                            "Use Custom Note Param",
                            desc.str(),
                            Type::ChangeState);
          }
          else if (param == 0x40) {
            desc << "Status: " << (bitValue ? "On" : "Off");
            addGenericEvent(beginOffset,
                            curOffset - beginOffset,
                            "Use Custom Percussion Table",
                            desc.str(),
                            Type::ChangeState);
          }
          else {
            desc << "Value: $" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << param;
            if (type == 0x01) {
              addGenericEvent(beginOffset, curOffset - beginOffset, "Set Flags On", desc.str(), Type::ChangeState);
            }
            else {
              addGenericEvent(beginOffset, curOffset - beginOffset, "Set Flags Off", desc.str(), Type::ChangeState);
            }
          }

          break;
        }

        case 0x03:
          // TODO: cancel keyoff of note (i.e. full duration)
          addGenericEvent(beginOffset, curOffset - beginOffset, "Legato On", desc.str(), Type::Portamento);
          break;

        case 0x04:
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "Legato Off",
                          desc.str(),
                          Type::Portamento);
          break;

        case 0x05:
          curOffset++;
          addUnknown(beginOffset, curOffset - beginOffset);
          break;

        default:
          addUnknown(beginOffset, curOffset - beginOffset);
          break;
      }

      break;
    }

    case EVENT_INTELLI_FE4_SUBEVENT: {
      uint8_t type = readByte(curOffset++);

      switch (type) {
        case 0x01:
          curOffset++;
          addUnknown(beginOffset, curOffset - beginOffset);
          break;

        case 0x02:
          curOffset++;
          addUnknown(beginOffset, curOffset - beginOffset);
          break;

        default:
          addUnknown(beginOffset, curOffset - beginOffset);
          break;
      }

      break;
    }

      // << INTELLIGENT SYSTEMS EVENTS END

    default: {
      auto descr = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }

  // Add the next "END" event to UI
  // (because it often gets interrupted by the end of other track)
  if (curOffset + 1 <= 0x10000 && statusByte != parentSeq->STATUS_END && readByte(curOffset) == parentSeq->STATUS_END) {
    if (shared->loopCount == 0) {
      addGenericEvent(curOffset, 1, "Section End", desc.str().c_str(), Type::TrackEnd);
    }
    else {
      addGenericEvent(curOffset, 1, "Pattern End", desc.str().c_str(), Type::RepeatEnd);
    }
  }

  return bContinue;
}

uint16_t NinSnesTrack::convertToApuAddress(uint16_t offset) {
  NinSnesSeq *parentSeq = (NinSnesSeq *) this->parentSeq;
  return parentSeq->convertToAPUAddress(offset);
}

uint16_t NinSnesTrack::getShortAddress(uint32_t offset) {
  NinSnesSeq *parentSeq = (NinSnesSeq *) this->parentSeq;
  return parentSeq->getShortAddress(offset);
}

void NinSnesTrack::getVolumeBalance(uint16_t pan, double &volumeLeft, double &volumeRight) {
  NinSnesSeq *parentSeq = (NinSnesSeq *) this->parentSeq;

  uint8_t panIndex = pan >> 8;
  if (parentSeq->version == NINSNES_TOSE) {
    if (panIndex <= 10) {
      // pan right, decrease left volume
      volumeLeft = (255 - 25 * std::max(10 - panIndex, 0)) / 256.0;
      volumeRight = 1.0;
    }
    else {
      // pan left, decrease right volume
      volumeLeft = 1.0;
      volumeRight = (255 - 25 * std::max(panIndex - 10, 0)) / 256.0;
    }
  }
  else {
    uint8_t panMaxIndex = (uint8_t) (parentSeq->panTable.size() - 1);
    if (panIndex > panMaxIndex) {
      // unexpected behavior
      pan = panMaxIndex << 8;
      panIndex = panMaxIndex;
    }

    // actual engine divides pan by 256, though pan value is 7-bit
    // by the way, note that it is right-to-left pan
    volumeRight = readPanTable((panMaxIndex << 8) - pan) / 128.0;
    volumeLeft = readPanTable(pan) / 128.0;

    if (parentSeq->version == NINSNES_HAL) {
      // left-to-right pan
      std::swap(volumeLeft, volumeRight);
    }
  }
}

uint8_t NinSnesTrack::readPanTable(uint16_t pan) {
  NinSnesSeq *parentSeq = (NinSnesSeq *) this->parentSeq;

  if (parentSeq->version == NINSNES_TOSE) {
    // no pan table
    return 0;
  }

  uint8_t panIndex = pan >> 8;
  uint8_t panFraction = pan & 0xff;

  uint8_t panMaxIndex = (uint8_t) (parentSeq->panTable.size() - 1);
  if (panIndex > panMaxIndex) {
    // unexpected behavior
    panIndex = panMaxIndex;
    panFraction = 0; // floor(pan)
  }

  uint8_t volumeRate = parentSeq->panTable[panIndex];

  // linear interpolation for pan fade
  uint8_t nextVolumeRate = (panIndex < panMaxIndex) ? parentSeq->panTable[panIndex + 1] : volumeRate;
  uint8_t volumeRateDelta = nextVolumeRate - volumeRate;
  volumeRate += (volumeRateDelta * panFraction) >> 8;

  return volumeRate;
}

int8_t NinSnesTrack::calculatePanValue(uint8_t pan, double &volumeScale, bool &reverseLeft, bool &reverseRight) {
  NinSnesSeq *parentSeq = (NinSnesSeq *) this->parentSeq;

  uint8_t panIndex;
  if (parentSeq->version == NINSNES_TOSE) {
    panIndex = pan;
    reverseLeft = false;
    reverseRight = false;
  }
  else {
    panIndex = pan & 0x1f;
    reverseLeft = (pan & 0x80) != 0;
    reverseRight = (pan & 0x40) != 0;
  }

  double volumeLeft;
  double volumeRight;
  getVolumeBalance(panIndex << 8, volumeLeft, volumeRight);

  // TODO: correct volume scale of TOSE sequence
  int8_t midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &volumeScale);
  volumeScale = std::min(volumeScale, 1.0); // workaround

  return midiPan;
}
