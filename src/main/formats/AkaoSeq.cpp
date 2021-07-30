#include "pch.h"
#include "AkaoSeq.h"
#include "AkaoInstr.h"

DECLARE_FORMAT(Akao);

using namespace std;

static const uint16_t DELTA_TIME_TABLE[] = { 192, 96, 48, 24, 12, 6, 3, 32, 16, 8, 4 };

AkaoSeq::AkaoSeq(RawFile *file, uint32_t offset, AkaoPs1Version version)
    : VGMSeq(AkaoFormat::name, file, offset), instrument_set_offset_(0), drum_set_offset_(0),
      seq_id(0), version_(version), condition(0) {
  UseLinearAmplitudeScale();        //I think this applies, but not certain, see FF9 320, track 3 for example of problem
  //UseLinearPanAmplitudeScale(PanVolumeCorrectionMode::kAdjustVolumeController); // disabled, it only changes the volume and the pan slightly, and also its output becomes undefined if pan and volume slides are used at the same time
  bUsesIndividualArts = false;
  UseReverb();
}

void AkaoSeq::ResetVars() {
  VGMSeq::ResetVars();

  condition = 0;
  if (rawfile->tag.album == L"Final Fantasy 9" && rawfile->tag.title == L"Final Battle")
    condition = 2;
}

bool AkaoSeq::IsPossibleAkaoSeq(RawFile *file, uint32_t offset) {
  if (offset + 0x10 > file->size())
    return false;
  if (file->GetWordBE(offset) != 0x414B414F)
    return false;

  const AkaoPs1Version version = GuessVersion(file, offset);
  const uint32_t track_bits_offset = GetTrackAllocationBitsOffset(version);
  if (offset + track_bits_offset + 4 > file->size())
    return false;

  const uint32_t track_bits = file->GetWord(offset + track_bits_offset);
  if (version <= AkaoPs1Version::VERSION_2) {
    if ((track_bits & ~0xffffff) != 0)
      return false;
  }

  if (version >= AkaoPs1Version::VERSION_3_0) {
    if (file->GetWord(offset + 0x2C) != 0 || file->GetWord(offset + 0x28) != 0)
      return false;
    if (file->GetWord(offset + 0x38) != 0 || file->GetWord(offset + 0x3C) != 0)
      return false;
  }

  return true;
}

AkaoPs1Version AkaoSeq::GuessVersion(RawFile *file, uint32_t offset) {
  if (file->GetWord(offset + 0x2C) == 0)
    return AkaoPs1Version::VERSION_3_2;
  else if (file->GetWord(offset + 0x1C) == 0)
    return AkaoPs1Version::VERSION_2;
  else
    return AkaoPs1Version::VERSION_1_1;
}

bool AkaoSeq::GetHeaderInfo() {
  if (version() == AkaoPs1Version::UNKNOWN)
    return false;

  const uint32_t track_bits = (version() >= AkaoPs1Version::VERSION_3_0)
    ? GetWord(dwOffset + 0x20)
    : GetWord(dwOffset + 0x10);
  nNumTracks = GetNumPositiveBits(track_bits);

  uint32_t track_header_offset;
  if (version() >= AkaoPs1Version::VERSION_3_0) {
    VGMHeader *hdr = AddHeader(dwOffset, 0x40);
    hdr->AddSig(dwOffset, 4);
    hdr->AddSimpleItem(dwOffset + 0x4, 2, L"ID");
    hdr->AddSimpleItem(dwOffset + 0x6, 2, L"Size");
    hdr->AddSimpleItem(dwOffset + 0x8, 2, L"Reverb Type");
    hdr->AddSimpleItem(dwOffset + 0x14, 2, L"Associated Sample Set ID");
    hdr->AddSimpleItem(dwOffset + 0x20, 4, L"Number of Tracks (# of true bits)");
    hdr->AddSimpleItem(dwOffset + 0x30, 4, L"Instrument Data Pointer");
    hdr->AddSimpleItem(dwOffset + 0x34, 4, L"Drumkit Data Pointer");

    unLength = GetShort(dwOffset + 6);
    id = GetShort(dwOffset + 0x14);
    track_header_offset = 0x40;
  }
  else if (version() == AkaoPs1Version::VERSION_2) {
    VGMHeader *hdr = AddHeader(dwOffset, 0x20);
    hdr->AddSig(dwOffset, 4);
    hdr->AddSimpleItem(dwOffset + 0x4, 2, L"ID");
    hdr->AddSimpleItem(dwOffset + 0x6, 2, L"Size (Excluding first 16 bytes)");
    hdr->AddSimpleItem(dwOffset + 0x8, 2, L"Reverb Type");
    hdr->AddSimpleItem(dwOffset + 0x10, 4, L"Number of Tracks (# of true bits)");

    unLength = 0x10 + GetShort(dwOffset + 6);
    track_header_offset = 0x20;
  }
  else if (version() < AkaoPs1Version::VERSION_2) {
    VGMHeader *hdr = AddHeader(dwOffset, 0x14);
    hdr->AddSig(dwOffset, 4);
    hdr->AddSimpleItem(dwOffset + 0x4, 2, L"ID");
    hdr->AddSimpleItem(dwOffset + 0x6, 2, L"Size (Excluding first 16 bytes)");
    hdr->AddSimpleItem(dwOffset + 0x8, 2, L"Reverb Type");
    std::wostringstream timestamp_text;
    timestamp_text << L"Timestamp (" << ReadTimestampAsText() << L")";
    hdr->AddSimpleItem(dwOffset + 0xA, 6, timestamp_text.str());
    hdr->AddSimpleItem(dwOffset + 0x10, 4, L"Number of Tracks (# of true bits)");

    unLength = 0x10 + GetShort(dwOffset + 6);
    track_header_offset = 0x14;
  }
  else {
    return false;
  }

  name = L"Akao Seq";

  SetPPQN(0x30);
  seq_id = GetShort(dwOffset + 4);

  LoadEventMap();

  if (version() >= AkaoPs1Version::VERSION_3_0)
  {
    //There must be either a melodic instrument section, a drumkit, or both.  We determine
    //the start of the InstrSet based on whether a melodic instrument section is given.
    const uint32_t instrOff = GetWord(dwOffset + 0x30);
    const uint32_t drumkitOff = GetWord(dwOffset + 0x34);
    if (instrOff != 0)
      set_instrument_set_offset(dwOffset + 0x30 + instrOff);
    if (drumkitOff != 0)
      set_drum_set_offset(dwOffset + 0x34 + drumkitOff);
  }

  VGMHeader *track_pointer_header = AddHeader(dwOffset + track_header_offset, nNumTracks * 2);
  for (unsigned int i = 0; i < nNumTracks; i++) {
    std::wstringstream name;
    name << L"Offset: Track " << (i + 1);
    track_pointer_header->AddSimpleItem(dwOffset + track_header_offset + (i * 2), 2, name.str());
  }

  return true;
}


bool AkaoSeq::GetTrackPointers() {
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
    const uint32_t relative_offset = GetShort(dwOffset + p);
    const uint32_t track_offset = base + relative_offset;
    aTracks.push_back(new AkaoTrack(this, dwOffset + track_offset));
  }
  return true;
}

std::wstring AkaoSeq::ReadTimestampAsText() {
  const uint8_t year_bcd = GetByte(dwOffset + 0xA);
  const uint8_t month_bcd = GetByte(dwOffset + 0xB);
  const uint8_t day_bcd = GetByte(dwOffset + 0xC);
  const uint8_t hour_bcd = GetByte(dwOffset + 0xD);
  const uint8_t minute_bcd = GetByte(dwOffset + 0xE);
  const uint8_t second_bcd = GetByte(dwOffset + 0xF);

  // Should we solve the year 2000 problem?
  const unsigned int year_bcd_full = 0x1900 + year_bcd;

  std::wostringstream text;
  text << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << year_bcd_full << L"-"
    << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << month_bcd << L"-"
    << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << day_bcd << L"T"
    << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << hour_bcd << L":"
    << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << minute_bcd << L":"
    << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << second_bcd;
  return text.str();
}

double AkaoSeq::GetTempoInBPM(uint16_t tempo) const {
  if (tempo != 0) {
    const uint16_t freq = (version() == AkaoPs1Version::VERSION_1_0) ? 0x43D1 : 0x44E8;
    return 60.0 / (ppqn * (65536.0 / tempo) * (freq / (33868800.0 / 8)));
  }
  else {
    // since tempo 0 cannot be expressed, this function returns a very small value.
    return 1.0;
  }
}

AkaoInstrSet* AkaoSeq::NewInstrSet() const {
  if (version() >= AkaoPs1Version::VERSION_3_0) {
    uint32_t length = 0;
    if (has_instrument_set_offset())
      length = unLength - (instrument_set_offset() - dwOffset);
    else if (has_drum_set_offset())
      length = unLength - (drum_set_offset() - dwOffset);

    return length != 0
      ? new AkaoInstrSet(rawfile, length, version(), instrument_set_offset(), drum_set_offset(), id, L"Akao Instr Set")
      : new AkaoInstrSet(rawfile, dwOffset, dwOffset + unLength, version());
  } else {
    return new AkaoInstrSet(rawfile, dwOffset + unLength, version(), custom_instrument_addresses, drum_instrument_addresses);
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
}

void AkaoTrack::ResetVars() {
  SeqTrack::ResetVars();

  slur = false;
  legato = false;
  drum = false;

  pattern_return_offset = 0;

  std::fill(loop_begin_loc.begin(), loop_begin_loc.end(), 0);
  loop_layer = 0;
  std::fill(loop_counter.begin(), loop_counter.end(), 0);

  last_delta_time = 0;
  use_one_time_delta_time = false;
  one_time_delta_time = 0;
  delta_time_overwrite = 0;
  tuning = 0;

  conditional_jump_destinations.clear();
}

bool AkaoTrack::ReadEvent() {
  AkaoSeq *parentSeq = seq();
  const AkaoPs1Version version = parentSeq->version();
  const uint32_t beginOffset = curOffset;
  const uint8_t status_byte = GetByte(curOffset++);

  std::wstringstream desc;

  std::wstringstream opcode_strm;
  opcode_strm << L"0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << status_byte;

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
      delta_time = GetByte(curOffset++);
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
        const uint8_t drum_octave = 2;
        key = drum ? (drum_octave * 12) + relative_key : real_key;
      }

      AddNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur);
      AddTime(delta_time);
    }
    else if (op_tie)
    {
      MakePrevDurNoteEnd(GetTime() + dur);
      AddTime(delta_time);
      desc << L"Length: " << delta_time << L"  Duration: " << dur;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str(), CLR_TIE);
    }
    else // rest
    {
      AddRest(beginOffset, curOffset - beginOffset, delta_time);
    }
  }
  else if ((status_byte >= 0x9A) && (status_byte <= 0x9F)) {
    AddUnknown(beginOffset, curOffset - beginOffset, L"Undefined");
    return false; // they should not be used
  }
  else {
    auto event = static_cast<AkaoSeqEventType>(0);

    if (version >= AkaoPs1Version::VERSION_3_0 && status_byte == 0xFE)
    {
      const uint8_t op = GetByte(curOffset++);
      const auto event_iterator = parentSeq->sub_event_map.find(op);
      if (event_iterator != parentSeq->sub_event_map.end())
        event = event_iterator->second;

      opcode_strm << L" 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << op;
    }
    else if ((version == AkaoPs1Version::VERSION_1_2 || version == AkaoPs1Version::VERSION_2) && status_byte == 0xFC)
    {
      const uint8_t op = GetByte(curOffset++);
      const auto event_iterator = parentSeq->sub_event_map.find(op);
      if (event_iterator != parentSeq->sub_event_map.end())
        event = event_iterator->second;

      opcode_strm << L" 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << op;
    }
    else
    {
      const auto event_iterator = parentSeq->event_map.find(status_byte);
      if (event_iterator != parentSeq->event_map.end())
        event = event_iterator->second;
    }

    const std::wstring opcode_str = opcode_strm.str();

    switch (event) {
    case EVENT_END:
      AddEndOfTrack(beginOffset, curOffset - beginOffset);
      return false;

    case EVENT_PROGCHANGE: {
      // change program to articulation number
      parentSeq->bUsesIndividualArts = true;
      const uint8_t artNum = GetByte(curOffset++);
      AddBankSelectNoItem(0);
      AddProgramChange(beginOffset, curOffset - beginOffset, artNum);
      break;
    }

    case EVENT_ONE_TIME_DURATION: {
      const uint8_t delta_time = GetByte(curOffset++);
      last_delta_time = one_time_delta_time = delta_time;
      use_one_time_delta_time = true;
      desc << L"Length: " << delta_time;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Next Note Length", desc.str(), CLR_CHANGESTATE);
      break;
    }

    case EVENT_VOLUME: {
      const uint8_t vol = GetByte(curOffset++);
      AddVol(beginOffset, curOffset - beginOffset, vol);
      break;
    }

    case EVENT_VOLUME_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t vol = GetByte(curOffset++);
      AddVolSlide(beginOffset, curOffset - beginOffset, length, vol);
      break;
    }

    case EVENT_PITCH_SLIDE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const int8_t semitones = GetByte(curOffset++);
      desc << L"Length: " << length << L"  Key: " << semitones << L" semitones";
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Slide", desc.str(), CLR_PITCHBEND, ICON_CONTROL);
      break;
    }

    case EVENT_OCTAVE: {
      const uint8_t new_octave = GetByte(curOffset++);
      AddSetOctave(beginOffset, curOffset - beginOffset, new_octave);
      break;
    }

    case EVENT_INCREMENT_OCTAVE:
      // The formula below is more accurate implementation of this (at least in FF7), but who cares?
      //octave = (octave + 1) & 0xf;
      AddIncrementOctave(beginOffset, curOffset - beginOffset);
      break;

    case EVENT_DECREMENT_OCTAVE:
      // The formula below is more accurate implementation of this (at least in FF7), but who cares?
      //octave = (octave - 1) & 0xf;
      AddDecrementOctave(beginOffset, curOffset - beginOffset);
      break;

    case EVENT_EXPRESSION: {
      const uint8_t expression = GetByte(curOffset++);
      AddExpression(beginOffset, curOffset - beginOffset, expression);
      break;
    }

    case EVENT_EXPRESSION_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t expression = GetByte(curOffset++);
      AddExpressionSlide(beginOffset, curOffset - beginOffset, length, expression);
      break;
    }

    case EVENT_EXPRESSION_FADE_PER_NOTE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t expression = GetByte(curOffset++);
      desc << L"Target Expression: " << expression << L"  Duration: " << length;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Expression Slide Per Note", desc.str(), CLR_VOLUME, ICON_CONTROL);
      break;
    }

    case EVENT_PAN: {
      // TODO: volume balance conversion
      const uint8_t pan = GetByte(curOffset++); // 0-127
      AddPan(beginOffset, curOffset - beginOffset, pan);
      break;
    }

    case EVENT_PAN_FADE: {
      // TODO: volume balance conversion
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t pan = GetByte(curOffset++);
      AddPanSlide(beginOffset, curOffset - beginOffset, length, pan);
      break;
    }

    case EVENT_NOISE_CLOCK: {
      const uint8_t raw_clock = GetByte(curOffset++);
      const bool relative = (raw_clock & 0xc0) != 0;
      if (relative)
      {
        const uint8_t clock = raw_clock & 0x3f;
        const int8_t signed_clock = ((clock & 0x20) != 0) ? (0x20 - (clock & 0x1f)) : clock;
        desc << L"Clock: " << signed_clock << " (Relative)";
      }
      else
      {
        const uint8_t clock = raw_clock & 0x3f;
        desc << L"Clock: " << clock;
      }
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise Clock", desc.str(), CLR_CHANGESTATE, ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_ATTACK_RATE: {
      const uint8_t ar = GetByte(curOffset++); // 0-127
      desc << L"AR: " << ar;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Attack Rate", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_DECAY_RATE: {
      const uint8_t dr = GetByte(curOffset++); // 0-15
      desc << L"DR: " << dr;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Decay Rate", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_SUSTAIN_LEVEL: {
      const uint8_t sl = GetByte(curOffset++); // 0-15
      desc << L"SL: " << sl;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Sustain Level", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_DECAY_RATE_AND_SUSTAIN_LEVEL: {
      const uint8_t dr = GetByte(curOffset++); // 0-15
      const uint8_t sl = GetByte(curOffset++); // 0-15
      desc << L"DR: " << dr << L"  SL: " << sl;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Decay Rate & Sustain Level", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_SUSTAIN_RATE: {
      const uint8_t sr = GetByte(curOffset++); // 0-127
      desc << L"SR: " << sr;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Sustain Rate", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_RELEASE_RATE: {
      const uint8_t rr = GetByte(curOffset++); // 0-127
      desc << L"RR: " << rr;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Release Rate", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_RESET_ADSR:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reset ADSR", L"", CLR_ADSR);
      break;

    case EVENT_VIBRATO: {
      const uint8_t delay = GetByte(curOffset++);
      const uint8_t raw_rate = GetByte(curOffset++);
      const uint16_t rate = raw_rate == 0 ? 256 : raw_rate;
      const uint8_t type = GetByte(curOffset++); // 0-15
      desc << L"Delay: " << delay << L"  Rate: " << rate << L"  LFO Type: " << type;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato", desc.str(), CLR_LFO, ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_DEPTH: {
      const uint8_t raw_depth = GetByte(curOffset++);
      const uint8_t depth = raw_depth & 0x7f;
      const uint8_t depth_type = (raw_depth & 0x80) >> 7;
      desc << L"Depth: " << depth;
      if (depth_type == 0) {
        desc << L" (15/256 scale, approx 50 cents at maximum)";
      }
      else {
        desc << L" (1/1 scale, approx 700 cents at maximum)";
      }
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Depth", desc.str(), CLR_LFO);
      break;
    }

    case EVENT_VIBRATO_OFF:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Off", L"", CLR_LFO);
      break;

    case EVENT_ADSR_ATTACK_MODE: {
      const uint8_t ar_mode = GetByte(curOffset++);
      desc << L"Mode: " << ar_mode;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Attack Rate Mode", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_TREMOLO: {
      const uint8_t delay = GetByte(curOffset++);
      const uint8_t raw_rate = GetByte(curOffset++);
      const uint16_t rate = raw_rate == 0 ? 256 : raw_rate;
      const uint8_t type = GetByte(curOffset++); // 0-15
      desc << L"Delay: " << delay << L"  Rate: " << rate << L"  LFO Type: " << type;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo", desc.str(), CLR_LFO, ICON_CONTROL);
      break;
    }

    case EVENT_TREMOLO_DEPTH: {
      const uint8_t depth = GetByte(curOffset++);
      desc << L"Depth: " << depth;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo Depth", desc.str(), CLR_LFO);
      break;
    }

    case EVENT_TREMOLO_OFF:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo Off", L"", CLR_LFO);
      break;

    case EVENT_ADSR_SUSTAIN_MODE: {
      const uint8_t sr_mode = GetByte(curOffset++);
      desc << L"Mode: " << sr_mode;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Sustain Rate Mode", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO: {
      const uint8_t raw_rate = GetByte(curOffset++);
      const uint16_t rate = raw_rate == 0 ? 256 : raw_rate;
      const uint8_t type = GetByte(curOffset++); // 0-15
      desc << L"Rate: " << rate << L"  LFO Type: " << type;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan LFO", desc.str(), CLR_LFO, ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO_DEPTH: {
      const uint8_t depth = GetByte(curOffset++);
      desc << L"Depth: " << depth;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan LFO Depth", desc.str(), CLR_LFO);
      break;
    }

    case EVENT_PAN_LFO_OFF:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan LFO Off", L"", CLR_LFO);
      break;

    case EVENT_ADSR_RELEASE_MODE: {
      const uint8_t rr_mode = GetByte(curOffset++);
      desc << L"Mode: " << rr_mode;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"ADSR Release Rate Mode", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_TRANSPOSE_ABS: {
      const int8_t key = GetByte(curOffset++);
      AddTranspose(beginOffset, curOffset - beginOffset, key);
      break;
    }

    case EVENT_TRANSPOSE_REL: {
      const int8_t key = GetByte(curOffset++);
      AddTranspose(beginOffset, curOffset - beginOffset, transpose + key, L"Transpose (Relative)");
      break;
    }

    case EVENT_REVERB_ON:
      // TODO: reverb control
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reverb On", L"", CLR_REVERB);
      break;

    case EVENT_REVERB_OFF:
      // TODO: reverb control
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reverb Off", L"", CLR_REVERB);
      break;

    case EVENT_NOISE_ON:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise On", L"", CLR_PROGCHANGE, ICON_CONTROL);
      break;

    case EVENT_NOISE_OFF:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise Off", L"", CLR_PROGCHANGE, ICON_CONTROL);
      break;

    case EVENT_PITCH_MOD_ON:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"FM (Pitch LFO) On", L"", CLR_PROGCHANGE, ICON_CONTROL);
      break;

    case EVENT_PITCH_MOD_OFF:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"FM (Pitch LFO) Off", L"", CLR_PROGCHANGE, ICON_CONTROL);
      break;

    case EVENT_LOOP_START:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Repeat Start", L"", CLR_LOOP, ICON_STARTREP);

      loop_layer = (loop_layer + 1) & 3;
      loop_begin_loc[loop_layer] = curOffset;
      loop_counter[loop_layer] = 0;
      break;

    case EVENT_LOOP_UNTIL: {
      const uint8_t raw_count = GetByte(curOffset++);
      const uint16_t count = raw_count == 0 ? 256 : raw_count;

      desc << L"Count: " << count;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Repeat Until", desc.str(), CLR_LOOP, ICON_ENDREP);

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
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Repeat Again", L"", CLR_LOOP, ICON_ENDREP);

      loop_counter[loop_layer]++;
      curOffset = loop_begin_loc[loop_layer];
      break;
    }

    case EVENT_RESET_VOICE_EFFECTS:
      // Reset noise, FM modulation, reverb, etc.
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reset Voice Effects", L"", CLR_CHANGESTATE, ICON_CONTROL);
      break;

    case EVENT_SLUR_ON:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Slur On (No Key Off, No Retrigger)", L"", CLR_CHANGESTATE, ICON_CONTROL);
      slur = true;
      break;

    case EVENT_SLUR_OFF:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Slur Off", L"", CLR_CHANGESTATE, ICON_CONTROL);
      slur = false;
      break;

    case EVENT_NOISE_ON_DELAY_TOGGLE: {
      const uint8_t raw_delay = GetByte(curOffset++);
      const uint16_t delay = raw_delay == 0 ? 1 : raw_delay + 1;
      desc << "Delay: " << delay;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise On & Delay Switching", desc.str(), CLR_PROGCHANGE, ICON_PROGCHANGE);
      break;
    }

    case EVENT_NOISE_DELAY_TOGGLE: {
      const uint8_t raw_delay = GetByte(curOffset++);
      const uint16_t delay = raw_delay == 0 ? 1 : raw_delay + 1;
      desc << "Delay: " << delay;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Noise On/Off Delay Switching", desc.str(), CLR_PROGCHANGE, ICON_PROGCHANGE);
      break;
    }

    case EVENT_LEGATO_ON:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Legato On (No Key Off)", L"", CLR_CHANGESTATE, ICON_CONTROL);
      legato = true;
      break;

    case EVENT_LEGATO_OFF:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Legato Off", L"", CLR_CHANGESTATE, ICON_CONTROL);
      legato = false;
      break;

    case EVENT_PITCH_MOD_ON_DELAY_TOGGLE: {
      const uint8_t raw_delay = GetByte(curOffset++);
      const uint16_t delay = raw_delay == 0 ? 1 : raw_delay + 1;
      desc << "Delay: " << delay;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"FM (Pitch LFO) On & Delay Switching", desc.str(), CLR_PROGCHANGE, ICON_PROGCHANGE);
      break;
    }

    case EVENT_PITCH_MOD_DELAY_TOGGLE: {
      const uint8_t raw_delay = GetByte(curOffset++);
      const uint16_t delay = raw_delay == 0 ? 1 : raw_delay + 1;
      desc << "Delay: " << delay;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"FM (Pitch LFO) On/Off Delay Switching", desc.str(), CLR_PROGCHANGE, ICON_PROGCHANGE);
      break;
    }

    case EVENT_PITCH_SIDE_CHAIN_ON:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Side Chain On", L"", CLR_MISC);
      break;

    case EVENT_PITCH_SIDE_CHAIN_OFF:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch Side Chain Off", L"", CLR_MISC);
      break;

    case EVENT_PITCH_TO_VOLUME_SIDE_CHAIN_ON:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch-Volume Side Chain On", L"", CLR_MISC);
      break;

    case EVENT_PITCH_TO_VOLUME_SIDE_CHAIN_OFF:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pitch-Volume Side Chain Off", L"", CLR_MISC);
      break;

    case EVENT_TUNING_ABS:
    {
      // signed data byte.  range of 1 octave (0x7F = +1 octave, 0x80 = -1 octave)
      tuning = GetByte(curOffset++);

      const int div = tuning >= 0 ? 128 : 256;
      const double scale = tuning / static_cast<double>(div);
      const double cents = (scale / log(2.0)) * 1200.0;
      desc << L"Tuning: " << cents << L" cents (" << tuning << L"/" << div << L")";
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tuning", desc.str(), CLR_MISC, ICON_CONTROL);
      AddFineTuningNoItem(cents);
      break;
    }

    case EVENT_TUNING_REL: {
      const int8_t relative_tuning = GetByte(curOffset++);
      tuning += relative_tuning;

      const int div = tuning >= 0 ? 128 : 256;
      const double scale = tuning / static_cast<double>(div);
      const double cents = (scale / log(2.0)) * 1200.0;
      desc << L"Amount: " << relative_tuning <<  L"  Tuning: " << cents << L" cents (" << tuning << L"/" << div << L")";
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tuning (Relative)", desc.str(), CLR_MISC, ICON_CONTROL);
      AddFineTuningNoItem(cents);
      break;
    }

    case EVENT_PORTAMENTO_ON: {
      const uint8_t raw_speed = GetByte(curOffset++);
      const uint16_t speed = raw_speed == 0 ? 256 : raw_speed;
      desc << L"Time: " << speed << " tick(s)";
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Portamento", desc.str(), CLR_PORTAMENTO, ICON_CONTROL);
      //AddPortamentoTimeNoItem(speed);
      AddPortamentoNoItem(true);
      break;
    }

    case EVENT_PORTAMENTO_OFF: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Portamento Off", desc.str(), CLR_PORTAMENTO, ICON_CONTROL);
      //AddPortamentoTimeNoItem(0);
      AddPortamentoNoItem(false);
      break;
    }

    case EVENT_FIXED_DURATION: {
      const int8_t relative_length = GetByte(curOffset++);
      const int16_t length = std::min(std::max(last_delta_time + relative_length, 1), 255);
      delta_time_overwrite = length;
      desc << L"Duration (Relative Amount): " << relative_length;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Fixed Note Length", desc.str(), CLR_MISC, ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_DEPTH_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t raw_depth = GetByte(curOffset++);
      const uint8_t depth = raw_depth & 0x7f;
      const uint8_t depth_type = (raw_depth & 0x80) >> 7;
      desc << L"Duration: " << length << L"  Target Depth: " << depth;
      if (depth_type == 0) {
        desc << L" (15/256 scale, approx 50 cents at maximum)";
      }
      else {
        desc << L" (1/1 scale, approx 700 cents at maximum)";
      }
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Depth Slide", desc.str(), CLR_LFO, ICON_CONTROL);
      break;
    }

    case EVENT_TREMOLO_DEPTH_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t depth = GetByte(curOffset++);
      desc << L"Duration: " << length << L"  Target Depth: " << depth;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo Depth Slide", desc.str(), CLR_LFO, ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t depth = GetByte(curOffset++);
      desc << L"Duration: " << length << L"  Target Depth: " << depth;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan LFO Depth Slide", desc.str(), CLR_LFO);
      break;
    }

    case EVENT_VIBRATO_RATE_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t rate = GetByte(curOffset++);
      desc << L"Duration: " << length << L"  Target Rate: " << rate;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Vibrato Rate Slide", desc.str(), CLR_LFO, ICON_CONTROL);
      break;
    }

    case EVENT_TREMOLO_RATE_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t rate = GetByte(curOffset++);
      desc << L"Duration: " << length << L"  Target Rate: " << rate;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tremolo Rate Slide", desc.str(), CLR_LFO, ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO_RATE_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t rate = GetByte(curOffset++);
      desc << L"Duration: " << length << L"  Target Rate: " << rate;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan LFO Rate Slide", desc.str(), CLR_LFO, ICON_CONTROL);
      break;
    }

    case EVENT_E0: {
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_E1: {
      curOffset++;
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_E2: {
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_TEMPO: {
      const uint16_t raw_tempo = GetShort(curOffset);
      curOffset += 2;

      const double bpm = parentSeq->GetTempoInBPM(raw_tempo);
      AddTempoBPM(beginOffset, curOffset - beginOffset, bpm);
      break;
    }

    case EVENT_TEMPO_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint16_t raw_tempo = GetShort(curOffset);
      curOffset += 2;

      const double bpm = parentSeq->GetTempoInBPM(raw_tempo);
      AddTempoBPMSlide(beginOffset, curOffset - beginOffset, length, bpm);
      break;
    }

    case EVENT_REVERB_DEPTH: {
      const int16_t depth = GetShort(curOffset);
      curOffset += 2;

      desc << L"Depth: " << depth;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reverb Depth", desc.str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_REVERB_DEPTH_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const int16_t depth = GetShort(curOffset);
      curOffset += 2;

      desc << L"Duration: " << length << L"  Target Depth: " << depth;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Reverb Depth Slide", desc.str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_DRUM_ON_V1: {
      const int16_t relative_drum_offset = GetShort(curOffset);
      curOffset += 2;
      const uint32_t drum_instrset_offset = curOffset + relative_drum_offset;

      desc << L"Offset: 0x" << std::hex << std::setfill(L'0') << std::uppercase << drum_instrset_offset;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Drum Kit On", desc.str(), CLR_PROGCHANGE, ICON_PROGCHANGE);

      if (readMode == READMODE_ADD_TO_UI) {
        parentSeq->drum_instrument_addresses.insert(drum_instrset_offset);
      }
      else {
        const int instrument_index = std::distance(parentSeq->drum_instrument_addresses.begin(),
          parentSeq->drum_instrument_addresses.find(drum_instrset_offset));

        AddBankSelectNoItem(127 - static_cast<uint8_t>(instrument_index));
        AddProgramChangeNoItem(127, false);
        drum = true;
        //channel = 9;
      }

      break;
    }

    case EVENT_DRUM_ON_V2: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Drum Kit On", desc.str(), CLR_PROGCHANGE, ICON_PROGCHANGE);
      AddBankSelectNoItem(127);
      AddProgramChangeNoItem(127, false);
      drum = true;
      //channel = 9;
      break;
    }

    case EVENT_DRUM_OFF:
      // TODO: restore program change for regular instrument
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Drum Kit Off", L"", CLR_PROGCHANGE, ICON_PROGCHANGE);
      drum = false;
      break;

    case EVENT_UNCONDITIONAL_JUMP: {
      const int16_t relative_offset = GetShort(curOffset);
      const uint32_t dest = curOffset + relative_offset + (version >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
      curOffset += 2;
      const uint32_t length = curOffset - beginOffset;

      desc << L"Destination: 0x" << std::hex << std::setfill(L'0') << std::uppercase << dest;
      curOffset = dest;

      if (!IsOffsetUsed(dest)) {
        AddGenericEvent(beginOffset, length, L"Jump", desc.str(), CLR_LOOPFOREVER);
      }
      else {
        if (!AddLoopForever(beginOffset, length, L"Jump"))
          return AnyUnvisitedJumpDestinations();
      }
      break;
    }

    case EVENT_CPU_CONDITIONAL_JUMP: {
      const uint8_t target_value = GetByte(curOffset++);
      const int16_t relative_offset = GetShort(curOffset);
      const uint32_t dest = curOffset + relative_offset + (version >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
      curOffset += 2;
      const uint32_t length = curOffset - beginOffset;

      desc << L"Conditional Value" << target_value << L"  Destination: 0x" << std::hex << std::setfill(L'0') << std::uppercase << dest;

      if (readMode == READMODE_ADD_TO_UI) {
        // This event performs conditional jump if certain CPU variable matches to the condValue.
        // VGMTrans will simply try to parse all events as far as possible, instead.
        // (Test case: FF9 416 Final Battle)
        if (!IsOffsetUsed(beginOffset)) {
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

      AddGenericEvent(beginOffset, length, L"CPU-Conditional Jump", desc.str(), CLR_LOOP);
      break;
    }

    case EVENT_LOOP_BRANCH: {
      const uint8_t raw_count = GetByte(curOffset++);
      const uint16_t count = raw_count == 0 ? 256 : raw_count;
      const int16_t relative_offset = GetShort(curOffset);
      const uint32_t dest = curOffset + relative_offset + (version >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
      curOffset += 2;
      const uint32_t length = curOffset - beginOffset;

      desc << L"Count: " << count << L"  Destination: 0x" << std::hex << std::setfill(L'0') << std::uppercase << dest;
      AddGenericEvent(beginOffset, length, L"Repeat Branch", desc.str(), CLR_MISC);

      if (loop_counter[loop_layer] + 1 == count)
        curOffset = dest;

      break;
    }

    case EVENT_LOOP_BREAK: {
      const uint8_t raw_count = GetByte(curOffset++);
      const uint16_t count = raw_count == 0 ? 256 : raw_count;
      const int16_t relative_offset = GetShort(curOffset);
      const uint32_t dest = curOffset + relative_offset + (version >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
      curOffset += 2;
      const uint32_t length = curOffset - beginOffset;

      desc << L"Count: " << count << L"  Destination: 0x" << std::hex << std::setfill(L'0') << std::uppercase << dest;
      AddGenericEvent(beginOffset, length, L"Repeat Break", desc.str(), CLR_MISC);

      if (loop_counter[loop_layer] + 1 == count)
        curOffset = dest;

      loop_layer = (loop_layer - 1) & 3;
      break;
    }

    case EVENT_PROGCHANGE_NO_ATTACK: {
      parentSeq->bUsesIndividualArts = true;
      const uint8_t artNum = GetByte(curOffset++);
      AddBankSelectNoItem(0);
      AddProgramChange(beginOffset, curOffset - beginOffset, artNum, false, L"Program Change w/o Attack Sample");
      break;
    }

    case EVENT_F3_FF7: {
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_F3_SAGAFRO: {
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_OVERLAY_VOICE_ON: {
      parentSeq->bUsesIndividualArts = true;
      const uint8_t artNum = GetByte(curOffset++);
      const uint8_t artNum2 = GetByte(curOffset++);

      desc << L"Program Number for Primary Voice: " << artNum << L"  Program Number for Secondary Voice: " << artNum2;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Overlay Voice On (With Program Change)", desc.str(), CLR_PROGCHANGE, ICON_PROGCHANGE);
      AddBankSelectNoItem(0);
      AddProgramChangeNoItem(artNum, false);
      break;
    }

    case EVENT_OVERLAY_VOICE_OFF: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Overlay Voice Off", L"", CLR_MISC, ICON_CONTROL);
      break;
    }

    case EVENT_OVERLAY_VOLUME_BALANCE: {
      const uint8_t balance = GetByte(curOffset++);
      const int primary_percent = (127 - balance) * 100 / 256;
      const int secondary_percent = balance * 100 / 256;
      desc << L"Balance: " << balance << L" (Primary Voice Volume " << primary_percent << "%, Secondary Voice Volume " << secondary_percent << L"%)";
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Overlay Volume Balance", desc.str(), CLR_VOLUME, ICON_CONTROL);
      break;
    }

    case EVENT_OVERLAY_VOLUME_BALANCE_FADE: {
      const uint8_t raw_length = GetByte(curOffset++);
      const uint16_t length = raw_length == 0 ? 256 : raw_length;
      const uint8_t balance = GetByte(curOffset++);
      const int primary_percent = (127 - balance) * 100 / 256;
      const int secondary_percent = balance * 100 / 256;
      desc << L"Duration: " << length << L"  Target Balance: " << balance << L" (Primary Voice Volume " << primary_percent << "%, Secondary Voice Volume " << secondary_percent << L"%)";
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Overlay Volume Balance Fade", desc.str(), CLR_VOLUME, ICON_CONTROL);
      break;
    }

    case EVENT_ALTERNATE_VOICE_ON: {
      const uint8_t rr = GetByte(curOffset++); // 0-127
      desc << L"Release Rate: " << rr;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Alternate Voice On", desc.str(), CLR_MISC);
      break;
    }

    case EVENT_ALTERNATE_VOICE_OFF: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Alternate Voice Off", L"", CLR_MISC);
      break;
    }

    case EVENT_FC_0C: {
      curOffset += 2;
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_FC_0D: {
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_FC_0E: {
      curOffset++;
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_FC_0F: {
      curOffset += 2;
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_FC_10: {
      curOffset++;
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_FC_11: {
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      break;
    }

    case EVENT_PATTERN: {
      const int16_t relative_offset = GetShort(curOffset);
      const uint32_t dest = curOffset + relative_offset + (version >= AkaoPs1Version::VERSION_3_0 ? 0 : 2);
      curOffset += 2;
      const uint32_t length = curOffset - beginOffset;

      desc << L"Destination: 0x" << std::hex << std::setfill(L'0') << std::uppercase << dest;
      AddGenericEvent(beginOffset, length, L"Play Pattern", desc.str(), CLR_MISC);

      pattern_return_offset = curOffset;
      curOffset = dest;
      break;
    }

    case EVENT_END_PATTERN: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"End Pattern", L"", CLR_MISC);
      curOffset = pattern_return_offset;
      break;
    }

    case EVENT_ALLOC_RESERVED_VOICES: {
      const uint8_t count = GetByte(curOffset++);
      desc << L"Number of Voices: " << count;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Allocate Reserved Voices", desc.str(), CLR_MISC);
      break;
    }

    case EVENT_FREE_RESERVED_VOICES: {
      const uint8_t count = 0;
      desc << L"Number of Voices: " << count;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Free Reserved Voices", desc.str(), CLR_MISC);
      break;
    }

    case EVENT_TIME_SIGNATURE: {
      // Both of arguments can be zero: Front Mission 3: 1-08 Setup 2.psf
      const uint8_t ticksPerBeat = GetByte(curOffset++);
      const uint8_t beatsPerMeasure = GetByte(curOffset++);
      if (ticksPerBeat != 0 && beatsPerMeasure != 0) {
        const uint8_t denom = static_cast<uint8_t>((parentSeq->ppqn * 4) / ticksPerBeat); // or should it always be 4? no idea
        AddTimeSig(beginOffset, curOffset - beginOffset, beatsPerMeasure, denom, ticksPerBeat);
      } else {
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Time Signature", L"", CLR_TIMESIG, ICON_TIMESIG);
      }
      break;
    }

    case EVENT_MEASURE: {
      const uint16_t measure = GetShort(curOffset);
      curOffset += 2;
      desc << L"Measure: " << measure;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Marker (Measure Number)", desc.str(), CLR_MISC);
      // TODO: write midi marker event
      break;
    }

    case EVENT_FE_13_CHRONO_CROSS: {
      // Chrono Cross - 114 Shadow Forest
      // Chrono Cross - 119 Hydra Marshes
      // Chrono Cross - 302 Chronopolis
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_WARN, L"AkaoSeq"));
      break;
    }

    case EVENT_PROGCHANGE_KEY_SPLIT_V1: {
      const int16_t relative_key_split_regions_offset = GetShort(curOffset);
      curOffset += 2;
      const uint32_t key_split_regions_offset = curOffset + relative_key_split_regions_offset;

      desc << L"Offset: 0x" << std::hex << std::setfill(L'0') << std::uppercase << key_split_regions_offset;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Program Change (Key-Split Instrument)", desc.str(), CLR_PROGCHANGE, ICON_PROGCHANGE);

      if (readMode == READMODE_ADD_TO_UI) {
        parentSeq->custom_instrument_addresses.insert(key_split_regions_offset);
      }
      else {
        const int instrument_index = std::distance(parentSeq->custom_instrument_addresses.begin(),
          parentSeq->custom_instrument_addresses.find(key_split_regions_offset));

        AddBankSelectNoItem(1);
        AddProgramChangeNoItem(instrument_index, false);
      }
      break;
    }

    case EVENT_PROGCHANGE_KEY_SPLIT_V2: {
      const uint8_t progNum = GetByte(curOffset++);
      AddBankSelectNoItem(1);
      AddProgramChange(beginOffset, curOffset - beginOffset, progNum, false, L"Program Change (Key-Split Instrument)");
      break;
    }

    case EVENT_FE_1C: {
      curOffset++;
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_WARN, L"AkaoSeq"));
      break;
    }

    case EVENT_USE_RESERVED_VOICES:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Use Reserved Voices", L"", CLR_MISC);
      break;

    case EVENT_USE_NO_RESERVED_VOICES:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Use No Reserved Voices", L"", CLR_MISC);
      break;

    default:
      desc << L"Filename: " << parentSeq->rawfile->GetFileName() << L" Event: " << opcode_str
        << " Address: 0x" << std::hex << std::setfill(L'0') << std::uppercase << beginOffset;
      AddUnknown(beginOffset, curOffset - beginOffset);
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()), LOG_LEVEL_ERR, L"AkaoSeq"));
      return false;
    }
  }

  return true;
}

bool AkaoTrack::AnyUnvisitedJumpDestinations()
{
  return std::any_of(conditional_jump_destinations.begin(), conditional_jump_destinations.end(),
                     [this](uint32_t dest) { return !IsOffsetUsed(dest); });
}
