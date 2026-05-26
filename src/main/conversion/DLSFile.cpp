/**
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "util/types.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <vector>
#include "ConversionContext.h"
#include "DLSFile.h"
#include "ScaleConversion.h"
#include "VGMInstrSet.h"
#include "VGMSamp.h"
#include "Root.h"

//  *******
//  DLSFile
//  *******

DLSFile::DLSFile(std::string dls_name) : RiffFile(std::move(dls_name), "DLS ") {}

std::vector<DLSInstr *> DLSFile::instruments() {
  std::vector<DLSInstr *> instrs(m_instrs.size());
  std::ranges::transform(m_instrs, instrs.begin(), [](auto &instr_pointer) {
    return instr_pointer.get();
  });

  return instrs;
}

std::vector<DLSWave *> DLSFile::waves() {
  std::vector<DLSWave *> waves(m_waves.size());
  std::ranges::transform(m_waves, waves.begin(), [](auto &wave_pointer) {
    return wave_pointer.get();
  });

  return waves;
}

DLSInstr *DLSFile::addInstr(unsigned long bank, unsigned long instrNum) {
  auto instr = m_instrs.emplace_back(std::make_unique<DLSInstr>(bank, instrNum)).get();
  return instr;
}

DLSInstr *DLSFile::addInstr(unsigned long bank, unsigned long instrNum, std::string instr_name) {
  auto instr = m_instrs.emplace_back(std::make_unique<DLSInstr>(bank, instrNum, instr_name)).get();
  return instr;
}

DLSWave *DLSFile::addWave(u16 formatTag, u16 channels, int samplesPerSec,
                          int aveBytesPerSec, u16 blockAlign, u16 bitsPerSample,
                          u32 waveDataSize, unsigned char *waveData, std::string wave_name) {
  auto wave = m_waves
                  .emplace_back(std::make_unique<DLSWave>(formatTag, channels, samplesPerSec,
                                                          aveBytesPerSec, blockAlign, bitsPerSample,
                                                          waveDataSize, waveData, wave_name))
                  .get();
  return wave;
}

// GetSize returns total DLS size, including the "RIFF" header size
u32 DLSFile::size() {
  u32 dls_size = 0;
  dls_size += 12;             // "RIFF" + size + "DLS "
  dls_size += COLH_SIZE;      // COLH chunk (collection chunk - tells how many instruments)
  dls_size += LIST_HDR_SIZE;  //"lins" list (list of instruments - contains all the "ins " lists)

  for (auto &instr : m_instrs) {
    dls_size += instr->size();
  }
  dls_size += 16;  // "ptbl" + size + cbSize + cCues
  dls_size += static_cast<u32>(m_waves.size()) * sizeof(u32);  // each wave gets a poolcue
  dls_size += LIST_HDR_SIZE;  //"wvpl" list (wave pool - contains all the "wave" lists)
  for (auto &wave : m_waves) {
    dls_size += wave->size();
  }
  dls_size += LIST_HDR_SIZE;                       //"INFO" list
  dls_size += 8;                                   //"INAM" + size
  dls_size += static_cast<u32>(name.size());  // size of name string

  return dls_size;
}

int DLSFile::writeDLSToBuffer(std::vector<u8> &buf) {
  pushTypeOnVectBE<u32>(buf, 0x52494646);   //"RIFF"
  pushTypeOnVect<u32>(buf, size() - 8);  // size
  pushTypeOnVectBE<u32>(buf, 0x444C5320);   //"DLS "

  pushTypeOnVectBE<u32>(buf, 0x636F6C68);  //"colh "
  pushTypeOnVect<u32>(buf, 4);             // size
  pushTypeOnVect<u32>(
      buf, static_cast<u32>(m_instrs.size()));  // cInstruments - number of instruments
  u32 theDWORD = 4;                                     // account for 4 "lins" bytes
  for (auto &instr : m_instrs)
    theDWORD += instr->size();        // each "ins " list
  writeLIST(buf, 0x6C696E73, theDWORD);  // Write the "lins" LIST
  for (auto &instr : m_instrs) {
    instr->write(buf);
  }

  pushTypeOnVectBE<u32>(buf, 0x7074626C);  //"ptbl"
  theDWORD = 8;
  theDWORD += static_cast<u32>(m_waves.size()) * sizeof(u32);  // each wave gets a poolcue
  pushTypeOnVect<u32>(buf, theDWORD);                              // size
  pushTypeOnVect<u32>(buf, 8);                                     // cbSize
  pushTypeOnVect<u32>(buf, static_cast<u32>(m_waves.size()));  // cCues

  theDWORD = 0;
  for (auto &wave : m_waves) {
    pushTypeOnVect<u32>(buf, theDWORD);  // write the poolcue for each sample
    theDWORD += wave->size();              // increment the offset to the next wave
  }

  theDWORD = 4;
  for (auto &wave : m_waves) {
    theDWORD += wave->size();  // each "wave" list
  }
  writeLIST(buf, 0x7776706C, theDWORD);  // Write the "wvpl" LIST
  for (auto &wave : m_waves) {
    wave->write(buf);  // Write each "wave" list
  }

  theDWORD = 12 + static_cast<u32>(name.size());  //"INFO" + "INAM" + size + the string size
  writeLIST(buf, 0x494E464F, theDWORD);                // write the "INFO" list
  pushTypeOnVectBE<u32>(buf, 0x494E414D);         //"INAM"
  pushTypeOnVect<u32>(buf, static_cast<u32>(name.size()));  // size
  pushBackStringOnVector(buf, name);                                  // The Instrument Name string

  return true;
}

// I should probably make this function part of a parent class for both Midi and DLS file
bool DLSFile::saveDLSFile(const std::filesystem::path &filepath) {
  std::vector<u8> dlsBuf;
  writeDLSToBuffer(dlsBuf);
  return pRoot->UI_writeBufferToFile(filepath, dlsBuf.data(), static_cast<u32>(dlsBuf.size()));
}

//  *******
//  DLSInstr
//  ********

DLSInstr::DLSInstr(u32 bank, u32 instrument)
    : ulBank(bank), ulInstrument(instrument), m_name("Untitled instrument") {
  RiffFile::alignName(m_name);
}

DLSInstr::DLSInstr(u32 bank, u32 instrument, std::string instrName)
    : ulBank(bank), ulInstrument(instrument), m_name(std::move(instrName)) {
  RiffFile::alignName(m_name);
}

u32 DLSInstr::size() const {
  u32 dls_size = 0;
  dls_size += LIST_HDR_SIZE;  //"ins " list
  dls_size += INSH_SIZE;      // insh chunk
  dls_size += LIST_HDR_SIZE;  //"lrgn" list
  for (auto &rgn : m_regions) {
    dls_size += rgn->size();
  }
  dls_size += LIST_HDR_SIZE;                       //"INFO" list
  dls_size += 8;                                   //"INAM" + size
  dls_size += static_cast<u32>(m_name.size());  // size of name string

  return dls_size;
}

void DLSInstr::write(std::vector<u8> &buf) {
  u32 temp = size() - 8;
  RiffFile::writeLIST(buf, 0x696E7320, temp);                          // write "ins " list
  pushTypeOnVectBE<u32>(buf, 0x696E7368);                         //"insh"
  pushTypeOnVect<u32>(buf, INSH_SIZE - 8);                        // size
  pushTypeOnVect<u32>(buf, static_cast<u32>(m_regions.size()));  // cRegions
  pushTypeOnVect<u32>(buf, ulBank);                               // ulBank
  pushTypeOnVect<u32>(buf, ulInstrument);                         // ulInstrument

  temp = 4;
  for (auto &rgn : m_regions) {
    temp += rgn->size();
  }
  RiffFile::writeLIST(buf, 0x6C72676E, temp);  // write the "lrgn" list
  for (auto &reg : m_regions) {
    reg->write(buf);
  }

  temp = 12 + static_cast<u32>(m_name.size());  //"INFO" + "INAM" + size + the string size
  RiffFile::writeLIST(buf, 0x494E464F, temp);      // write the "INFO" list
  pushTypeOnVectBE<u32>(buf, 0x494E414D);     //"INAM"

  temp = static_cast<u32>(m_name.size());
  pushTypeOnVect<u32>(buf, temp);  // size
  pushBackStringOnVector(buf, m_name);    // The Instrument Name string
}

DLSRgn *DLSInstr::addRgn() {
  auto reg = m_regions.emplace_back(std::make_unique<DLSRgn>()).get();
  return reg;
}

//  ******
//  DLSRgn
//  ******

u32 DLSRgn::size() const {
  u32 size = 0;
  size += LIST_HDR_SIZE;  //"rgn2" list
  size += RGNH_SIZE;      // rgnh chunk
  if (m_wsmp) {
    size += m_wsmp->size();
  }

  size += WLNK_SIZE;
  if (m_art) {
    size += m_art->GetSize();
  }

  return size;
}

void DLSRgn::write(std::vector<u8> &buf) const {
  RiffFile::writeLIST(buf, 0x72676E32, (size() - 8));  // write "rgn2" list
  pushTypeOnVectBE<u32>(buf, 0x72676E68);                      //"rgnh"
  pushTypeOnVect<u32>(buf, static_cast<u32>(RGNH_SIZE - 8));  // size
  pushTypeOnVect<u16>(buf, usKeyLow);                              // usLow  (key)
  pushTypeOnVect<u16>(buf, usKeyHigh);                             // usHigh (key)
  pushTypeOnVect<u16>(buf, usVelLow);                              // usLow  (vel)
  pushTypeOnVect<u16>(buf, usVelHigh);                             // usHigh (vel)
  pushTypeOnVect<u16>(buf, 1);                                 // fusOptions
  pushTypeOnVect<u16>(buf, 0);                                 // usKeyGroup

  // new for dls2
  pushTypeOnVect<u16>(buf, 1);  // NO CLUE

  if (m_wsmp)
    m_wsmp->write(buf);  // write the "wsmp" chunk

  pushTypeOnVectBE<u32>(buf, 0x776C6E6B);   //"wlnk"
  pushTypeOnVect<u32>(buf, WLNK_SIZE - 8);  // size
  pushTypeOnVect<u16>(buf, fusOptions);         // fusOptions
  pushTypeOnVect<u16>(buf, usPhaseGroup);       // usPhaseGroup
  pushTypeOnVect<u32>(buf, channel);            // ulChannel
  pushTypeOnVect<u32>(buf, tableIndex);         // ulTableIndex

  if (m_art)
    m_art->Write(buf);
}

DLSArt *DLSRgn::addArt() {
  m_art = std::make_unique<DLSArt>();
  return m_art.get();
}

DLSWsmp *DLSRgn::addWsmp() {
  m_wsmp = std::make_unique<DLSWsmp>();
  return m_wsmp.get();
}

void DLSRgn::setRanges(u16 keyLow, u16 keyHigh, u16 velLow, u16 velHigh) {
  usKeyLow = keyLow;
  usKeyHigh = keyHigh;
  usVelLow = velLow;
  usVelHigh = velHigh;
}

void DLSRgn::setWaveLinkInfo(u16 options, u16 phaseGroup, u32 theChannel,
                             u32 theTableIndex) {
  fusOptions = options;
  usPhaseGroup = phaseGroup;
  channel = theChannel;
  tableIndex = theTableIndex;
}

//  ******
//  DLSArt
//  ******

namespace {

std::optional<u16> dlsSourceForModSource(ModSource source) {
  if (auto controller = midiControllerForModSource(source)) {
    switch (*controller) {
      case 1:
        return CONN_SRC_CC1;
      case 7:
        return CONN_SRC_CC7;
      case 10:
        return CONN_SRC_CC10;
      case 11:
        return CONN_SRC_CC11;
      case 91:
        return CONN_SRC_CC91;
      case 93:
        return CONN_SRC_CC93;
      default:
        return std::nullopt;
    }
  }

  switch (source) {
    case ModSource::None:
      return std::nullopt;
    case ModSource::ChannelPressure:
      return CONN_SRC_CHANNELPRESSURE;
    case ModSource::PolyPressure:
      return CONN_SRC_POLYPRESSURE;
    case ModSource::PitchWheel:
      return CONN_SRC_PITCHWHEEL;
    default:
      break;
  }
  return std::nullopt;
}

ModSource sourceForModulator(const SynthModulator& modulator, const ConversionContext& context) {
  return modulator.source.value_or(context.synthSourceFor(SynthTarget::DLS, modulator.destination));
}

s32 toDls16Dot16Scale(s32 amount) {
  const s64 scaled = static_cast<s64>(amount) * 65536;
  return static_cast<s32>(std::clamp<s64>(
      scaled, std::numeric_limits<s32>::min(), std::numeric_limits<s32>::max()));
}

} // namespace

u32 DLSArt::GetSize() const {
  u32 size = 0;
  size += LIST_HDR_SIZE;  //"lar2" list
  size += 16;             //"art2" chunk + size + cbSize + cConnectionBlocks
  for (auto &block : m_blocks)
    size += block->size();  // each connection block
  return size;
}

void DLSArt::Write(std::vector<u8> &buf) const {
  RiffFile::writeLIST(buf, 0x6C617232, GetSize() - 8);                       // write "lar2" list
  pushTypeOnVectBE<u32>(buf, 0x61727432);                               //"art2"
  pushTypeOnVect<u32>(buf, GetSize() - LIST_HDR_SIZE - 8);              // size
  pushTypeOnVect<u32>(buf, 8);                                          // cbSize
  pushTypeOnVect<u32>(buf, static_cast<u32>(m_blocks.size()));  // cConnectionBlocks
  for (auto &block : m_blocks) {
    block->write(buf);  // each connection block
  }
}

void DLSArt::addADSR(long attack_time, u16 atk_transform, long hold_time, long decay_time,
                     long sustain_lev, long release_time, u16 rls_transform) {
  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_ATTACKTIME, atk_transform, attack_time));

  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_HOLDTIME, CONN_TRN_NONE,hold_time));

  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_DECAYTIME, CONN_TRN_NONE, decay_time));

  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_SUSTAINLEVEL, CONN_TRN_NONE, sustain_lev));

  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_RELEASETIME, rls_transform, release_time));
}

void DLSArt::addPan(long pan) {
  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(CONN_SRC_NONE, CONN_SRC_NONE,
                                                             CONN_DST_PAN, CONN_TRN_NONE, pan));
}

void DLSArt::addVibrato(s32 depth, s32 frequency, s32 delay) {
  if (depth != 0) {
    m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
        CONN_SRC_VIBRATO, CONN_SRC_NONE, CONN_DST_PITCH, CONN_TRN_NONE, depth));
  }

  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_VIB_FREQUENCY, CONN_TRN_NONE, frequency));

  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_VIB_STARTDELAY, CONN_TRN_NONE, delay));
}

void DLSArt::addGenerator(const SynthGenerator& generator) {
  switch (generator.destination) {
    case ModDest::VibLfoToPitch:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          CONN_SRC_VIBRATO, CONN_SRC_NONE, CONN_DST_PITCH, CONN_TRN_NONE,
          centsToDlsPitchScale(generator.amount)));
      break;
    case ModDest::VibLfoFreq:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_VIB_FREQUENCY, CONN_TRN_NONE,
          centsToDlsPitchScale(generator.amount)));
      break;
    case ModDest::VibLfoDelay:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_VIB_STARTDELAY, CONN_TRN_NONE,
          toDls16Dot16Scale(generator.amount)));
      break;
    case ModDest::ModLfoToVol:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          CONN_SRC_LFO, CONN_SRC_NONE, CONN_DST_ATTENUATION, CONN_TRN_NONE,
          toDls16Dot16Scale(generator.amount)));
      break;
    case ModDest::ModLfoFreq:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_LFO_FREQUENCY, CONN_TRN_NONE,
          centsToDlsPitchScale(generator.amount)));
      break;
    case ModDest::ModLfoDelay:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_LFO_STARTDELAY, CONN_TRN_NONE,
          toDls16Dot16Scale(generator.amount)));
      break;
    case ModDest::InitialAtten:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_ATTENUATION, CONN_TRN_NONE,
          toDls16Dot16Scale(generator.amount)));
      break;
  }
}

void DLSArt::addModulator(const SynthModulator& modulator, const ConversionContext& context) {
  const auto source = dlsSourceForModSource(sourceForModulator(modulator, context));
  if (!source.has_value()) {
    return;
  }

  switch (modulator.destination) {
    case ModDest::VibLfoToPitch:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          CONN_SRC_VIBRATO, *source, CONN_DST_PITCH, CONN_TRN_NONE,
          centsToDlsPitchScale(modulator.amount)));
      break;
    case ModDest::VibLfoFreq:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          *source, CONN_SRC_NONE, CONN_DST_VIB_FREQUENCY, CONN_TRN_NONE,
          centsToDlsPitchScale(modulator.amount)));
      break;
    case ModDest::VibLfoDelay:
      // DLS does not have a portable equivalent to SF2's controller-driven vibrato delay path.
      // Keep static vibrato delay generators, but ignore delay modulators.
      break;
    case ModDest::ModLfoFreq:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          *source, CONN_SRC_NONE, CONN_DST_LFO_FREQUENCY, CONN_TRN_NONE,
          centsToDlsPitchScale(modulator.amount)));
      break;
    case ModDest::ModLfoDelay:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          *source, CONN_SRC_NONE, CONN_DST_LFO_STARTDELAY, CONN_TRN_NONE,
          toDls16Dot16Scale(modulator.amount)));
      break;
    case ModDest::ModLfoToVol:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          CONN_SRC_LFO, *source, CONN_DST_ATTENUATION, CONN_TRN_NONE,
          toDls16Dot16Scale(modulator.amount)));
      break;
    case ModDest::InitialAtten:
      m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
          *source, CONN_SRC_NONE, CONN_DST_ATTENUATION, CONN_TRN_NONE,
          toDls16Dot16Scale(modulator.amount)));
      break;
  }
}

//  ***************
//  ConnectionBlock
//  ***************

void ConnectionBlock::write(std::vector<u8> &buf) const {
  pushTypeOnVect<u16>(buf, usSource);       // usSource
  pushTypeOnVect<u16>(buf, usControl);      // usControl
  pushTypeOnVect<u16>(buf, usDestination);  // usDestination
  pushTypeOnVect<u16>(buf, usTransform);    // usTransform
  pushTypeOnVect<s32>(buf, lScale);          // lScale
}

//  *******
//  DLSWsmp
//  *******

u32 DLSWsmp::size() const {
  u32 size = 0;
  size += 28;  // all the variables minus the loop info
  if (cSampleLoops)
    size += 16;  // plus the loop info
  return size;
}

void DLSWsmp::write(std::vector<u8> &buf) const {
  pushTypeOnVectBE<u32>(buf, 0x77736D70);   //"wsmp"
  pushTypeOnVect<u32>(buf, size() - 8);  // size
  pushTypeOnVect<u32>(buf, 20);             // cbSize (size of structure without loop record)
  pushTypeOnVect<u16>(buf, usUnityNote);    // usUnityNote
  pushTypeOnVect<s16>(buf, sFineTune);       // sFineTune
  pushTypeOnVect<s32>(buf, lAttenuation);    // lAttenuation
  pushTypeOnVect<u32>(buf, 1);              // fulOptions
  pushTypeOnVect<u32>(buf, cSampleLoops);   // cSampleLoops
  if (cSampleLoops)                              // if it loops, write the loop structure
  {
    pushTypeOnVect<u32>(buf, 16);
    pushTypeOnVect<u32>(buf, ulLoopType);  // ulLoopType
    pushTypeOnVect<u32>(buf, ulLoopStart);
    pushTypeOnVect<u32>(buf, ulLoopLength);
  }
}

void DLSWsmp::setLoopInfo(Loop &loop, VGMSamp *samp) {
  const int origFormatBytesPerSamp = samp->bytesPerSample();
  double compressionRatio = samp->compressionRatio();

  // If the sample loops, but the loop length is 0, then assume the length should
  // extend to the end of the sample.
  if (loop.loopStatus && loop.loopLength == 0)
    loop.loopLength = samp->dataLength - loop.loopStart;

  cSampleLoops = loop.loopStatus;
  ulLoopType = loop.loopType;
  // In DLS, the value is in number of samples
  // if the val is a raw offset of the original format, multiply it by the compression ratio
  ulLoopStart = (loop.loopStartMeasure == LM_BYTES)
                    ? static_cast<u32>((loop.loopStart * compressionRatio) / origFormatBytesPerSamp)
                    : loop.loopStart;

  ulLoopLength = (loop.loopLengthMeasure == LM_BYTES)
                     ? static_cast<u32>((loop.loopLength * compressionRatio) / origFormatBytesPerSamp)
                     : loop.loopLength;
}

void DLSWsmp::setPitchInfo(u16 unityNote, s16 fineTune, s32 attenuation) {
  usUnityNote = unityNote;
  sFineTune = fineTune;
  lAttenuation = attenuation;
}

//  *******
//  DLSWave
//  *******

u32 DLSWave::size() const {
  u32 size = 0;
  size += LIST_HDR_SIZE;          //"wave" list
  size += 8;                      //"fmt " chunk + size
  size += 18;                     // fmt chunk data
  size += 8;                      //"data" chunk + size
  size += this->sampleSize();  // dataSize;			    //size of sample data

  size += LIST_HDR_SIZE;                       //"INFO" list
  size += 8;                                   //"INAM" + size
  size += static_cast<u32>(m_name.size());  // size of name string
  return size;
}

void DLSWave::write(std::vector<u8> &buf) {
  RiffFile::writeLIST(buf, 0x77617665, size() - 8);  // write "wave" list
  pushTypeOnVectBE<u32>(buf, 0x666D7420);          //"fmt "
  pushTypeOnVect<u32>(buf, 18);                    // size
  pushTypeOnVect<u16>(buf, wFormatTag);            // wFormatTag
  pushTypeOnVect<u16>(buf, wChannels);             // wChannels
  pushTypeOnVect<u32>(buf, dwSamplesPerSec);       // dwSamplesPerSec
  pushTypeOnVect<u32>(buf, dwAveBytesPerSec);      // dwAveBytesPerSec
  pushTypeOnVect<u16>(buf, wBlockAlign);           // wBlockAlign
  pushTypeOnVect<u16>(buf, wBitsPerSample);        // wBitsPerSample

  pushTypeOnVect<u16>(buf, 0);  // cbSize DLS2 specific.  I don't know anything else

  pushTypeOnVectBE<u32>(buf, 0x64617461);  // "data"
  /* size: this is the ACTUAL size, not the even-aligned size */
  pushTypeOnVect<u32>(buf, static_cast<u32>(m_wave_data.size()));
  buf.insert(buf.end(), std::begin(m_wave_data), std::end(m_wave_data));  // Write the sample
  if (m_wave_data.size() % 2) {
    buf.push_back(0);
  }

  u32 info_sig =
      12 + static_cast<u32>(m_name.size());     //"INFO" + "INAM" + size + the string size
  RiffFile::writeLIST(buf, 0x494E464F, info_sig);  // write the "INFO" list
  pushTypeOnVectBE<u32>(buf, 0x494E414D);     //"INAM"
  pushTypeOnVect<u32>(buf, static_cast<u32>(m_name.size()));  // size
  pushBackStringOnVector(buf, m_name);
}
