/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <bitset>
#include "AkaoSeq.h"
#include "AkaoInstr.h"
#include "LogManager.h"
#include "spdlog/fmt/fmt.h"

DECLARE_FORMAT(Akao);

using namespace std;

static const uint16_t DELTA_TIME_TABLE[] = { 192, 96, 48, 24, 12, 6, 3, 32, 16, 8, 4 };
static constexpr uint8_t NOTE_VELOCITY = 127;

AkaoSeq::AkaoSeq(RawFile *file, uint32_t offset, AkaoPs1Version version, std::string name)
    : VGMSeq(AkaoFormat::name, file, offset, 0, std::move(name)), seq_id(0), version_(version),
      instrument_set_offset_(0), drum_set_offset_(0), condition(0) {
  setAlwaysWriteInitialVol(127);
  setAlwaysWriteInitialPitchBendRange(12 * 100);
  setUseLinearAmplitudeScale(true);        //I think this applies, but not certain, see FF9 320, track 3 for example of problem
  //UseLinearPanAmplitudeScale(PanVolumeCorrectionMode::kAdjustVolumeController); // disabled, it only changes the volume and the pan slightly, and also its output becomes undefined if pan and volume slides are used at the same time
  bUsesIndividualArts = false;
  useReverb();
}

void AkaoSeq::resetVars() {
  VGMSeq::resetVars();

  condition = 0;
  if (rawFile()->tag.album == "Final Fantasy 9" && rawFile()->tag.title == "Final Battle")
    condition = 2;
}

bool AkaoSeq::isPossibleAkaoSeq(const RawFile *file, uint32_t offset) {
  if (offset + 0x10 > file->size())
    return false;
  if (file->readWordBE(offset) != 0x414B414F)
    return false;

  const AkaoPs1Version version = guessVersion(file, offset);
  const uint32_t track_bits_offset = getTrackAllocationBitsOffset(version);
  if (offset + track_bits_offset + 4 > file->size())
    return false;

  const uint32_t track_bits = file->readWord(offset + track_bits_offset);
  if (version <= AkaoPs1Version::VERSION_2) {
    if ((track_bits & ~0xffffff) != 0)
      return false;
  }

  if (version >= AkaoPs1Version::VERSION_3_0) {
    if (file->readWord(offset + 0x2C) != 0 || file->readWord(offset + 0x28) != 0)
      return false;
    if (file->readWord(offset + 0x38) != 0 || file->readWord(offset + 0x3C) != 0)
      return false;
  }

  return true;
}

AkaoPs1Version AkaoSeq::guessVersion(const RawFile *file, uint32_t offset) {
  if (file->readWord(offset + 0x2C) == 0)
    return AkaoPs1Version::VERSION_3_2;
  else if (file->readWord(offset + 0x1C) == 0)
    return AkaoPs1Version::VERSION_2;
  else
    return AkaoPs1Version::VERSION_1_1;
}

bool AkaoSeq::parseHeader() {
  if (version() == AkaoPs1Version::UNKNOWN)
    return false;

  const uint32_t track_bits = (version() >= AkaoPs1Version::VERSION_3_0)
    ? readWord(dwOffset + 0x20)
    : readWord(dwOffset + 0x10);
  nNumTracks = static_cast<uint32_t>(std::bitset<32>(track_bits).count()); // popcount

  uint32_t track_header_offset;
  if (version() >= AkaoPs1Version::VERSION_3_0) {
    VGMHeader *hdr = addHeader(dwOffset, 0x40);
    hdr->addSig(dwOffset, 4);
    hdr->addChild(dwOffset + 0x4, 2, "ID");
    hdr->addChild(dwOffset + 0x6, 2, "Size");
    hdr->addChild(dwOffset + 0x8, 2, "Reverb Type");
    hdr->addChild(dwOffset + 0x14, 2, "Associated Sample Set ID");
    hdr->addChild(dwOffset + 0x20, 4, "Number of Tracks (# of true bits)");
    hdr->addChild(dwOffset + 0x30, 4, "Instrument Data Pointer");
    hdr->addChild(dwOffset + 0x34, 4, "Drumkit Data Pointer");

    unLength = readShort(dwOffset + 6);
    setId(readShort(dwOffset + 0x14));
    track_header_offset = 0x40;
  }
  else if (version() == AkaoPs1Version::VERSION_2) {
    VGMHeader *hdr = addHeader(dwOffset, 0x20);
    hdr->addSig(dwOffset, 4);
    hdr->addChild(dwOffset + 0x4, 2, "ID");
    hdr->addChild(dwOffset + 0x6, 2, "Size (Excluding first 16 bytes)");
    hdr->addChild(dwOffset + 0x8, 2, "Reverb Type");
    hdr->addChild(dwOffset + 0x10, 4, "Number of Tracks (# of true bits)");

    unLength = 0x10 + readShort(dwOffset + 6);
    track_header_offset = 0x20;
  }
  else if (version() < AkaoPs1Version::VERSION_2) {
    VGMHeader *hdr = addHeader(dwOffset, 0x14);
    hdr->addSig(dwOffset, 4);
    hdr->addChild(dwOffset + 0x4, 2, "ID");
    hdr->addChild(dwOffset + 0x6, 2, "Size (Excluding first 16 bytes)");
    hdr->addChild(dwOffset + 0x8, 2, "Reverb Type");
    auto timestamp_text = fmt::format("Timestamp ({})", readTimestampAsText());
    hdr->addChild(dwOffset + 0xA, 6, timestamp_text);
    hdr->addChild(dwOffset + 0x10, 4, "Number of Tracks (# of true bits)");

    unLength = 0x10 + readShort(dwOffset + 6);
    track_header_offset = 0x14;
  }
  else {
    return false;
  }

  setPPQN(0x30);
  seq_id = readShort(dwOffset + 4);

  LoadEventMap();

  if (version() >= AkaoPs1Version::VERSION_3_0)
  {
    //There must be either a melodic instrument section, a drumkit, or both.  We determine
    //the start of the InstrSet based on whether a melodic instrument section is given.
    const uint32_t instrOff = readWord(dwOffset + 0x30);
    const uint32_t drumkitOff = readWord(dwOffset + 0x34);
    if (instrOff != 0)
      set_instrument_set_offset(dwOffset + 0x30 + instrOff);
    if (drumkitOff != 0)
      set_drum_set_offset(dwOffset + 0x34 + drumkitOff);
  }

  VGMHeader *track_pointer_header = addHeader(dwOffset + track_header_offset, nNumTracks * 2);
  for (unsigned int i = 0; i < nNumTracks; i++) {
    auto name = fmt::format("Offset: Track {}", i + 1);
    track_pointer_header->addChild(dwOffset + track_header_offset + (i * 2), 2, name);
  }

  return true;
}


bool AkaoSeq::parseTrackPointers() {
  uint32_t track_header_offset;
  switch (version())
  {
  case AkaoPs1Version::VERSION_1_0:
  case AkaoPs1Version::VERSION_1_1:
  case AkaoPs1Version::VERSION_1_2:
    track_header_offset = 0x14;
    break;

  case AkaoPs1Version::VERSION_2:
    track_header_offset = 0x20;
    break;

  case AkaoPs1Version::VERSION_3_0:
  case AkaoPs1Version::VERSION_3_1:
  case AkaoPs1Version::VERSION_3_2:
  default:
    track_header_offset = 0x40;
    break;
  }

  for (unsigned int i = 0; i < nNumTracks; i++) {
    const uint32_t p = track_header_offset + (i * 2);
    const uint32_t base = p + (version() >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
    const uint32_t relative_offset = readShort(dwOffset + p);
    const uint32_t track_offset = base + relative_offset;
    aTracks.push_back(new AkaoTrack(this, dwOffset + track_offset));
  }
  return true;
}

std::string AkaoSeq::readTimestampAsText() const {
  const uint8_t year_bcd = readByte(dwOffset + 0xA);
  const uint8_t month_bcd = readByte(dwOffset + 0xB);
  const uint8_t day_bcd = readByte(dwOffset + 0xC);
  const uint8_t hour_bcd = readByte(dwOffset + 0xD);
  const uint8_t minute_bcd = readByte(dwOffset + 0xE);
  const uint8_t second_bcd = readByte(dwOffset + 0xF);

  // Should we solve the year 2000 problem?
  const unsigned int year_bcd_full = 0x1900 + year_bcd;

  return fmt::format("{:02X}-{:02X}-{:02X}T{:02X}:{:02X}:{:02X}",
                     year_bcd_full,
                     month_bcd,
                     day_bcd,
                     hour_bcd,
                     minute_bcd,
                     second_bcd);
}

double AkaoSeq::getTempoInBPM(uint16_t tempo) const {
  if (tempo != 0) {
    const uint16_t freq = (version() == AkaoPs1Version::VERSION_1_0) ? 0x43D1 : 0x44E8;
    return 60.0 / (ppqn() * (65536.0 / tempo) * (freq / (33868800.0 / 8)));
  }
  else {
    // since tempo 0 cannot be expressed, this function returns a very small value.
    return 1.0;
  }
}

AkaoInstrSet* AkaoSeq::newInstrSet() const {
  if (version() >= AkaoPs1Version::VERSION_3_0) {
    uint32_t length = 0;
    if (has_instrument_set_offset())
      length = unLength - (instrument_set_offset() - dwOffset);
    else if (has_drum_set_offset())
      length = unLength - (drum_set_offset() - dwOffset);

    return length != 0
      ? new AkaoInstrSet(rawFile(), length, version(), instrument_set_offset(), drum_set_offset(), seq_id, "Akao Instr Set")
      : new AkaoInstrSet(rawFile(), dwOffset, dwOffset + unLength, version(), seq_id);
  } else {
    return new AkaoInstrSet(rawFile(), dwOffset + unLength, version(), custom_instrument_addresses, drum_instrument_addresses, seq_id);
  }
}

void AkaoSeq::LoadEventMap()
{
  event_map[0xa0] = EVENT_END;
  event_map[0xa1] = EVENT_PROGCHANGE;
  event_map[0xa2] = EVENT_ONE_TIME_DURATION;
  event_map[0xa3] = EVENT_VOLUME;
  event_map[0xa4] = EVENT_PITCH_SLIDE;
  event_map[0xa5] = EVENT_OCTAVE;
  event_map[0xa6] = EVENT_INCREMENT_OCTAVE;
  event_map[0xa7] = EVENT_DECREMENT_OCTAVE;
  event_map[0xa8] = EVENT_EXPRESSION;
  event_map[0xa9] = EVENT_EXPRESSION_FADE;
  event_map[0xaa] = EVENT_PAN;
  event_map[0xab] = EVENT_PAN_FADE;
  event_map[0xac] = EVENT_NOISE_CLOCK;
  event_map[0xad] = EVENT_ADSR_ATTACK_RATE;
  event_map[0xae] = EVENT_ADSR_DECAY_RATE;
  event_map[0xaf] = EVENT_ADSR_SUSTAIN_LEVEL;
  event_map[0xb0] = EVENT_ADSR_DECAY_RATE_AND_SUSTAIN_LEVEL;
  event_map[0xb1] = EVENT_ADSR_SUSTAIN_RATE;
  event_map[0xb2] = EVENT_ADSR_RELEASE_RATE;
  event_map[0xb3] = EVENT_RESET_ADSR;
  event_map[0xb4] = EVENT_VIBRATO;
  event_map[0xb5] = EVENT_VIBRATO_DEPTH;
  event_map[0xb6] = EVENT_VIBRATO_OFF;
  event_map[0xb7] = EVENT_ADSR_ATTACK_MODE;
  event_map[0xb8] = EVENT_TREMOLO;
  event_map[0xb9] = EVENT_TREMOLO_DEPTH;
  event_map[0xba] = EVENT_TREMOLO_OFF;
  event_map[0xbb] = EVENT_ADSR_SUSTAIN_MODE;
  event_map[0xbc] = EVENT_PAN_LFO;
  event_map[0xbd] = EVENT_PAN_LFO_DEPTH;
  event_map[0xbe] = EVENT_PAN_LFO_OFF;
  event_map[0xbf] = EVENT_ADSR_RELEASE_MODE;
  event_map[0xc0] = EVENT_TRANSPOSE_ABS;
  event_map[0xc1] = EVENT_TRANSPOSE_REL;
  event_map[0xc2] = EVENT_REVERB_ON;
  event_map[0xc3] = EVENT_REVERB_OFF;
  event_map[0xc4] = EVENT_NOISE_ON;
  event_map[0xc5] = EVENT_NOISE_OFF;
  event_map[0xc6] = EVENT_PITCH_MOD_ON;
  event_map[0xc7] = EVENT_PITCH_MOD_OFF;
  event_map[0xc8] = EVENT_LOOP_START;
  event_map[0xc9] = EVENT_LOOP_UNTIL;
  event_map[0xca] = EVENT_LOOP_AGAIN;
  event_map[0xcb] = EVENT_RESET_VOICE_EFFECTS;
  event_map[0xcc] = EVENT_SLUR_ON;
  event_map[0xcd] = EVENT_SLUR_OFF;
  event_map[0xce] = EVENT_NOISE_ON_DELAY_TOGGLE;
  event_map[0xcf] = EVENT_NOISE_DELAY_TOGGLE;
  event_map[0xd0] = EVENT_LEGATO_ON;
  event_map[0xd1] = EVENT_LEGATO_OFF;
  event_map[0xd2] = EVENT_PITCH_MOD_ON_DELAY_TOGGLE;
  event_map[0xd3] = EVENT_PITCH_MOD_DELAY_TOGGLE;
  event_map[0xd4] = EVENT_PITCH_SIDE_CHAIN_ON;
  event_map[0xd5] = EVENT_PITCH_SIDE_CHAIN_OFF;
  event_map[0xd6] = EVENT_PITCH_TO_VOLUME_SIDE_CHAIN_ON;
  event_map[0xd7] = EVENT_PITCH_TO_VOLUME_SIDE_CHAIN_OFF;
  event_map[0xd8] = EVENT_TUNING_ABS;
  event_map[0xd9] = EVENT_TUNING_REL;
  event_map[0xda] = EVENT_PORTAMENTO_ON;
  event_map[0xdb] = EVENT_PORTAMENTO_OFF;
  event_map[0xdc] = EVENT_FIXED_DURATION;
  event_map[0xdd] = EVENT_VIBRATO_DEPTH_FADE;
  event_map[0xde] = EVENT_TREMOLO_DEPTH_FADE;
  event_map[0xdf] = EVENT_PAN_LFO_FADE;

  if (version() == AkaoPs1Version::VERSION_1_0 || version() == AkaoPs1Version::VERSION_1_1) {
    event_map[0xe0] = EVENT_UNIMPLEMENTED;
    event_map[0xe1] = EVENT_UNIMPLEMENTED;
    event_map[0xe2] = EVENT_UNIMPLEMENTED;
    event_map[0xe3] = EVENT_UNIMPLEMENTED;
    event_map[0xe4] = EVENT_UNIMPLEMENTED;
    event_map[0xe5] = EVENT_UNIMPLEMENTED;
    event_map[0xe6] = EVENT_UNIMPLEMENTED;
    event_map[0xe7] = EVENT_UNIMPLEMENTED;
    event_map[0xe8] = EVENT_TEMPO;
    event_map[0xe9] = EVENT_TEMPO_FADE;
    event_map[0xea] = EVENT_REVERB_DEPTH;
    event_map[0xeb] = EVENT_REVERB_DEPTH_FADE;
    event_map[0xec] = EVENT_DRUM_ON_V1;
    event_map[0xed] = EVENT_DRUM_OFF;
    event_map[0xee] = EVENT_UNCONDITIONAL_JUMP;
    event_map[0xef] = EVENT_CPU_CONDITIONAL_JUMP;
    event_map[0xf0] = EVENT_LOOP_BRANCH;
    event_map[0xf1] = EVENT_LOOP_BREAK;
    event_map[0xf2] = EVENT_PROGCHANGE_NO_ATTACK;
    event_map[0xf3] = EVENT_F3_FF7;
    event_map[0xf4] = EVENT_OVERLAY_VOICE_ON;
    event_map[0xf5] = EVENT_OVERLAY_VOICE_OFF;
    event_map[0xf6] = EVENT_OVERLAY_VOLUME_BALANCE;
    event_map[0xf7] = EVENT_OVERLAY_VOLUME_BALANCE_FADE;
    event_map[0xf8] = EVENT_ALTERNATE_VOICE_ON;
    event_map[0xf9] = EVENT_ALTERNATE_VOICE_OFF;
    event_map[0xfa] = EVENT_UNIMPLEMENTED;
    event_map[0xfb] = EVENT_UNIMPLEMENTED;
    event_map[0xfc] = EVENT_UNIMPLEMENTED;
    event_map[0xfd] = EVENT_TIME_SIGNATURE;
    event_map[0xfe] = EVENT_MEASURE;
    event_map[0xff] = EVENT_UNIMPLEMENTED;

    if (version() == AkaoPs1Version::VERSION_1_1)
    {
      event_map[0xf3] = EVENT_F3_SAGAFRO;
      event_map[0xf4] = EVENT_UNIMPLEMENTED;
      event_map[0xf5] = EVENT_UNIMPLEMENTED;
      event_map[0xf6] = EVENT_UNIMPLEMENTED;
      event_map[0xf7] = EVENT_UNIMPLEMENTED;
      event_map[0xfc] = EVENT_PROGCHANGE_KEY_SPLIT_V1;
    }
  }
  else if (version() == AkaoPs1Version::VERSION_1_2 || version() == AkaoPs1Version::VERSION_2) {
    event_map[0xe0] = EVENT_E0;
    event_map[0xe1] = EVENT_UNIMPLEMENTED;
    event_map[0xe2] = EVENT_UNIMPLEMENTED;
    event_map[0xe3] = EVENT_UNIMPLEMENTED;
    event_map[0xe4] = EVENT_UNIMPLEMENTED;
    event_map[0xe5] = EVENT_UNIMPLEMENTED;
    event_map[0xe6] = EVENT_UNIMPLEMENTED;
    event_map[0xe7] = EVENT_UNIMPLEMENTED;
    event_map[0xe8] = EVENT_UNIMPLEMENTED;
    event_map[0xe9] = EVENT_UNIMPLEMENTED;
    event_map[0xea] = EVENT_UNIMPLEMENTED;
    event_map[0xeb] = EVENT_UNIMPLEMENTED;
    event_map[0xec] = EVENT_UNIMPLEMENTED;
    event_map[0xed] = EVENT_UNIMPLEMENTED;
    event_map[0xee] = EVENT_UNIMPLEMENTED;
    event_map[0xef] = EVENT_UNIMPLEMENTED;
    event_map[0xf0] = EVENT_UNIMPLEMENTED;
    event_map[0xf1] = EVENT_UNIMPLEMENTED;
    event_map[0xf2] = EVENT_UNIMPLEMENTED;
    event_map[0xf3] = EVENT_UNIMPLEMENTED;
    event_map[0xf4] = EVENT_UNIMPLEMENTED;
    event_map[0xf5] = EVENT_UNIMPLEMENTED;
    event_map[0xf6] = EVENT_UNIMPLEMENTED;
    event_map[0xf7] = EVENT_UNIMPLEMENTED;
    event_map[0xf8] = EVENT_UNIMPLEMENTED;
    event_map[0xf9] = EVENT_UNIMPLEMENTED;
    event_map[0xfa] = EVENT_UNIMPLEMENTED;
    event_map[0xfb] = EVENT_UNIMPLEMENTED;
    event_map[0xfc] = EVENT_UNIMPLEMENTED; // 0xfc: extra opcodes
    event_map[0xfd] = EVENT_UNIMPLEMENTED;
    event_map[0xfe] = EVENT_UNIMPLEMENTED;
    event_map[0xff] = EVENT_UNIMPLEMENTED;

    sub_event_map[0x00] = EVENT_TEMPO;
    sub_event_map[0x01] = EVENT_TEMPO_FADE;
    sub_event_map[0x02] = EVENT_REVERB_DEPTH;
    sub_event_map[0x03] = EVENT_REVERB_DEPTH_FADE;
    sub_event_map[0x04] = EVENT_DRUM_ON_V1;
    sub_event_map[0x05] = EVENT_DRUM_OFF;
    sub_event_map[0x06] = EVENT_UNCONDITIONAL_JUMP;
    sub_event_map[0x07] = EVENT_CPU_CONDITIONAL_JUMP;
    sub_event_map[0x08] = EVENT_LOOP_BRANCH;
    sub_event_map[0x09] = EVENT_LOOP_BREAK;
    sub_event_map[0x0a] = EVENT_PROGCHANGE_NO_ATTACK;
    sub_event_map[0x0b] = EVENT_FC_0B;
    sub_event_map[0x0c] = EVENT_FC_0C;
    sub_event_map[0x0d] = EVENT_FC_0D;
    sub_event_map[0x0e] = EVENT_FC_0E;
    sub_event_map[0x0f] = EVENT_FC_0F;
    sub_event_map[0x10] = EVENT_FC_10;
    sub_event_map[0x11] = EVENT_FC_11;
    sub_event_map[0x12] = EVENT_VOLUME_FADE;
    sub_event_map[0x13] = EVENT_UNIMPLEMENTED;
    sub_event_map[0x14] = EVENT_PROGCHANGE_KEY_SPLIT_V1;
    sub_event_map[0x15] = EVENT_TIME_SIGNATURE;
    sub_event_map[0x16] = EVENT_MEASURE;
    sub_event_map[0x17] = EVENT_FC_17;
    sub_event_map[0x18] = EVENT_FC_18;
    sub_event_map[0x19] = EVENT_UNIMPLEMENTED;
    sub_event_map[0x1a] = EVENT_UNIMPLEMENTED;
    sub_event_map[0x1b] = EVENT_UNIMPLEMENTED;
    sub_event_map[0x1c] = EVENT_UNIMPLEMENTED;
    sub_event_map[0x1d] = EVENT_UNIMPLEMENTED;
    sub_event_map[0x1e] = EVENT_UNIMPLEMENTED;
    sub_event_map[0x1f] = EVENT_UNIMPLEMENTED;
  }
  else if (version() == AkaoPs1Version::VERSION_3_0 || version() == AkaoPs1Version::VERSION_3_1) {
    event_map[0xe0] = EVENT_E0;
    event_map[0xe1] = EVENT_UNIMPLEMENTED;
    event_map[0xe2] = EVENT_UNIMPLEMENTED;
    event_map[0xe3] = EVENT_UNIMPLEMENTED;
    event_map[0xe4] = EVENT_UNIMPLEMENTED;
    event_map[0xe5] = EVENT_UNIMPLEMENTED;
    event_map[0xe6] = EVENT_UNIMPLEMENTED;
    event_map[0xe7] = EVENT_UNIMPLEMENTED;
    event_map[0xe8] = EVENT_UNIMPLEMENTED;
    event_map[0xe9] = EVENT_UNIMPLEMENTED;
    event_map[0xea] = EVENT_UNIMPLEMENTED;
    event_map[0xeb] = EVENT_UNIMPLEMENTED;
    event_map[0xec] = EVENT_UNIMPLEMENTED;
    event_map[0xed] = EVENT_UNIMPLEMENTED;
    event_map[0xee] = EVENT_UNIMPLEMENTED;
    event_map[0xef] = EVENT_UNIMPLEMENTED;
    // 0xf0-0xfd: note with duration
    // 0xfe: extra opcodes
    event_map[0xff] = EVENT_UNIMPLEMENTED;

    sub_event_map[0x00] = EVENT_TEMPO;
    sub_event_map[0x01] = EVENT_TEMPO_FADE;
    sub_event_map[0x02] = EVENT_REVERB_DEPTH;
    sub_event_map[0x03] = EVENT_REVERB_DEPTH_FADE;
    sub_event_map[0x04] = EVENT_DRUM_ON_V2;
    sub_event_map[0x05] = EVENT_DRUM_OFF;
    sub_event_map[0x06] = EVENT_UNCONDITIONAL_JUMP;
    sub_event_map[0x07] = EVENT_CPU_CONDITIONAL_JUMP;
    sub_event_map[0x08] = EVENT_LOOP_BRANCH;
    sub_event_map[0x09] = EVENT_LOOP_BREAK;
    sub_event_map[0x0a] = EVENT_PROGCHANGE_NO_ATTACK;
    sub_event_map[0x0b] = EVENT_UNIMPLEMENTED; // Same as 0xFC 0x0B in former version. Aside from that, it seems that Chrono Cross assigns a different operation for this opcode than other games
    sub_event_map[0x0c] = EVENT_UNIMPLEMENTED; // Formerly used in Chocobo Dungeon but not implemented in this later version. Aside from that, Vagrant Story uses this opcode for a different operation (See 0x8001B70C)
    sub_event_map[0x0d] = EVENT_UNIMPLEMENTED; // Formerly used in Chocobo Dungeon but not implemented in this later version
    sub_event_map[0x0e] = EVENT_FC_0E;
    sub_event_map[0x0f] = EVENT_FC_0F;
    sub_event_map[0x10] = EVENT_ALLOC_RESERVED_VOICES; // Possible having different implementation before Chocobo Racing
    sub_event_map[0x11] = EVENT_FREE_RESERVED_VOICES; // Possible having different implementation before Chocobo Racing
    sub_event_map[0x12] = EVENT_VOLUME_FADE;
    sub_event_map[0x13] = EVENT_UNIMPLEMENTED;
    sub_event_map[0x14] = EVENT_PROGCHANGE_KEY_SPLIT_V2;
    sub_event_map[0x15] = EVENT_TIME_SIGNATURE;
    sub_event_map[0x16] = EVENT_MEASURE;
    sub_event_map[0x17] = EVENT_FC_17;
    sub_event_map[0x18] = EVENT_FC_18;
    sub_event_map[0x19] = EVENT_EXPRESSION_FADE_PER_NOTE;
    sub_event_map[0x1a] = EVENT_FE_1A;
    sub_event_map[0x1b] = EVENT_FE_1B;
    sub_event_map[0x1c] = EVENT_FE_1C; // ommitted since SaGa Frontier 2
    sub_event_map[0x1d] = EVENT_USE_RESERVED_VOICES; // Possible having different implementation before Chocobo Racing
    sub_event_map[0x1e] = EVENT_USE_NO_RESERVED_VOICES; // Possible having different implementation before Chocobo Racing
    sub_event_map[0x1f] = EVENT_UNIMPLEMENTED;
  }
  else if (version() == AkaoPs1Version::VERSION_3_2) {
    event_map[0xe0] = EVENT_E0;
    event_map[0xe1] = EVENT_E1;
    event_map[0xe2] = EVENT_E2;
    event_map[0xe3] = EVENT_UNIMPLEMENTED;
    event_map[0xe4] = EVENT_VIBRATO_RATE_FADE;
    event_map[0xe5] = EVENT_TREMOLO_RATE_FADE;
    event_map[0xe6] = EVENT_PAN_LFO_RATE_FADE;
    event_map[0xe7] = EVENT_UNIMPLEMENTED;
    event_map[0xe8] = EVENT_UNIMPLEMENTED;
    event_map[0xe9] = EVENT_UNIMPLEMENTED;
    event_map[0xea] = EVENT_UNIMPLEMENTED;
    event_map[0xeb] = EVENT_UNIMPLEMENTED;
    event_map[0xec] = EVENT_UNIMPLEMENTED;
    event_map[0xed] = EVENT_UNIMPLEMENTED;
    event_map[0xee] = EVENT_UNIMPLEMENTED;
    event_map[0xef] = EVENT_UNIMPLEMENTED;
    // 0xf0-0xfd: note with duration
    // 0xfe: extra opcodes
    event_map[0xff] = EVENT_UNIMPLEMENTED;

    sub_event_map[0x00] = EVENT_TEMPO;
    sub_event_map[0x01] = EVENT_TEMPO_FADE;
    sub_event_map[0x02] = EVENT_REVERB_DEPTH;
    sub_event_map[0x03] = EVENT_REVERB_DEPTH_FADE;
    sub_event_map[0x04] = EVENT_DRUM_ON_V2;
    sub_event_map[0x05] = EVENT_DRUM_OFF;
    sub_event_map[0x06] = EVENT_UNCONDITIONAL_JUMP;
    sub_event_map[0x07] = EVENT_CPU_CONDITIONAL_JUMP;
    sub_event_map[0x08] = EVENT_LOOP_BRANCH;
    sub_event_map[0x09] = EVENT_LOOP_BREAK;
    sub_event_map[0x0a] = EVENT_PROGCHANGE_NO_ATTACK;
    sub_event_map[0x0b] = EVENT_UNIMPLEMENTED; // Same as 0xFC 0x0B in former version. Aside from that, it seems that Chrono Cross assigns a different operation for this opcode than other games
    sub_event_map[0x0c] = EVENT_UNIMPLEMENTED; // Generally not implemented, but Vagrant Story uses this opcode for a different operation from the former version (See 0x8001B70C)
    sub_event_map[0x0d] = EVENT_UNIMPLEMENTED;
    sub_event_map[0x0e] = EVENT_PATTERN;
    sub_event_map[0x0f] = EVENT_END_PATTERN;
    sub_event_map[0x10] = EVENT_ALLOC_RESERVED_VOICES;
    sub_event_map[0x11] = EVENT_FREE_RESERVED_VOICES;
    sub_event_map[0x12] = EVENT_VOLUME_FADE;
    sub_event_map[0x13] = EVENT_FE_13_CHRONO_CROSS; // used only in Chrono Cross
    sub_event_map[0x14] = EVENT_PROGCHANGE_KEY_SPLIT_V2;
    sub_event_map[0x15] = EVENT_TIME_SIGNATURE;
    sub_event_map[0x16] = EVENT_MEASURE;
    sub_event_map[0x17] = EVENT_FC_17; // ommitted since Chrono Cross
    sub_event_map[0x18] = EVENT_FC_18; // ommitted since Chrono Cross
    sub_event_map[0x19] = EVENT_EXPRESSION_FADE_PER_NOTE;
    sub_event_map[0x1a] = EVENT_FE_1A;
    sub_event_map[0x1b] = EVENT_FE_1B;
    sub_event_map[0x1c] = EVENT_FE_1C; // Generally not implemented, but Vagrant Story uses this opcode (for a different operation from the former version?)
    sub_event_map[0x1d] = EVENT_USE_RESERVED_VOICES;
    sub_event_map[0x1e] = EVENT_USE_NO_RESERVED_VOICES;
    sub_event_map[0x1f] = EVENT_UNIMPLEMENTED;
  }
}


AkaoTrack::AkaoTrack(AkaoSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  AkaoTrack::resetVars();
}

void AkaoTrack::resetVars() {
  SeqTrack::resetVars();

  slur = false;
  legato = false;
  portamento = false;
  drum = false;

  pattern_return_offset = 0;

  std::ranges::fill(loop_begin_loc, 0);
  loop_layer = 0;
  std::ranges::fill(loop_counter, 0);

  last_delta_time = 0;
  use_one_time_delta_time = false;
  one_time_delta_time = 0;
  delta_time_overwrite = 0;
  tuning = 0;

  conditional_jump_destinations.clear();
}

SeqTrack::State AkaoTrack::readEvent() {
  AkaoSeq *parentSeq = seq();
  const AkaoPs1Version version = parentSeq->version();
  const uint32_t beginOffset = curOffset;
  const uint8_t status_byte = readByte(curOffset++);

  const bool op_note_with_length = (version >= AkaoPs1Version::VERSION_3_0)
    && (status_byte >= 0xF0) && (status_byte <= 0xFD);

  if (status_byte <= 0x99 || op_note_with_length)   //it's either a  note-on message, a tie message, or a rest message
  {
    const uint8_t note_byte = op_note_with_length
      ? ((status_byte - 0xF0) * 11)
      : status_byte;

    const bool op_rest = note_byte >= 0x8F;
    const bool op_tie = !op_rest && note_byte >= 0x83;
    const bool op_note = !op_rest && !op_tie;
    const auto delta_time_from_op = static_cast<uint8_t>(DELTA_TIME_TABLE[note_byte % 11]);

    uint8_t delta_time = 0;
    if (op_note_with_length)
      delta_time = readByte(curOffset++);
    if (use_one_time_delta_time) {
      delta_time = one_time_delta_time;
      use_one_time_delta_time = false;
    }
    if (delta_time_overwrite != 0)
      delta_time = static_cast<uint8_t>(delta_time_overwrite);
    if (delta_time == 0)
      delta_time = delta_time_from_op;

    uint8_t dur = delta_time;
    if (version < AkaoPs1Version::VERSION_3_0 && !slur && !legato) // and the next note event is not op_tie
      dur = static_cast<uint8_t>(max(static_cast<int>(dur) - 2, 0));

    last_delta_time = delta_time;

    if (op_note)
    {
      const uint8_t relative_key = note_byte / 11;
      const uint8_t real_key = (octave * 12) + relative_key;

      // in earlier verion, drum instrument will ignore the octave number
      uint8_t key = real_key;
      if (version <= AkaoPs1Version::VERSION_2) {
        constexpr uint8_t drum_octave = 2;
        key = drum ? (drum_octave * 12) + relative_key : real_key;
      }

      if (portamento) {
        addPortamentoControlNoItem(prevKey);
      }
      addNoteByDur(beginOffset, curOffset - beginOffset, key, NOTE_VELOCITY, dur);
      addTime(delta_time);
    }
    else if (op_tie)
    {
      makePrevDurNoteEnd(getTime() + dur);
      addTime(delta_time);
      auto desc = fmt::format("Length: {}  Duration: {}", delta_time, dur);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc, Type::Tie);
    }
    else // rest
    {
      addRest(beginOffset, curOffset - beginOffset, delta_time);
    }
  }
  else if ((status_byte >= 0x9A) && (status_byte <= 0x9F)) {
    addUnknown(beginOffset, curOffset - beginOffset, "Undefined");
    return State::Finished; // they should not be used
  }
  else {
    auto event = static_cast<AkaoSeqEventType>(0);

    if (version >= AkaoPs1Version::VERSION_3_0 && status_byte == 0xFE)
    {
      const uint8_t op = readByte(curOffset++);
      const auto event_iterator = parentSeq->sub_event_map.find(op);
      if (event_iterator != parentSeq->sub_event_map.end())
        event = event_iterator->second;
    }
    else if ((version == AkaoPs1Version::VERSION_1_2 || version == AkaoPs1Version::VERSION_2) && status_byte == 0xFC)
    {
      const uint8_t op = readByte(curOffset++);
      const auto event_iterator = parentSeq->sub_event_map.find(op);
      if (event_iterator != parentSeq->sub_event_map.end())
        event = event_iterator->second;
    }
    else
    {
      const auto event_iterator = parentSeq->event_map.find(status_byte);
      if (event_iterator != parentSeq->event_map.end())
        event = event_iterator->second;
    }

    switch (event) {
    case EVENT_END:
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      return State::Finished;

    case EVENT_PROGCHANGE: {
      // change program to articulation number
      parentSeq->bUsesIndividualArts = true;
      const uint8_t artNum = readByte(curOffset++);
      addBankSelectNoItem(0);
      addProgramChange(beginOffset, curOffset - beginOffset, artNum);
      break;
    }

    case EVENT_ONE_TIME_DURATION: {
      const uint8_t delta_time = readByte(curOffset++);
      last_delta_time = one_time_delta_time = delta_time;
      use_one_time_delta_time = true;
      auto desc = fmt::format("Length: {}", delta_time);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Next Note Length", desc, Type::DurationChange);
      break;
    }

    case EVENT_VOLUME: {
      const uint8_t vol = readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, vol);
      break;
    }

    case EVENT_VOLUME_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t vol = readByte(curOffset++);
      addVolSlide(beginOffset, curOffset - beginOffset, length, vol);
      break;
    }

    case EVENT_PITCH_SLIDE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const int8_t semitones = readByte(curOffset++);
      auto desc = fmt::format("Length: {}  Key: {} semitones", length, semitones);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide", desc, Type::PitchBendSlide);
      break;
    }

    case EVENT_OCTAVE: {
      const uint8_t new_octave = readByte(curOffset++);
      addSetOctave(beginOffset, curOffset - beginOffset, new_octave);
      break;
    }

    case EVENT_INCREMENT_OCTAVE:
      // The formula below is more accurate implementation of this (at least in FF7), but who cares?
      //octave = (octave + 1) & 0xf;
      addIncrementOctave(beginOffset, curOffset - beginOffset);
      break;

    case EVENT_DECREMENT_OCTAVE:
      // The formula below is more accurate implementation of this (at least in FF7), but who cares?
      //octave = (octave - 1) & 0xf;
      addDecrementOctave(beginOffset, curOffset - beginOffset);
      break;

    case EVENT_EXPRESSION: {
      const uint8_t expression = readByte(curOffset++);
      addExpression(beginOffset, curOffset - beginOffset, expression);
      break;
    }

    case EVENT_EXPRESSION_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t expression = readByte(curOffset++);
      addExpressionSlide(beginOffset, curOffset - beginOffset, length, expression);
      break;
    }

    case EVENT_EXPRESSION_FADE_PER_NOTE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t expression = readByte(curOffset++);
      auto desc = fmt::format("Target Expression: {}  Duration: {}", expression, length);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Expression Slide Per Note",
        desc, Type::ExpressionSlide);
      break;
    }

    case EVENT_PAN: {
      // TODO: volume balance conversion
      const uint8_t pan = readByte(curOffset++); // 0-127
      addPan(beginOffset, curOffset - beginOffset, pan);
      break;
    }

    case EVENT_PAN_FADE: {
      // TODO: volume balance conversion
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t pan = readByte(curOffset++);
      addPanSlide(beginOffset, curOffset - beginOffset, length, pan);
      break;
    }

    case EVENT_NOISE_CLOCK: {
      const uint8_t raw_clock = readByte(curOffset++);
      const bool relative = (raw_clock & 0xc0) != 0;
      std::string desc;
      if (relative)
      {
        const uint8_t clock = raw_clock & 0x3f;
        const int8_t signed_clock = ((clock & 0x20) != 0) ? (0x20 - (clock & 0x1f)) : clock;
        desc = fmt::format("Clock: {} (Relative)", signed_clock);
      }
      else
      {
        const uint8_t clock = raw_clock & 0x3f;
        desc = fmt::format("Clock: {}", clock);
      }
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Clock", desc, Type::ChangeState);
      break;
    }

    case EVENT_ADSR_ATTACK_RATE: {
      const uint8_t ar = readByte(curOffset++); // 0-127
      auto desc = fmt::format("AR: {}", ar);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Attack Rate", desc, Type::Adsr);
      break;
    }

    case EVENT_ADSR_DECAY_RATE: {
      const uint8_t dr = readByte(curOffset++); // 0-15
      auto desc = fmt::format("DR: {}", dr);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Decay Rate", desc, Type::Adsr);
      break;
    }

    case EVENT_ADSR_SUSTAIN_LEVEL: {
      const uint8_t sl = readByte(curOffset++); // 0-15
      auto desc = fmt::format("SL: {}", sl);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Sustain Level", desc, Type::Adsr);
      break;
    }

    case EVENT_ADSR_DECAY_RATE_AND_SUSTAIN_LEVEL: {
      const uint8_t dr = readByte(curOffset++); // 0-15
      const uint8_t sl = readByte(curOffset++); // 0-15
      auto desc = fmt::format("DR: {}  SL: {}", dr, sl);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Decay Rate & Sustain Level", desc, Type::Adsr);
      break;
    }

    case EVENT_ADSR_SUSTAIN_RATE: {
      const uint8_t sr = readByte(curOffset++); // 0-127
      auto desc = fmt::format("SR: {}", sr);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Sustain Rate", desc, Type::Adsr);
      break;
    }

    case EVENT_ADSR_RELEASE_RATE: {
      const uint8_t rr = readByte(curOffset++); // 0-127
      auto desc = fmt::format("RR: {}", rr);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Release Rate", desc, Type::Adsr);
      break;
    }

    case EVENT_RESET_ADSR:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Reset ADSR", "", Type::Adsr);
      break;

    case EVENT_VIBRATO: {
      const uint8_t delay = readByte(curOffset++);
      const uint8_t raw_rate = readByte(curOffset++);
      const uint16_t rate = raw_rate == 0 ? 256 : raw_rate;
      const uint8_t type = readByte(curOffset++); // 0-15
      auto desc = fmt::format("Delay: {}  Rate: {}  LFO Type: {}", delay, rate, type);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO_DEPTH: {
      const uint8_t raw_depth = readByte(curOffset++);
      const uint8_t depth = raw_depth & 0x7f;
      const uint8_t depth_type = (raw_depth & 0x80) >> 7;
      auto desc = depth_type == 0
           ? fmt::format("Depth: {} (15/256 scale, approx 50 cents at maximum)", depth)
           : fmt::format("Depth: {} (1/1 scale, approx 700 cents at maximum)", depth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Depth", desc, Type::Vibrato);
      break;
    }

    case EVENT_VIBRATO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Off", "", Type::Vibrato);
      break;

    case EVENT_ADSR_ATTACK_MODE: {
      const uint8_t ar_mode = readByte(curOffset++);
      auto desc = fmt::format("Mode: {}", ar_mode);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Attack Rate Mode", desc, Type::Adsr);
      break;
    }

    case EVENT_TREMOLO: {
      const uint8_t delay = readByte(curOffset++);
      const uint8_t raw_rate = readByte(curOffset++);
      const uint16_t rate = raw_rate == 0 ? 256 : raw_rate;
      const uint8_t type = readByte(curOffset++); // 0-15
      auto desc = fmt::format("Delay: {}  Rate: {}  LFO Type: {}", delay, rate, type);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo", desc, Type::Tremelo);
      break;
    }

    case EVENT_TREMOLO_DEPTH: {
      const uint8_t depth = readByte(curOffset++);
      auto desc = fmt::format("Depth: {}", depth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Depth", desc, Type::Tremelo);
      break;
    }

    case EVENT_TREMOLO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Off", "", Type::Tremelo);
      break;

    case EVENT_ADSR_SUSTAIN_MODE: {
      const uint8_t sr_mode = readByte(curOffset++);
      auto desc = fmt::format("Mode: {}", sr_mode);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Sustain Rate Mode", desc, Type::Adsr);
      break;
    }

    case EVENT_PAN_LFO: {
      const uint8_t raw_rate = readByte(curOffset++);
      const uint16_t rate = raw_rate == 0 ? 256 : raw_rate;
      const uint8_t type = readByte(curOffset++); // 0-15
      auto desc = fmt::format("Rate: {}  LFO Type: {}", rate, type);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO", desc, Type::PanLfo);
      break;
    }

    case EVENT_PAN_LFO_DEPTH: {
      const uint8_t depth = readByte(curOffset++);
      auto desc = fmt::format("Depth: {}", depth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO Depth", desc, Type::PanLfo);
      break;
    }

    case EVENT_PAN_LFO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO Off", "", Type::PanLfo);
      break;

    case EVENT_ADSR_RELEASE_MODE: {
      const uint8_t rr_mode = readByte(curOffset++);
      auto desc = fmt::format("Mode: {}", rr_mode);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR Release Rate Mode", desc, Type::Adsr);
      break;
    }

    case EVENT_TRANSPOSE_ABS: {
      const int8_t key = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, key);
      break;
    }

    case EVENT_TRANSPOSE_REL: {
      const int8_t key = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, transpose + key, "Transpose (Relative)");
      break;
    }

    case EVENT_REVERB_ON:
      // TODO: reverb control
      addGenericEvent(beginOffset, curOffset - beginOffset, "Reverb On", "", Type::Reverb);
      break;

    case EVENT_REVERB_OFF:
      // TODO: reverb control
      addGenericEvent(beginOffset, curOffset - beginOffset, "Reverb Off", "", Type::Reverb);
      break;

    case EVENT_NOISE_ON:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise On", "", Type::Noise);
      break;

    case EVENT_NOISE_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Off", "", Type::Noise);
      break;

    case EVENT_PITCH_MOD_ON:
      addGenericEvent(beginOffset, curOffset - beginOffset, "FM (Pitch LFO) On", "", Type::Modulation);
      break;

    case EVENT_PITCH_MOD_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "FM (Pitch LFO) Off", "", Type::Modulation);
      break;

    case EVENT_LOOP_START:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Start", "", Type::RepeatStart);

      loop_layer = (loop_layer + 1) & 3;
      loop_begin_loc[loop_layer] = curOffset;
      loop_counter[loop_layer] = 0;
      break;

    case EVENT_LOOP_UNTIL: {
      const uint8_t raw_count = readByte(curOffset++);
      const uint16_t count = raw_count == 0 ? 256 : raw_count;

      auto desc = fmt::format("Count: {}", count);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Until", desc, Type::RepeatEnd);

      loop_counter[loop_layer]++;
      if (loop_counter[loop_layer] == count)
      {
        loop_layer = (loop_layer - 1) & 3;
      }
      else
      {
        curOffset = loop_begin_loc[loop_layer];
      }
      break;
    }

    case EVENT_LOOP_AGAIN: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Again", "", Type::Loop);

      loop_counter[loop_layer]++;
      curOffset = loop_begin_loc[loop_layer];
      break;
    }

    case EVENT_RESET_VOICE_EFFECTS:
      // Reset noise, FM modulation, reverb, etc.
      addGenericEvent(beginOffset, curOffset - beginOffset, "Reset Voice Effects", "", Type::ChangeState);
      break;

    case EVENT_SLUR_ON:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Slur On (No Key Off, No Retrigger)", "", Type::ChangeState);
      slur = true;
      break;

    case EVENT_SLUR_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Slur Off", "", Type::ChangeState);
      slur = false;
      break;

    case EVENT_NOISE_ON_DELAY_TOGGLE: {
      const uint8_t raw_delay = readByte(curOffset++);
      const uint16_t delay = raw_delay == 0 ? 1 : raw_delay + 1;
      auto desc = fmt::format("Delay: {}", delay);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise On & Delay Switching", desc, Type::Noise);
      break;
    }

    case EVENT_NOISE_DELAY_TOGGLE: {
      const uint8_t raw_delay = readByte(curOffset++);
      const uint16_t delay = raw_delay == 0 ? 1 : raw_delay + 1;
      auto desc = fmt::format("Delay: {}", delay);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise On/Off Delay Switching", desc, Type::Noise);
      break;
    }

    case EVENT_LEGATO_ON:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Legato On (No Key Off)", "", Type::ChangeState);
      legato = true;
      break;

    case EVENT_LEGATO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Legato Off", "", Type::ChangeState);
      legato = false;
      break;

    case EVENT_PITCH_MOD_ON_DELAY_TOGGLE: {
      const uint8_t raw_delay = readByte(curOffset++);
      const uint16_t delay = raw_delay == 0 ? 1 : raw_delay + 1;
      auto desc = fmt::format("Delay: {}", delay);
      addGenericEvent(beginOffset, curOffset - beginOffset, "FM (Pitch LFO) On & Delay Switching", desc, Type::ProgramChange);
      break;
    }

    case EVENT_PITCH_MOD_DELAY_TOGGLE: {
      const uint8_t raw_delay = readByte(curOffset++);
      const uint16_t delay = raw_delay == 0 ? 1 : raw_delay + 1;
      auto desc = fmt::format("Delay: {}", delay);
      addGenericEvent(beginOffset, curOffset - beginOffset, "FM (Pitch LFO) On/Off Delay Switching", desc, Type::ProgramChange);
      break;
    }

    case EVENT_PITCH_SIDE_CHAIN_ON:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Side Chain On", "", Type::Misc);
      break;

    case EVENT_PITCH_SIDE_CHAIN_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Side Chain Off", "", Type::Misc);
      break;

    case EVENT_PITCH_TO_VOLUME_SIDE_CHAIN_ON:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch-Volume Side Chain On", "", Type::Misc);
      break;

    case EVENT_PITCH_TO_VOLUME_SIDE_CHAIN_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch-Volume Side Chain Off", "", Type::Misc);
      break;

    case EVENT_TUNING_ABS:
    {
      // signed data byte.  range of 1 octave (0x7F = +1 octave, 0x80 = -1 octave)
      tuning = readByte(curOffset++);

      const int div = tuning >= 0 ? 128 : 256;
      const double scale = tuning / static_cast<double>(div);
      addPitchBendAsPercent(beginOffset, curOffset-beginOffset, scale / log(2.0));
      break;
    }

    case EVENT_TUNING_REL: {
      const int8_t relative_tuning = readByte(curOffset++);
      tuning += relative_tuning;

      const int div = tuning >= 0 ? 128 : 256;
      const double scale = tuning / static_cast<double>(div);
      const double cents = (scale / log(2.0)) * 1200.0;
      auto desc = fmt::format(
"Amount: {}  Tuning: {} cents ({}/{})",
        relative_tuning, cents, tuning, div
      );
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tuning (Relative)", desc, Type::FineTune);
      addFineTuningNoItem(cents);
      break;
    }

    case EVENT_PORTAMENTO_ON: {
      const uint8_t raw_speed = readByte(curOffset++);
      const uint16_t speed = raw_speed == 0 ? 256 : raw_speed;
      auto desc = fmt::format("Time: {} tick(s)", speed);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento", desc, Type::Portamento);
      double millisPerTick = 60000.0 / (parentSeq->tempoBPM * parentSeq->ppqn());
      addPortamentoTime14BitNoItem(millisPerTick * speed);
      addPortamentoNoItem(true);
      portamento = true;
      break;
    }

    case EVENT_PORTAMENTO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento Off", "", Type::Portamento);
      addPortamentoNoItem(false);
      portamento = false;
      break;
    }

    case EVENT_FIXED_DURATION: {
      const int8_t relative_length = readByte(curOffset++);
      const int16_t length = std::min(std::max(last_delta_time + relative_length, 1), 255);
      delta_time_overwrite = length;
      auto desc = fmt::format("Duration (Relative Amount): {}", relative_length);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Fixed Note Length", desc, Type::DurationChange);
      break;
    }

    case EVENT_VIBRATO_DEPTH_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t raw_depth = readByte(curOffset++);
      const uint8_t depth = raw_depth & 0x7f;
      const uint8_t depth_type = (raw_depth & 0x80) >> 7;
      auto desc = depth_type == 0
           ? fmt::format("Duration: {}  Target Depth: {} (15/256 scale, approx 50 cents at maximum)", length, depth)
           : fmt::format("Duration: {}  Target Depth: {} (1/1 scale, approx 700 cents at maximum)", length, depth);

      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Depth Slide", desc, Type::Lfo);
      break;
    }

    case EVENT_TREMOLO_DEPTH_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t depth = readByte(curOffset++);
      auto desc = fmt::format("Duration: {}  Target Depth: {}", length, depth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Depth Slide", desc, Type::Lfo);
      break;
    }

    case EVENT_PAN_LFO_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t depth = readByte(curOffset++);
      auto desc = fmt::format("Duration: {}  Target Depth: {}", length, depth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO Depth Slide", desc, Type::Lfo);
      break;
    }

    case EVENT_VIBRATO_RATE_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t rate = readByte(curOffset++);
      auto desc = fmt::format("Duration: {}  Target Rate: {}", length, rate);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Rate Slide", desc, Type::Lfo);
      break;
    }

    case EVENT_TREMOLO_RATE_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t rate = readByte(curOffset++);
      auto desc = fmt::format("Duration: {}  Target Rate: {}", length, rate);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Rate Slide", desc, Type::Lfo);
      break;
    }

    case EVENT_PAN_LFO_RATE_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t rate = readByte(curOffset++);
      auto desc = fmt::format("Duration: {}  Target Rate: {}", length, rate);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO Rate Slide", desc, Type::Lfo);
      break;
    }

    case EVENT_E0: {
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_E1: {
      curOffset++;
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_E2: {
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_TEMPO: {
      const uint16_t raw_tempo = readShort(curOffset);
      curOffset += 2;

      const double bpm = parentSeq->getTempoInBPM(raw_tempo);
      addTempoBPM(beginOffset, curOffset - beginOffset, bpm);
      break;
    }

    case EVENT_TEMPO_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint16_t raw_tempo = readShort(curOffset);
      curOffset += 2;

      const double bpm = parentSeq->getTempoInBPM(raw_tempo);
      addTempoBPMSlide(beginOffset, curOffset - beginOffset, length, bpm);
      break;
    }

    case EVENT_REVERB_DEPTH: {
      const int16_t depth = readShort(curOffset);
      curOffset += 2;

      auto desc = fmt::format("Depth: {}", depth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Reverb Depth", desc, Type::Reverb);
      break;
    }

    case EVENT_REVERB_DEPTH_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const int16_t depth = readShort(curOffset);
      curOffset += 2;

      auto desc = fmt::format("Duration: {}  Target Depth: {}", length, depth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Reverb Depth Slide", desc, Type::Reverb);
      break;
    }

    case EVENT_DRUM_ON_V1: {
      const int16_t relative_drum_offset = readShort(curOffset);
      curOffset += 2;
      const uint32_t drum_instrset_offset = curOffset + relative_drum_offset;

      auto d = fmt::format("Offset: 0x{:X}", drum_instrset_offset);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Drum Kit On", d, Type::UseDrumKit);

      if (readMode == READMODE_ADD_TO_UI) {
        parentSeq->drum_instrument_addresses.insert(drum_instrset_offset);
      } else {
        const auto instrument_index = static_cast<uint8_t>(
            std::distance(parentSeq->drum_instrument_addresses.begin(),
                          parentSeq->drum_instrument_addresses.find(drum_instrset_offset)));

        addBankSelectNoItem(127 - instrument_index);
        addProgramChangeNoItem(127, false);
        drum = true;
        // channel = 9;
      }

      break;
    }

    case EVENT_DRUM_ON_V2: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Drum Kit On", "", Type::UseDrumKit);
      addBankSelectNoItem(127);
      addProgramChangeNoItem(127, false);
      drum = true;
      //channel = 9;
      break;
    }

    case EVENT_DRUM_OFF:
      // TODO: restore program change for regular instrument
      addGenericEvent(beginOffset, curOffset - beginOffset, "Drum Kit Off", "", Type::UseDrumKit);
      drum = false;
      break;

    case EVENT_UNCONDITIONAL_JUMP: {
      const int16_t relative_offset = readShort(curOffset);
      const uint32_t dest = curOffset + relative_offset + (version >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
      curOffset += 2;
      const uint32_t length = curOffset - beginOffset;

      auto d = fmt::format("Destination: 0x{:X}", dest);
      curOffset = dest;

      if (!isOffsetUsed(dest)) {
        addGenericEvent(beginOffset, length, "Jump", d, Type::LoopForever);
      }
      else {
        return addLoopForever(beginOffset, length, "Jump");
      }
      break;
    }

    case EVENT_CPU_CONDITIONAL_JUMP: {
      const uint8_t target_value = readByte(curOffset++);
      const int16_t relative_offset = readShort(curOffset);
      const uint32_t dest = curOffset + relative_offset + (version >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
      curOffset += 2;
      const uint32_t length = curOffset - beginOffset;

      auto desc = fmt::format("Conditional Value {}  Destination: 0x{:X}", target_value, dest);

      if (readMode == READMODE_ADD_TO_UI) {
        // This event performs conditional jump if certain CPU variable matches to the condValue.
        // VGMTrans will simply try to parse all events as far as possible, instead.
        // (Test case: FF9 416 Final Battle)
        if (!isOffsetUsed(beginOffset)) {
          // For the first time, VGMTrans just skips the event,
          // but remembers the destination address for future jump.
          conditional_jump_destinations.push_back(dest);
        }
        else {
          // For the second time, VGMTrans jumps to the destination address.
          curOffset = dest;
        }
      } else {
        if (parentSeq->condition == target_value)
          curOffset = dest;
      }

      addGenericEvent(beginOffset, length, "CPU-Conditional Jump", desc, Type::Loop);
      break;
    }

    case EVENT_LOOP_BRANCH: {
      const uint8_t raw_count = readByte(curOffset++);
      const uint16_t count = raw_count == 0 ? 256 : raw_count;
      const int16_t relative_offset = readShort(curOffset);
      const uint32_t dest = curOffset + relative_offset + (version >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
      curOffset += 2;
      const uint32_t length = curOffset - beginOffset;

      auto desc = fmt::format("Count: {}  Destination: 0x{:X}", count, dest);
      addGenericEvent(beginOffset, length, "Repeat Branch", desc, Type::Misc);

      if (loop_counter[loop_layer] + 1 == count)
        curOffset = dest;

      break;
    }

    case EVENT_LOOP_BREAK: {
      const uint8_t raw_count = readByte(curOffset++);
      const uint16_t count = raw_count == 0 ? 256 : raw_count;
      const int16_t relative_offset = readShort(curOffset);
      const uint32_t dest = curOffset + relative_offset + (version >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
      curOffset += 2;
      const uint32_t length = curOffset - beginOffset;
      auto desc = fmt::format("Count {}  Destination: 0x{:X}", count, dest);

      addGenericEvent(beginOffset, length, "Repeat Break", desc, Type::Misc);

      if (loop_counter[loop_layer] + 1 == count)
        curOffset = dest;

      loop_layer = (loop_layer - 1) & 3;
      break;
    }

    case EVENT_PROGCHANGE_NO_ATTACK: {
      parentSeq->bUsesIndividualArts = true;
      const uint8_t artNum = readByte(curOffset++);
      addBankSelectNoItem(0);
      addProgramChange(beginOffset, curOffset - beginOffset, artNum, false, "Program Change w/o Attack Sample");
      break;
    }

    case EVENT_F3_FF7: {
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_F3_SAGAFRO: {
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_OVERLAY_VOICE_ON: {
      parentSeq->bUsesIndividualArts = true;
      const uint8_t artNum = readByte(curOffset++);
      const uint8_t artNum2 = readByte(curOffset++);

      auto desc = fmt::format(
        "Program Number for Primary Voice: {}  Program Number for Secondary Voice: {}",
        artNum, artNum2
      );
      addGenericEvent(beginOffset, curOffset - beginOffset, "Overlay Voice On (With Program Change)", desc, Type::ProgramChange);
      addBankSelectNoItem(0);
      addProgramChangeNoItem(artNum, false);
      break;
    }

    case EVENT_OVERLAY_VOICE_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Overlay Voice Off", "", Type::Misc);
      break;
    }

    case EVENT_OVERLAY_VOLUME_BALANCE: {
      const uint8_t balance = readByte(curOffset++);
      const int primary_percent = (127 - balance) * 100 / 256;
      const int secondary_percent = balance * 100 / 256;
      auto desc = fmt::format(
        "Balance: {} (Primary Voice Volume {}%, Secondary Voice Volume {}%)",
        balance, primary_percent, secondary_percent
      );
      addGenericEvent(beginOffset, curOffset - beginOffset, "Overlay Volume Balance", desc, Type::Volume);
      break;
    }

    case EVENT_OVERLAY_VOLUME_BALANCE_FADE: {
      const uint8_t raw_length = readByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t balance = readByte(curOffset++);
      const int primary_percent = (127 - balance) * 100 / 256;
      const int secondary_percent = balance * 100 / 256;
      auto desc = fmt::format(
        "Duration: {}  Target Balance: {} (Primary Voice Volume {}%, Secondary Voice Volume {}%)",
        length, balance, primary_percent, secondary_percent
      );
      addGenericEvent(beginOffset, curOffset - beginOffset, "Overlay Volume Balance Fade", desc, Type::VolumeSlide);
      break;
    }

    case EVENT_ALTERNATE_VOICE_ON: {
      const uint8_t rr = readByte(curOffset++); // 0-127
      auto desc = fmt::format("Release Rate: {}", rr);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Alternate Voice On", desc, Type::Misc);
      break;
    }

    case EVENT_ALTERNATE_VOICE_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Alternate Voice Off", "", Type::Misc);
      break;
    }

    case EVENT_FC_0C: {
      curOffset += 2;
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_FC_0D: {
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_FC_0E: {
      curOffset++;
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_FC_0F: {
      curOffset += 2;
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_FC_10: {
      curOffset++;
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_FC_11: {
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_PATTERN: {
      const int16_t relative_offset = readShort(curOffset);
      const uint32_t dest = curOffset + relative_offset + (version >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
      curOffset += 2;
      const uint32_t length = curOffset - beginOffset;

      auto desc = fmt::format("Destination: 0x{:X}", dest);
      addGenericEvent(beginOffset, length, "Play Pattern", desc, Type::Misc);

      pattern_return_offset = curOffset;
      curOffset = dest;
      break;
    }

    case EVENT_END_PATTERN: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "End Pattern", "", Type::Misc);
      curOffset = pattern_return_offset;
      break;
    }

    case EVENT_ALLOC_RESERVED_VOICES: {
      const uint8_t count = readByte(curOffset++);
      auto desc = fmt::format("Number of Voices: {}", count);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Allocate Reserved Voices", desc, Type::Misc);
      break;
    }

    case EVENT_FREE_RESERVED_VOICES: {
      constexpr uint8_t count = 0;
      auto desc = fmt::format("Number of Voices: {}", count);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Free Reserved Voices", desc, Type::Misc);
      break;
    }

    case EVENT_TIME_SIGNATURE: {
      // Both of arguments can be zero: Front Mission 3: 1-08 Setup 2.psf
      const uint8_t ticksPerBeat = readByte(curOffset++);
      const uint8_t beatsPerMeasure = readByte(curOffset++);
      if (ticksPerBeat != 0 && beatsPerMeasure != 0) {
        const uint8_t denom = static_cast<uint8_t>((parentSeq->ppqn() * 4) / ticksPerBeat); // or should it always be 4? no idea
        addTimeSig(beginOffset, curOffset - beginOffset, beatsPerMeasure, denom, ticksPerBeat);
      } else {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Time Signature", "", Type::TimeSignature);
      }
      break;
    }

    case EVENT_MEASURE: {
      const uint16_t measure = readShort(curOffset);
      curOffset += 2;
      auto desc = fmt::format("Measure: {}", measure);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Marker (Measure Number)", desc, Type::Misc);
      // TODO: write midi marker event
      break;
    }

    case EVENT_FE_13_CHRONO_CROSS: {
      // Chrono Cross - 114 Shadow Forest
      // Chrono Cross - 119 Hydra Marshes
      // Chrono Cross - 302 Chronopolis
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_PROGCHANGE_KEY_SPLIT_V1: {
      const int16_t relative_key_split_regions_offset = readShort(curOffset);
      curOffset += 2;
      const uint32_t key_split_regions_offset = curOffset + relative_key_split_regions_offset;

      auto desc = fmt::format("Offset: 0x{:X}", key_split_regions_offset);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Program Change (Key-Split Instrument)", desc, Type::ProgramChange);

      if (readMode == READMODE_ADD_TO_UI) {
        parentSeq->custom_instrument_addresses.insert(key_split_regions_offset);
      } else {
        const auto instrument_index = static_cast<uint32_t>(
            std::distance(parentSeq->custom_instrument_addresses.begin(),
                          parentSeq->custom_instrument_addresses.find(key_split_regions_offset)));

        addBankSelectNoItem(1);
        addProgramChangeNoItem(instrument_index, false);
      }
      break;
    }

    case EVENT_PROGCHANGE_KEY_SPLIT_V2: {
      const uint8_t progNum = readByte(curOffset++);
      addBankSelectNoItem(1);
      addProgramChange(beginOffset, curOffset - beginOffset, progNum, false, "Program Change (Key-Split Instrument)");
      break;
    }

    case EVENT_FE_1C: {
      curOffset++;
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      break;
    }

    case EVENT_USE_RESERVED_VOICES:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Use Reserved Voices", "", Type::Misc);
      break;

    case EVENT_USE_NO_RESERVED_VOICES:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Use No Reserved Voices", "", Type::Misc);
      break;

    default:
      addUnknown(beginOffset, curOffset - beginOffset);
      logUnknownEvent(beginOffset);
      return State::Finished;
    }
  }

  return State::Active;
}

void AkaoTrack::logUnknownEvent(u32 beginOffset) const {
  const u8 status_byte = readByte(beginOffset);
  const u8 data_byte = readByte(beginOffset + 1);
  auto opcode_str = curOffset - beginOffset > 1 ?
    fmt::format("0x{:02X}  0x{:02X}", status_byte, data_byte) :
    fmt::format("0x{:02X}", status_byte);

  L_WARN("Unknown Event - Filename: {} Event: {} Address: {:#010X}", parentSeq->rawFile()->name(),
    opcode_str, beginOffset);
}

bool AkaoTrack::anyUnvisitedJumpDestinations()
{
  return std::ranges::any_of(conditional_jump_destinations,
                     [this](uint32_t dest) { return !isOffsetUsed(dest); });
}
