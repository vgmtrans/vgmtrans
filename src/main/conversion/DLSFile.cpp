/**
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <algorithm>
#include <numeric>
#include <ranges>
#include <vector>
#include "DLSFile.h"
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

DLSWave *DLSFile::addWave(uint16_t formatTag, uint16_t channels, int samplesPerSec,
                          int aveBytesPerSec, uint16_t blockAlign, uint16_t bitsPerSample,
                          uint32_t waveDataSize, unsigned char *waveData, std::string wave_name) {
  auto wave = m_waves
                  .emplace_back(std::make_unique<DLSWave>(formatTag, channels, samplesPerSec,
                                                          aveBytesPerSec, blockAlign, bitsPerSample,
                                                          waveDataSize, waveData, wave_name))
                  .get();
  return wave;
}

// GetSize returns total DLS size, including the "RIFF" header size
uint32_t DLSFile::size() {
  uint32_t dls_size = 0;
  dls_size += 12;             // "RIFF" + size + "DLS "
  dls_size += COLH_SIZE;      // COLH chunk (collection chunk - tells how many instruments)
  dls_size += LIST_HDR_SIZE;  //"lins" list (list of instruments - contains all the "ins " lists)

  for (auto &instr : m_instrs) {
    dls_size += instr->size();
  }
  dls_size += 16;  // "ptbl" + size + cbSize + cCues
  dls_size += static_cast<uint32_t>(m_waves.size()) * sizeof(uint32_t);  // each wave gets a poolcue
  dls_size += LIST_HDR_SIZE;  //"wvpl" list (wave pool - contains all the "wave" lists)
  for (auto &wave : m_waves) {
    dls_size += wave->size();
  }
  dls_size += LIST_HDR_SIZE;                       //"INFO" list
  dls_size += 8;                                   //"INAM" + size
  dls_size += static_cast<uint32_t>(name.size());  // size of name string

  return dls_size;
}

int DLSFile::writeDLSToBuffer(std::vector<uint8_t> &buf) {
  pushTypeOnVectBE<uint32_t>(buf, 0x52494646);   //"RIFF"
  pushTypeOnVect<uint32_t>(buf, size() - 8);  // size
  pushTypeOnVectBE<uint32_t>(buf, 0x444C5320);   //"DLS "

  pushTypeOnVectBE<uint32_t>(buf, 0x636F6C68);  //"colh "
  pushTypeOnVect<uint32_t>(buf, 4);             // size
  pushTypeOnVect<uint32_t>(
      buf, static_cast<uint32_t>(m_instrs.size()));  // cInstruments - number of instruments
  uint32_t theDWORD = 4;                                     // account for 4 "lins" bytes
  for (auto &instr : m_instrs)
    theDWORD += instr->size();        // each "ins " list
  writeLIST(buf, 0x6C696E73, theDWORD);  // Write the "lins" LIST
  for (auto &instr : m_instrs) {
    instr->write(buf);
  }

  pushTypeOnVectBE<uint32_t>(buf, 0x7074626C);  //"ptbl"
  theDWORD = 8;
  theDWORD += static_cast<uint32_t>(m_waves.size()) * sizeof(uint32_t);  // each wave gets a poolcue
  pushTypeOnVect<uint32_t>(buf, theDWORD);                              // size
  pushTypeOnVect<uint32_t>(buf, 8);                                     // cbSize
  pushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(m_waves.size()));  // cCues

  theDWORD = 0;
  for (auto &wave : m_waves) {
    pushTypeOnVect<uint32_t>(buf, theDWORD);  // write the poolcue for each sample
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

  theDWORD = 12 + static_cast<uint32_t>(name.size());  //"INFO" + "INAM" + size + the string size
  writeLIST(buf, 0x494E464F, theDWORD);                // write the "INFO" list
  pushTypeOnVectBE<uint32_t>(buf, 0x494E414D);         //"INAM"
  pushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(name.size()));  // size
  pushBackStringOnVector(buf, name);                                  // The Instrument Name string

  return true;
}

// I should probably make this function part of a parent class for both Midi and DLS file
bool DLSFile::saveDLSFile(const std::filesystem::path &filepath) {
  std::vector<uint8_t> dlsBuf;
  writeDLSToBuffer(dlsBuf);
  return pRoot->UI_writeBufferToFile(filepath, dlsBuf.data(), static_cast<uint32_t>(dlsBuf.size()));
}

//  *******
//  DLSInstr
//  ********

DLSInstr::DLSInstr(uint32_t bank, uint32_t instrument)
    : ulBank(bank), ulInstrument(instrument), m_name("Untitled instrument") {
  RiffFile::alignName(m_name);
}

DLSInstr::DLSInstr(uint32_t bank, uint32_t instrument, std::string instrName)
    : ulBank(bank), ulInstrument(instrument), m_name(std::move(instrName)) {
  RiffFile::alignName(m_name);
}

uint32_t DLSInstr::size() const {
  uint32_t dls_size = 0;
  dls_size += LIST_HDR_SIZE;  //"ins " list
  dls_size += INSH_SIZE;      // insh chunk
  dls_size += LIST_HDR_SIZE;  //"lrgn" list
  for (auto &rgn : m_regions) {
    dls_size += rgn->size();
  }
  dls_size += LIST_HDR_SIZE;                       //"INFO" list
  dls_size += 8;                                   //"INAM" + size
  dls_size += static_cast<uint32_t>(m_name.size());  // size of name string

  return dls_size;
}

void DLSInstr::write(std::vector<uint8_t> &buf) {
  uint32_t temp = size() - 8;
  RiffFile::writeLIST(buf, 0x696E7320, temp);                          // write "ins " list
  pushTypeOnVectBE<uint32_t>(buf, 0x696E7368);                         //"insh"
  pushTypeOnVect<uint32_t>(buf, INSH_SIZE - 8);                        // size
  pushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(m_regions.size()));  // cRegions
  pushTypeOnVect<uint32_t>(buf, ulBank);                               // ulBank
  pushTypeOnVect<uint32_t>(buf, ulInstrument);                         // ulInstrument

  temp = 4;
  for (auto &rgn : m_regions) {
    temp += rgn->size();
  }
  RiffFile::writeLIST(buf, 0x6C72676E, temp);  // write the "lrgn" list
  for (auto &reg : m_regions) {
    reg->write(buf);
  }

  temp = 12 + static_cast<uint32_t>(m_name.size());  //"INFO" + "INAM" + size + the string size
  RiffFile::writeLIST(buf, 0x494E464F, temp);      // write the "INFO" list
  pushTypeOnVectBE<uint32_t>(buf, 0x494E414D);     //"INAM"

  temp = static_cast<uint32_t>(m_name.size());
  pushTypeOnVect<uint32_t>(buf, temp);  // size
  pushBackStringOnVector(buf, m_name);    // The Instrument Name string
}

DLSRgn *DLSInstr::addRgn() {
  auto reg = m_regions.emplace_back(std::make_unique<DLSRgn>()).get();
  return reg;
}

//  ******
//  DLSRgn
//  ******

uint32_t DLSRgn::size() const {
  uint32_t size = 0;
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

void DLSRgn::write(std::vector<uint8_t> &buf) const {
  RiffFile::writeLIST(buf, 0x72676E32, (size() - 8));  // write "rgn2" list
  pushTypeOnVectBE<uint32_t>(buf, 0x72676E68);                      //"rgnh"
  pushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(RGNH_SIZE - 8));  // size
  pushTypeOnVect<uint16_t>(buf, usKeyLow);                              // usLow  (key)
  pushTypeOnVect<uint16_t>(buf, usKeyHigh);                             // usHigh (key)
  pushTypeOnVect<uint16_t>(buf, usVelLow);                              // usLow  (vel)
  pushTypeOnVect<uint16_t>(buf, usVelHigh);                             // usHigh (vel)
  pushTypeOnVect<uint16_t>(buf, 1);                                 // fusOptions
  pushTypeOnVect<uint16_t>(buf, 0);                                 // usKeyGroup

  // new for dls2
  pushTypeOnVect<uint16_t>(buf, 1);  // NO CLUE

  if (m_wsmp)
    m_wsmp->write(buf);  // write the "wsmp" chunk

  pushTypeOnVectBE<uint32_t>(buf, 0x776C6E6B);   //"wlnk"
  pushTypeOnVect<uint32_t>(buf, WLNK_SIZE - 8);  // size
  pushTypeOnVect<uint16_t>(buf, fusOptions);         // fusOptions
  pushTypeOnVect<uint16_t>(buf, usPhaseGroup);       // usPhaseGroup
  pushTypeOnVect<uint32_t>(buf, channel);            // ulChannel
  pushTypeOnVect<uint32_t>(buf, tableIndex);         // ulTableIndex

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

void DLSRgn::setRanges(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh) {
  usKeyLow = keyLow;
  usKeyHigh = keyHigh;
  usVelLow = velLow;
  usVelHigh = velHigh;
}

void DLSRgn::setWaveLinkInfo(uint16_t options, uint16_t phaseGroup, uint32_t theChannel,
                             uint32_t theTableIndex) {
  fusOptions = options;
  usPhaseGroup = phaseGroup;
  channel = theChannel;
  tableIndex = theTableIndex;
}

//  ******
//  DLSArt
//  ******

uint32_t DLSArt::GetSize() const {
  uint32_t size = 0;
  size += LIST_HDR_SIZE;  //"lar2" list
  size += 16;             //"art2" chunk + size + cbSize + cConnectionBlocks
  for (auto &block : m_blocks)
    size += block->size();  // each connection block
  return size;
}

void DLSArt::Write(std::vector<uint8_t> &buf) const {
  RiffFile::writeLIST(buf, 0x6C617232, GetSize() - 8);                       // write "lar2" list
  pushTypeOnVectBE<uint32_t>(buf, 0x61727432);                               //"art2"
  pushTypeOnVect<uint32_t>(buf, GetSize() - LIST_HDR_SIZE - 8);              // size
  pushTypeOnVect<uint32_t>(buf, 8);                                          // cbSize
  pushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(m_blocks.size()));  // cConnectionBlocks
  for (auto &block : m_blocks) {
    block->write(buf);  // each connection block
  }
}

void DLSArt::addADSR(long attack_time, uint16_t atk_transform, long hold_time, long decay_time,
                     long sustain_lev, long release_time, uint16_t rls_transform) {
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

//  ***************
//  ConnectionBlock
//  ***************

void ConnectionBlock::write(std::vector<uint8_t> &buf) const {
  pushTypeOnVect<uint16_t>(buf, usSource);       // usSource
  pushTypeOnVect<uint16_t>(buf, usControl);      // usControl
  pushTypeOnVect<uint16_t>(buf, usDestination);  // usDestination
  pushTypeOnVect<uint16_t>(buf, usTransform);    // usTransform
  pushTypeOnVect<int32_t>(buf, lScale);          // lScale
}

//  *******
//  DLSWsmp
//  *******

uint32_t DLSWsmp::size() const {
  uint32_t size = 0;
  size += 28;  // all the variables minus the loop info
  if (cSampleLoops)
    size += 16;  // plus the loop info
  return size;
}

void DLSWsmp::write(std::vector<uint8_t> &buf) const {
  pushTypeOnVectBE<uint32_t>(buf, 0x77736D70);   //"wsmp"
  pushTypeOnVect<uint32_t>(buf, size() - 8);  // size
  pushTypeOnVect<uint32_t>(buf, 20);             // cbSize (size of structure without loop record)
  pushTypeOnVect<uint16_t>(buf, usUnityNote);    // usUnityNote
  pushTypeOnVect<int16_t>(buf, sFineTune);       // sFineTune
  pushTypeOnVect<int32_t>(buf, lAttenuation);    // lAttenuation
  pushTypeOnVect<uint32_t>(buf, 1);              // fulOptions
  pushTypeOnVect<uint32_t>(buf, cSampleLoops);   // cSampleLoops
  if (cSampleLoops)                              // if it loops, write the loop structure
  {
    pushTypeOnVect<uint32_t>(buf, 16);
    pushTypeOnVect<uint32_t>(buf, ulLoopType);  // ulLoopType
    pushTypeOnVect<uint32_t>(buf, ulLoopStart);
    pushTypeOnVect<uint32_t>(buf, ulLoopLength);
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
                    ? static_cast<uint32_t>((loop.loopStart * compressionRatio) / origFormatBytesPerSamp)
                    : loop.loopStart;

  ulLoopLength = (loop.loopLengthMeasure == LM_BYTES)
                     ? static_cast<uint32_t>((loop.loopLength * compressionRatio) / origFormatBytesPerSamp)
                     : loop.loopLength;
}

void DLSWsmp::setPitchInfo(uint16_t unityNote, int16_t fineTune, int32_t attenuation) {
  usUnityNote = unityNote;
  sFineTune = fineTune;
  lAttenuation = attenuation;
}

//  *******
//  DLSWave
//  *******

uint32_t DLSWave::size() const {
  uint32_t size = 0;
  size += LIST_HDR_SIZE;          //"wave" list
  size += 8;                      //"fmt " chunk + size
  size += 18;                     // fmt chunk data
  size += 8;                      //"data" chunk + size
  size += this->sampleSize();  // dataSize;			    //size of sample data

  size += LIST_HDR_SIZE;                       //"INFO" list
  size += 8;                                   //"INAM" + size
  size += static_cast<uint32_t>(m_name.size());  // size of name string
  return size;
}

void DLSWave::write(std::vector<uint8_t> &buf) {
  RiffFile::writeLIST(buf, 0x77617665, size() - 8);  // write "wave" list
  pushTypeOnVectBE<uint32_t>(buf, 0x666D7420);          //"fmt "
  pushTypeOnVect<uint32_t>(buf, 18);                    // size
  pushTypeOnVect<uint16_t>(buf, wFormatTag);            // wFormatTag
  pushTypeOnVect<uint16_t>(buf, wChannels);             // wChannels
  pushTypeOnVect<uint32_t>(buf, dwSamplesPerSec);       // dwSamplesPerSec
  pushTypeOnVect<uint32_t>(buf, dwAveBytesPerSec);      // dwAveBytesPerSec
  pushTypeOnVect<uint16_t>(buf, wBlockAlign);           // wBlockAlign
  pushTypeOnVect<uint16_t>(buf, wBitsPerSample);        // wBitsPerSample

  pushTypeOnVect<uint16_t>(buf, 0);  // cbSize DLS2 specific.  I don't know anything else

  pushTypeOnVectBE<uint32_t>(buf, 0x64617461);  // "data"
  /* size: this is the ACTUAL size, not the even-aligned size */
  pushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(m_wave_data.size()));
  buf.insert(buf.end(), std::begin(m_wave_data), std::end(m_wave_data));  // Write the sample
  if (m_wave_data.size() % 2) {
    buf.push_back(0);
  }

  uint32_t info_sig =
      12 + static_cast<uint32_t>(m_name.size());     //"INFO" + "INAM" + size + the string size
  RiffFile::writeLIST(buf, 0x494E464F, info_sig);  // write the "INFO" list
  pushTypeOnVectBE<uint32_t>(buf, 0x494E414D);     //"INAM"
  pushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(m_name.size()));  // size
  pushBackStringOnVector(buf, m_name);
}
