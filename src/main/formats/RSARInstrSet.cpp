#include "pch.h"
#include "RSARInstrSet.h"

static float GetFallingRate(uint8_t DecayTime) {
  if (DecayTime == 0x7F)
    return 65535.0f;
  else if (DecayTime == 0x7E)
    return 120 / 5.0f;
  else if (DecayTime < 0x32)
    return ((DecayTime << 1) + 1) / 128.0f / 5.0f;
  else
    return ((60.0f) / (126 - DecayTime)) / 5.0f;
}

static void SetupEnvelope(VGMRgn *rgn, uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release) {
  static const float attackTable[128] = {
    0.9992175f, 0.9984326f, 0.9976452f, 0.9968553f,
    0.9960629f, 0.9952679f, 0.9944704f, 0.9936704f,
    0.9928677f, 0.9920625f, 0.9912546f, 0.9904441f,
    0.9896309f, 0.9888151f, 0.9879965f, 0.9871752f,
    0.9863512f, 0.9855244f, 0.9846949f, 0.9838625f,
    0.9830273f, 0.9821893f, 0.9813483f, 0.9805045f,
    0.9796578f, 0.9788081f, 0.9779555f, 0.9770999f,
    0.9762413f, 0.9753797f, 0.9745150f, 0.9736472f,
    0.9727763f, 0.9719023f, 0.9710251f, 0.9701448f,
    0.9692612f, 0.9683744f, 0.9674844f, 0.9665910f,
    0.9656944f, 0.9647944f, 0.9638910f, 0.9629842f,
    0.9620740f, 0.9611604f, 0.9602433f, 0.9593226f,
    0.9583984f, 0.9574706f, 0.9565392f, 0.9556042f,
    0.9546655f, 0.9537231f, 0.9527769f, 0.9518270f,
    0.9508732f, 0.9499157f, 0.9489542f, 0.9479888f,
    0.9470195f, 0.9460462f, 0.9450689f, 0.9440875f,
    0.9431020f, 0.9421124f, 0.9411186f, 0.9401206f,
    0.9391184f, 0.9381118f, 0.9371009f, 0.9360856f,
    0.9350659f, 0.9340417f, 0.9330131f, 0.9319798f,
    0.9309420f, 0.9298995f, 0.9288523f, 0.9278004f,
    0.9267436f, 0.9256821f, 0.9246156f, 0.9235442f,
    0.9224678f, 0.9213864f, 0.9202998f, 0.9192081f,
    0.9181112f, 0.9170091f, 0.9159016f, 0.9147887f,
    0.9136703f, 0.9125465f, 0.9114171f, 0.9102821f,
    0.9091414f, 0.9079949f, 0.9068427f, 0.9056845f,
    0.9045204f, 0.9033502f, 0.9021740f, 0.9009916f,
    0.8998029f, 0.8986080f, 0.8974066f, 0.8961988f,
    0.8949844f, 0.8900599f, 0.8824622f, 0.8759247f,
    0.8691861f, 0.8636406f, 0.8535788f, 0.8430189f,
    0.8286135f, 0.8149099f, 0.8002172f, 0.7780663f,
    0.7554750f, 0.7242125f, 0.6828239f, 0.6329169f,
    0.5592135f, 0.4551411f, 0.3298770f, 0.0000000f
  };
  
  /* Figure out how many msecs it takes to go from the initial decimal to our threshold. */
  float realAttack = attackTable[attack];
  int msecs = 0;

  const float VOLUME_DB_MIN = -90.4f;

  float vol = VOLUME_DB_MIN * 10.0f;
  while (vol <= -1.0f / 32.0f) {
    vol *= realAttack;
    msecs++;
  }
  rgn->attack_time = msecs / 1000.0;

  /* Scale is dB*10, so the first number is actually -72.3dB.
   * Minimum possible volume is -90.4dB. */
  const int16_t sustainTable[] = {
    -723, -722, -721, -651, -601, -562, -530, -503,
    -480, -460, -442, -425, -410, -396, -383, -371,
    -360, -349, -339, -330, -321, -313, -305, -297,
    -289, -282, -276, -269, -263, -257, -251, -245,
    -239, -234, -229, -224, -219, -214, -210, -205,
    -201, -196, -192, -188, -184, -180, -176, -173,
    -169, -165, -162, -158, -155, -152, -149, -145,
    -142, -139, -136, -133, -130, -127, -125, -122,
    -119, -116, -114, -111, -109, -106, -103, -101,
    -99,  -96,  -94,  -91,  -89,  -87,  -85,  -82,
    -80,  -78,  -76,  -74,  -72,  -70,  -68,  -66,
    -64,  -62,  -60,  -58,  -56,  -54,  -52,  -50,
    -49,  -47,  -45,  -43,  -42,  -40,  -38,  -36,
    -35,  -33,  -31,  -30,  -28,  -27,  -25,  -23,
    -22,  -20,  -19,  -17,  -16,  -14,  -13,  -11,
    -10,  -8,   -7,   -6,   -4,   -3,   -1,    0
  };

  float decayRate = GetFallingRate(decay);
  int16_t sustainLev = sustainTable[sustain];

  /* Decay time is the time it takes to get to the sustain level from max vol,
   * decaying by decayRate every 1ms. */
  rgn->decay_time = ((0.0f - sustainLev) / decayRate) / 1000.0;

  rgn->sustain_level = 1.0 - (sustainLev / VOLUME_DB_MIN);

  float releaseRate = GetFallingRate(release);

  /* Release time is the time it takes to get from sustain level to minimum volume. */
  rgn->release_time = ((sustainLev - VOLUME_DB_MIN) / releaseRate) / 1000.0;
}

bool RBNKInstr::LoadInstr() {
  AddHeader(dwOffset + 0x00, 8, L"Key Regions Table Header");

  std::vector<Region> keyRegions = EnumerateRegionTable(dwOffset);

  for (uint32_t i = 0; i < keyRegions.size(); i++) {
    Region keyRegion = keyRegions[i];
    uint8_t keyLow = (i > 0) ? keyRegions[i - 1].high + 1 : 0;
    uint8_t keyHigh = keyRegion.high;

    std::vector<Region> velRegions = EnumerateRegionTable(keyRegion.subRefOffs);
    for (uint32_t j = 0; j < velRegions.size(); j++) {
      Region velRegion = velRegions[j];
      uint8_t velLow = (j > 0) ? velRegions[j - 1].high + 1 : 0;
      uint8_t velHigh = velRegion.high;

      uint32_t offs = parInstrSet->dwOffset + GetWordBE(velRegion.subRefOffs + 0x04);
      VGMRgn *rgn = AddRgn(offs, 0, 0, keyLow, keyHigh, velLow, velHigh);
      LoadRgn(rgn);
    }
  }

  return true;
}

std::vector<RBNKInstr::Region> RBNKInstr::EnumerateRegionTable(uint32_t refOffset) {
  RegionSet regionSet = (RegionSet)GetByte(refOffset + 0x01);

  /* First, enumerate based on key. */
  switch (regionSet) {
  case RegionSet::DIRECT:
    return { { 0x7F, refOffset } };
  case RegionSet::RANGE: {
    uint32_t offs = parInstrSet->dwOffset + GetWordBE(refOffset + 0x04);
    uint8_t tableSize = GetByte(offs);
    uint32_t tableEnd = (offs + 1 + tableSize + 3) & ~3;

    std::vector<Region> table;
    for (uint8_t i = 0; i < tableSize; i++) {
      uint32_t hi = GetByte(offs + 1 + i);
      uint32_t subRefOffs = tableEnd + (i * 8);
      Region region = { hi, subRefOffs };
      table.push_back(region);
    }
    return table;
  }
  case RegionSet::INDEX: {
    uint32_t offs = parInstrSet->dwOffset + GetWordBE(refOffset + 0x04);
    uint8_t min = GetByte(offs + 0);
    uint8_t max = GetByte(offs + 1);
    uint32_t tableEnd = offs + 4;

    std::vector<Region> table;
    for (uint8_t i = min; i < max; i++) {
      uint8_t idx = i - min;
      uint32_t subRefOffs = tableEnd + (idx * 8);
      Region region = { i, subRefOffs };
      table.push_back(region);
    }
    return table;
  }
  case RegionSet::NONE:
    return {};
  default:
    assert(false && "Unsupported region");
    return {};
  }
}

void RBNKInstr::LoadRgn(VGMRgn *rgn) {
  uint32_t rgnBase = rgn->dwOffset;

  uint32_t waveIndex = GetWordBE(rgnBase + 0x00);
  rgn->AddSampNum(waveIndex, rgnBase + 0x00, 4);

  uint8_t a = GetByte(rgnBase + 0x04);
  uint8_t d = GetByte(rgnBase + 0x05);
  uint8_t s = GetByte(rgnBase + 0x06);
  uint8_t r = GetByte(rgnBase + 0x07);
  rgn->AddSimpleItem(rgnBase + 0x04, 1, L"Attack");
  rgn->AddSimpleItem(rgnBase + 0x05, 1, L"Decay");
  rgn->AddSimpleItem(rgnBase + 0x06, 1, L"Sustain");
  rgn->AddSimpleItem(rgnBase + 0x07, 1, L"Release");

  SetupEnvelope(rgn, a, d, s, r);

  rgn->AddSimpleItem(rgnBase + 0x08, 1, L"Hold");
  uint8_t waveDataLocationType = GetByte(rgnBase + 0x09);
  rgn->AddSimpleItem(rgnBase + 0x09, 1, L"Wave Data Location Type");
  assert(waveDataLocationType == 0x00 && "We only support INDEX wave data for now.");

  rgn->AddSimpleItem(rgnBase + 0x0A, 1, L"Note Off Type");
  rgn->AddSimpleItem(rgnBase + 0x0B, 1, L"Alternate Assign");
  rgn->AddUnityKey(GetByte(rgnBase + 0x0C), rgnBase + 0x0C);

  /* XXX: How do I transcribe volume? */
  rgn->AddVolume(GetByte(rgnBase + 0x0D) / 127.0, rgnBase + 0x0D);

  rgn->AddPan(GetByte(rgnBase + 0x0E), rgnBase + 0x0E);
  rgn->AddSimpleItem(rgnBase + 0x0F, 1, L"Padding");
  rgn->AddSimpleItem(rgnBase + 0x10, 4, L"Frequency multiplier");
  rgn->AddSimpleItem(rgnBase + 0x14, 8, L"LFO Table");
  rgn->AddSimpleItem(rgnBase + 0x1C, 8, L"Graph Env Table");
  rgn->AddSimpleItem(rgnBase + 0x24, 8, L"Randomizer Table");
  rgn->AddSimpleItem(rgnBase + 0x2C, 4, L"Reserved");
}

bool RSARInstrSet::GetInstrPointers() {
  VGMHeader *header = AddHeader(dwOffset, 0);

  uint32_t instTableCount = GetWordBE(dwOffset + 0x00);
  header->AddSimpleItem(dwOffset + 0x00, 4, L"Instrument Count");

  uint32_t instTableIdx = dwOffset + 0x04;
  for (uint32_t i = 0; i < instTableCount; i++) {
    /* Some RBNK files have NULL offsets here. Don't crash in that case. */
    uint32_t instKind = GetByte(instTableIdx + 0x01);
    if (instKind != 0x00)
      aInstrs.push_back(new RBNKInstr(this, instTableIdx, 0, i));
    instTableIdx += 0x08;
  }
  return true;
}

uint32_t RBNKSamp::DspAddressToSamples(uint32_t dspAddr) {
  switch (format) {
  case Format::ADPCM:
    return (dspAddr / 16) * 14 + (dspAddr % 16) - 2;
  case Format::PCM16:
  case Format::PCM8:
  default:
    return dspAddr;
  }
}

void RBNKSamp::ReadAdpcmParam(uint32_t adpcmParamBase, AdpcmParam *param) {
  for (int i = 0; i < 16; i++)
    param->coef[i] = GetShortBE(adpcmParamBase + i * 0x02);
  param->gain = GetShortBE(adpcmParamBase + 0x20);
  param->expectedScale = GetShortBE(adpcmParamBase + 0x22);
  param->yn1 = GetShortBE(adpcmParamBase + 0x24);
  param->yn2 = GetShortBE(adpcmParamBase + 0x26);

  /* Make sure that these are sane. */
  assert(param->gain == 0x00);
  assert(param->yn2 == 0x00);
  assert(param->yn1 == 0x00);
}

void RBNKSamp::Load() {
  format = (Format)GetByte(dwOffset + 0x00);

  switch (format) {
  case Format::PCM8:
    SetWaveType(WT_PCM8);
    SetBPS(8);
    break;
  case Format::PCM16:
  case Format::ADPCM:
    SetWaveType(WT_PCM16);
    SetBPS(16);
  }

  bool loop = !!GetByte(dwOffset + 0x01);

  SetNumChannels(GetByte(dwOffset + 0x02));
  assert((channels <= MAX_CHANNELS) && "We only support up to two channels.");

  uint8_t sampleRateH = GetByte(dwOffset + 0x03);
  uint16_t sampleRateL = GetShortBE(dwOffset + 0x04);
  SetRate((sampleRateH << 16) | sampleRateL);

  uint8_t dataLocationType = GetByte(dwOffset + 0x06);
  assert((dataLocationType == 0x00) && "We only support OFFSET-based wave data locations.");
  /* padding */

  uint32_t loopStart = GetWordBE(dwOffset + 0x08);
  uint32_t loopEnd = GetWordBE(dwOffset + 0x0C);

  /*
    static int nsmp = 0;
    char debug[255];
    sprintf(debug, "%d %d %d %d %d\n", nsmp++, DspAddressToSamples(loopStart), DspAddressToSamples(loopEnd),
    DspAddressToSamples(loopEnd) - DspAddressToSamples(loopStart),
    DspAddressToSamples(loopEnd - loopStart));
    OutputDebugStringA(debug);
  */

  SetLoopStatus(loop);
  if (loop) {
    SetLoopStartMeasure(LM_SAMPLES);
    SetLoopLengthMeasure(LM_SAMPLES);
    /* XXX: Not sure about these exactly but it makes Wii Shop sound correct.
     * Probably specific to PCM16 though... */
    SetLoopOffset(DspAddressToSamples(loopStart) - 2);
    SetLoopLength(DspAddressToSamples(loopEnd) - DspAddressToSamples(loopStart) + 2);
  }

  uint32_t channelInfoTableBase = dwOffset + GetWordBE(dwOffset + 0x10);
  for (int i = 0; i < channels; i++) {
    ChannelParam *channelParam = &channelParams[i];

    uint32_t channelInfoBase = dwOffset + GetWordBE(channelInfoTableBase + (i * 4));
    channelParam->dataOffset = GetWordBE(channelInfoBase + 0x00);

    uint32_t adpcmOffset = GetWordBE(channelInfoBase + 0x04);
    /* volumeFrontL, volumeFrontR, volumeRearL, volumeRearR */

    if (adpcmOffset != 0) {
      uint32_t adpcmBase = dwOffset + adpcmOffset;
      ReadAdpcmParam(adpcmBase, &channelParam->adpcmParam);
    }
  }

  uint32_t dataLocation = GetWordBE(dwOffset + 0x14);
  dataOff += dataLocation;

  /* Quite sure this maps to end bytes, even without looping. */
  dataLength = loopEnd;
  ulUncompressedSize = DspAddressToSamples(loopEnd) * (bps / 8) * channels;
}

static int16_t Clamp16(int32_t sample) {
  if (sample > 0x7FFF) return  0x7FFF;
  if (sample < -0x8000) return -0x8000;
  return sample;
}

void RBNKSamp::ConvertAdpcmChannel(ChannelParam *param, uint8_t *buf, int channelSpacing) {
  int16_t hist1 = param->adpcmParam.yn1, hist2 = param->adpcmParam.yn2;

  uint32_t samplesDone = 0;
  uint32_t numSamples = DspAddressToSamples(dataLength);

  /* Go over and convert each frame of 14 samples. */
  uint32_t srcOffs = dataOff + param->dataOffset;
  uint32_t dstOffs = 0;
  while (samplesDone < numSamples) {
    /* Each frame is 8 bytes -- a one-byte header, and then seven bytes
     * of samples. Each sample is 4 bits, so we have 14 samples per frame. */
    uint8_t frameHeader = GetByte(srcOffs++);

    /* Sanity check. */
    if (samplesDone == 0)
      assert(frameHeader == param->adpcmParam.expectedScale);

    int32_t scale = 1 << (frameHeader & 0x0F);
    uint8_t coefIdx = (frameHeader >> 4);
    int16_t coef1 = (int16_t)param->adpcmParam.coef[coefIdx * 2], coef2 = (int16_t)param->adpcmParam.coef[coefIdx * 2 + 1];

    for (int i = 0; i < 14; i++) {
      /* Pull out the correct byte and nibble. */
      uint8_t byte = GetByte(srcOffs + (i >> 1));
      uint8_t unsignedNibble = (i & 1) ? (byte & 0x0F) : (byte >> 4);

      /* Sign extend. */
      int8_t signedNibble = ((int8_t)(unsignedNibble << 4)) >> 4;

      /* Decode. */
      int16_t sample = Clamp16((((signedNibble * scale) << 11) + 1024 + (coef1*hist1 + coef2*hist2)) >> 11);
      ((int16_t *)buf)[dstOffs] = sample;
      dstOffs += channelSpacing;

      hist2 = hist1;
      hist1 = sample;

      samplesDone++;
      if (samplesDone >= numSamples)
        break;
    }

    srcOffs += 7;
  }
}

void RBNKSamp::ConvertPCM8Channel(ChannelParam *param, uint8_t *buf, int channelSpacing) {
  uint32_t srcOffs = dataOff + param->dataOffset;
  uint32_t numSamples = DspAddressToSamples(dataLength);
  GetBytes(srcOffs, numSamples, buf);

  /* PCM8 is signed */
  for (uint32_t i = 0; i < numSamples; i++)
    buf[i] ^= 0x80;
}

void RBNKSamp::ConvertPCM16Channel(ChannelParam *param, uint8_t *buf, int channelSpacing) {
  /* Do this the long way because of endianness issues... */

  uint32_t srcOffs = dataOff + param->dataOffset;
  uint32_t dstOffs = 0;

  uint32_t samplesDone = 0;
  uint32_t numSamples = DspAddressToSamples(dataLength);

  while (samplesDone < numSamples) {
    uint16_t sample = GetWordBE(srcOffs);
    srcOffs += 2;

    ((int16_t *)buf)[dstOffs] = sample;
    dstOffs += channelSpacing;

    samplesDone++;
  }
}

void RBNKSamp::ConvertChannel(ChannelParam *param, uint8_t *buf, int channelSpacing) {
  if (format == ADPCM)
    ConvertAdpcmChannel(param, buf, channelSpacing);
  else if (format == PCM8)
    ConvertPCM8Channel(param, buf, channelSpacing);
  else if (format == PCM16)
    ConvertPCM16Channel(param, buf, channelSpacing);
}

void RBNKSamp::ConvertToStdWave(uint8_t *buf) {
  int bytesPerSample = bps / 8;
  for (int i = 0; i < channels; i++)
    ConvertChannel(&channelParams[i], &buf[i*bytesPerSample], channels);
}

static FileRange CheckBlock(RawFile *file, uint32_t offs, const char *magic) {
  if (!file->MatchBytes(magic, offs))
    return { 0, 0 };

  uint32_t size = file->GetWordBE(offs + 0x04);
  return { offs + 0x08, size - 0x08 };
}

bool RSARSampCollWAVE::GetSampleInfo() {
  uint32_t waveTableCount = GetWordBE(dwOffset + 0x00);
  uint32_t waveTableIdx = dwOffset + 0x08;
  for (uint32_t i = 0; i < waveTableCount; i++) {
    uint32_t waveOffs = dwOffset + GetWordBE(waveTableIdx);
    wchar_t sampName[32];
    swprintf(sampName, sizeof(sampName) / sizeof(*sampName), L"Sample_%04d", i);
    samples.push_back(new RBNKSamp(this, waveOffs, 0, waveDataOffset, 0, sampName));
    waveTableIdx += 0x08;
  }
  return true;
}

VGMSamp * RSARSampCollRWAR::ParseRWAVFile(uint32_t offset, uint32_t index) {
  if (!rawfile->MatchBytes("RWAV\xFE\xFF\x01", offset))
    return nullptr;

  uint32_t infoBlockOffs = offset + GetWordBE(offset + 0x10);
  uint32_t dataBlockOffs = offset + GetWordBE(offset + 0x18);
  FileRange infoBlock = CheckBlock(rawfile, infoBlockOffs, "INFO");
  FileRange dataBlock = CheckBlock(rawfile, dataBlockOffs, "DATA");

  /* Bizarrely enough, dataLocation inside the WAVE info points to the start
   * of the DATA block header so add an extra 0x08. */
  uint32_t dataOffset = infoBlock.offset + 0x08;
  uint32_t dataSize = dataBlock.size + 0x08;

  wchar_t sampName[32];
  swprintf(sampName, sizeof(sampName) / sizeof(*sampName), L"Sample_%04d", index);
  return new RBNKSamp(this, infoBlock.offset, infoBlock.size, dataOffset, dataSize, sampName);
}

bool RSARSampCollRWAR::GetHeaderInfo() {
  if (!rawfile->MatchBytes("RWAR\xFE\xFF\x01", dwOffset))
    return false;

  unLength = GetWordBE(dwOffset + 0x08);

  return true;
}

bool RSARSampCollRWAR::GetSampleInfo() {
  uint32_t tablBlockOffs = dwOffset + GetWordBE(dwOffset + 0x10);
  uint32_t dataBlockOffs = dwOffset + GetWordBE(dwOffset + 0x18);
  FileRange tablBlock = CheckBlock(rawfile, tablBlockOffs, "TABL");

  uint32_t waveTableCount = GetWordBE(tablBlock.offset + 0x00);
  uint32_t waveTableIdx = tablBlock.offset + 0x08;
  for (uint32_t i = 0; i < waveTableCount; i++) {
    uint32_t rwavHeaderOffs = dataBlockOffs + GetWordBE(waveTableIdx);
    uint32_t rwavHeaderSize = GetWordBE(waveTableIdx + 0x04);
    samples.push_back(ParseRWAVFile(rwavHeaderOffs, i));
    waveTableIdx += 0x0C;
  }

  return true;
}
