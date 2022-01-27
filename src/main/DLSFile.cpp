/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "pch.h"
#include <algorithm>
#include <memory>
#include <numeric>
#include "DLSFile.h"
#include "VGMInstrSet.h"
#include "VGMSamp.h"
#include "Root.h"

using namespace std;

//  *******
//  DLSFile
//  *******

DLSFile::DLSFile(string dls_name) : RiffFile(dls_name, "DLS ") {
}

std::vector<DLSInstr *> DLSFile::GetInstruments() {
  std::vector<DLSInstr *> instrs(m_instrs.size());
  std::transform(std::begin(m_instrs), std::end(m_instrs), std::begin(instrs), [](auto &instr_pointer) {
    return instr_pointer.get();
  });

  return instrs;
}

std::vector<DLSWave *> DLSFile::GetWaves() {
  std::vector<DLSWave *> waves(m_waves.size());
  std::transform(std::begin(m_waves), std::end(m_waves), std::begin(waves), [](auto &wave_pointer) {
    return wave_pointer.get();
  });

  return waves;
}

DLSInstr *DLSFile::AddInstr(unsigned long bank, unsigned long instrNum) {
  auto instr = m_instrs.emplace_back(std::make_unique<DLSInstr>(bank, instrNum)).get();
  return instr;
}

DLSInstr *DLSFile::AddInstr(unsigned long bank, unsigned long instrNum, std::string instr_name) {
  auto instr = m_instrs.emplace_back(std::make_unique<DLSInstr>(bank, instrNum, instr_name)).get();
  return instr;
}

void DLSFile::DeleteInstr(unsigned long, unsigned long) {
}

DLSWave *DLSFile::AddWave(uint16_t formatTag, uint16_t channels, int samplesPerSec,
                          int aveBytesPerSec, uint16_t blockAlign, uint16_t bitsPerSample,
                          uint32_t waveDataSize, unsigned char *waveData, string wave_name) {
  auto wave = m_waves
                  .emplace_back(std::make_unique<DLSWave>(formatTag, channels, samplesPerSec,
                                                          aveBytesPerSec, blockAlign, bitsPerSample,
                                                          waveDataSize, waveData, wave_name))
                  .get();
  return wave;
}

// GetSize returns total DLS size, including the "RIFF" header size
uint32_t DLSFile::GetSize() {
  uint32_t dls_size = 0;
  dls_size += 12;             // "RIFF" + size + "DLS "
  dls_size += COLH_SIZE;      // COLH chunk (collection chunk - tells how many instruments)
  dls_size += LIST_HDR_SIZE;  //"lins" list (list of instruments - contains all the "ins " lists)

  for (auto &instr : m_instrs) {
    dls_size += instr->GetSize();
  }
  dls_size += 16;  // "ptbl" + size + cbSize + cCues
  dls_size += static_cast<uint32_t>(m_waves.size()) * sizeof(uint32_t);  // each wave gets a poolcue
  dls_size += LIST_HDR_SIZE;  //"wvpl" list (wave pool - contains all the "wave" lists)
  for (auto &wave : m_waves) {
    dls_size += wave->GetSize();
  }
  dls_size += LIST_HDR_SIZE;                       //"INFO" list
  dls_size += 8;                                   //"INAM" + size
  dls_size += static_cast<uint32_t>(name.size());  // size of name string

  return dls_size;
}

int DLSFile::WriteDLSToBuffer(vector<uint8_t> &buf) {
  uint32_t theDWORD;

  PushTypeOnVectBE<uint32_t>(buf, 0x52494646);   //"RIFF"
  PushTypeOnVect<uint32_t>(buf, GetSize() - 8);  // size
  PushTypeOnVectBE<uint32_t>(buf, 0x444C5320);   //"DLS "

  PushTypeOnVectBE<uint32_t>(buf, 0x636F6C68);  //"colh "
  PushTypeOnVect<uint32_t>(buf, 4);             // size
  PushTypeOnVect<uint32_t>(
      buf, static_cast<uint32_t>(m_instrs.size()));  // cInstruments - number of instruments
  theDWORD = 4;                                     // account for 4 "lins" bytes
  for (auto &instr : m_instrs)
    theDWORD += instr->GetSize();        // each "ins " list
  WriteLIST(buf, 0x6C696E73, theDWORD);  // Write the "lins" LIST
  for (auto &instr : m_instrs) {
    instr->Write(buf);
  }

  PushTypeOnVectBE<uint32_t>(buf, 0x7074626C);  //"ptbl"
  theDWORD = 8;
  theDWORD += static_cast<uint32_t>(m_waves.size()) * sizeof(uint32_t);  // each wave gets a poolcue
  PushTypeOnVect<uint32_t>(buf, theDWORD);                              // size
  PushTypeOnVect<uint32_t>(buf, 8);                                     // cbSize
  PushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(m_waves.size()));  // cCues

  theDWORD = 0;
  for (auto &wave : m_waves) {
    PushTypeOnVect<uint32_t>(buf, theDWORD);  // write the poolcue for each sample
    theDWORD += wave->GetSize();              // increment the offset to the next wave
  }

  theDWORD = 4;
  for (auto &wave : m_waves) {
    theDWORD += wave->GetSize();  // each "wave" list
  }
  WriteLIST(buf, 0x7776706C, theDWORD);  // Write the "wvpl" LIST
  for (auto &wave : m_waves) {
    wave->Write(buf);  // Write each "wave" list
  }

  theDWORD = 12 + static_cast<uint32_t>(name.size());  //"INFO" + "INAM" + size + the string size
  WriteLIST(buf, 0x494E464F, theDWORD);                // write the "INFO" list
  PushTypeOnVectBE<uint32_t>(buf, 0x494E414D);         //"INAM"
  PushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(name.size()));  // size
  PushBackStringOnVector(buf, name);                                  // The Instrument Name string

  return true;
}

// I should probably make this function part of a parent class for both Midi and DLS file
bool DLSFile::SaveDLSFile(const std::wstring &filepath) {
  vector<uint8_t> dlsBuf;
  WriteDLSToBuffer(dlsBuf);
  return pRoot->UI_WriteBufferToFile(filepath, dlsBuf.data(), static_cast<uint32_t>(dlsBuf.size()));
}

//  *******
//  DLSInstr
//  ********

DLSInstr::DLSInstr(uint32_t bank, uint32_t instrument)
    : ulBank(bank), ulInstrument(instrument), m_name("Untitled instrument") {
  RiffFile::AlignName(m_name);
}

DLSInstr::DLSInstr(uint32_t bank, uint32_t instrument, string instrName)
    : ulBank(bank), ulInstrument(instrument), m_name(instrName) {
  RiffFile::AlignName(m_name);
}

uint32_t DLSInstr::GetSize() const {
  uint32_t dls_size = 0;
  dls_size += LIST_HDR_SIZE;  //"ins " list
  dls_size += INSH_SIZE;      // insh chunk
  dls_size += LIST_HDR_SIZE;  //"lrgn" list
  for (auto &rgn : m_regions) {
    dls_size += rgn->GetSize();
  }
  dls_size += LIST_HDR_SIZE;                       //"INFO" list
  dls_size += 8;                                   //"INAM" + size
  dls_size += static_cast<uint32_t>(m_name.size());  // size of name string

  return dls_size;
}

void DLSInstr::Write(vector<uint8_t> &buf) {
  uint32_t temp = GetSize() - 8;
  RiffFile::WriteLIST(buf, 0x696E7320, temp);                          // write "ins " list
  PushTypeOnVectBE<uint32_t>(buf, 0x696E7368);                         //"insh"
  PushTypeOnVect<uint32_t>(buf, INSH_SIZE - 8);                        // size
  PushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(m_regions.size()));  // cRegions
  PushTypeOnVect<uint32_t>(buf, ulBank);                               // ulBank
  PushTypeOnVect<uint32_t>(buf, ulInstrument);                         // ulInstrument

  temp = 4;
  for (auto &rgn : m_regions) {
    temp += rgn->GetSize();
  }
  RiffFile::WriteLIST(buf, 0x6C72676E, temp);  // write the "lrgn" list
  for (auto &reg : m_regions) {
    reg->Write(buf);
  }

  temp = 12 + static_cast<uint32_t>(m_name.size());  //"INFO" + "INAM" + size + the string size
  RiffFile::WriteLIST(buf, 0x494E464F, temp);      // write the "INFO" list
  PushTypeOnVectBE<uint32_t>(buf, 0x494E414D);     //"INAM"

  temp = static_cast<uint32_t>(m_name.size());
  PushTypeOnVect<uint32_t>(buf, temp);  // size
  PushBackStringOnVector(buf, m_name);    // The Instrument Name string
}

DLSRgn *DLSInstr::AddRgn() {
  auto reg = m_regions.emplace_back().get();
  return reg;
}

//  ******
//  DLSRgn
//  ******

uint32_t DLSRgn::GetSize() const {
  uint32_t size = 0;
  size += LIST_HDR_SIZE;  //"rgn2" list
  size += RGNH_SIZE;      // rgnh chunk
  if (m_wsmp) {
    size += m_wsmp->GetSize();
  }

  size += WLNK_SIZE;
  if (m_art) {
    size += m_art->GetSize();
  }

  return size;
}

void DLSRgn::Write(vector<uint8_t> &buf) {
  RiffFile::WriteLIST(buf, 0x72676E32, (GetSize() - 8));                // write "rgn2" list
  PushTypeOnVectBE<uint32_t>(buf, 0x72676E68);                          //"rgnh"
  PushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(RGNH_SIZE - 8));  // size
  PushTypeOnVect<uint16_t>(buf, usKeyLow);                              // usLow  (key)
  PushTypeOnVect<uint16_t>(buf, usKeyHigh);                             // usHigh (key)
  PushTypeOnVect<uint16_t>(buf, usVelLow);                              // usLow  (vel)
  PushTypeOnVect<uint16_t>(buf, usVelHigh);                             // usHigh (vel)
  PushTypeOnVect<uint16_t>(buf, 1);                                     // fusOptions
  PushTypeOnVect<uint16_t>(buf, 0);                                     // usKeyGroup

  // new for dls2
  PushTypeOnVect<uint16_t>(buf, 1);  // NO CLUE

  if (m_wsmp)
    m_wsmp->Write(buf);  // write the "wsmp" chunk

  PushTypeOnVectBE<uint32_t>(buf, 0x776C6E6B);   //"wlnk"
  PushTypeOnVect<uint32_t>(buf, WLNK_SIZE - 8);  // size
  PushTypeOnVect<uint16_t>(buf, fusOptions);     // fusOptions
  PushTypeOnVect<uint16_t>(buf, usPhaseGroup);   // usPhaseGroup
  PushTypeOnVect<uint32_t>(buf, channel);        // ulChannel
  PushTypeOnVect<uint32_t>(buf, tableIndex);     // ulTableIndex

  if (m_art)
    m_art->Write(buf);
}

DLSArt *DLSRgn::AddArt() {
  m_art = std::make_unique<DLSArt>();
  return m_art.get();
}

DLSWsmp *DLSRgn::AddWsmp() {
  m_wsmp = std::make_unique<DLSWsmp>();
  return m_wsmp.get();
}

void DLSRgn::SetRanges(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh) {
  usKeyLow = keyLow;
  usKeyHigh = keyHigh;
  usVelLow = velLow;
  usVelHigh = velHigh;
}

void DLSRgn::SetWaveLinkInfo(uint16_t options, uint16_t phaseGroup, uint32_t theChannel,
                             uint32_t theTableIndex) {
  fusOptions = options;
  usPhaseGroup = phaseGroup;
  channel = theChannel;
  tableIndex = theTableIndex;
}

//  ******
//  DLSArt
//  ******

uint32_t DLSArt::GetSize(void) {
  uint32_t size = 0;
  size += LIST_HDR_SIZE;  //"lar2" list
  size += 16;             //"art2" chunk + size + cbSize + cConnectionBlocks
  for (auto &block : m_blocks)
    size += block->GetSize();  // each connection block
  return size;
}

void DLSArt::Write(vector<uint8_t> &buf) {
  RiffFile::WriteLIST(buf, 0x6C617232, GetSize() - 8);                       // write "lar2" list
  PushTypeOnVectBE<uint32_t>(buf, 0x61727432);                               //"art2"
  PushTypeOnVect<uint32_t>(buf, GetSize() - LIST_HDR_SIZE - 8);              // size
  PushTypeOnVect<uint32_t>(buf, 8);                                          // cbSize
  PushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(m_blocks.size()));  // cConnectionBlocks
  for (auto &block : m_blocks) {
    block->Write(buf);  // each connection block
  }
}

void DLSArt::AddADSR(long attack_time, uint16_t atk_transform, long decay_time, long sustain_lev,
                     long release_time, uint16_t rls_transform) {
  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_ATTACKTIME, atk_transform, attack_time));

  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_DECAYTIME, CONN_TRN_NONE, decay_time));

  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_SUSTAINLEVEL, CONN_TRN_NONE, sustain_lev));

  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(
      CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_RELEASETIME, rls_transform, release_time));
}

void DLSArt::AddPan(long pan) {
  m_blocks.emplace_back(std::make_unique<ConnectionBlock>(CONN_SRC_NONE, CONN_SRC_NONE,
                                                             CONN_DST_PAN, CONN_TRN_NONE, pan));
}

//  ***************
//  ConnectionBlock
//  ***************

void ConnectionBlock::Write(vector<uint8_t> &buf) {
  PushTypeOnVect<uint16_t>(buf, usSource);       // usSource
  PushTypeOnVect<uint16_t>(buf, usControl);      // usControl
  PushTypeOnVect<uint16_t>(buf, usDestination);  // usDestination
  PushTypeOnVect<uint16_t>(buf, usTransform);    // usTransform
  PushTypeOnVect<int32_t>(buf, lScale);          // lScale
}

//  *******
//  DLSWsmp
//  *******

uint32_t DLSWsmp::GetSize() const {
  uint32_t size = 0;
  size += 28;  // all the variables minus the loop info
  if (cSampleLoops)
    size += 16;  // plus the loop info
  return size;
}

void DLSWsmp::Write(vector<uint8_t> &buf) {
  PushTypeOnVectBE<uint32_t>(buf, 0x77736D70);   //"wsmp"
  PushTypeOnVect<uint32_t>(buf, GetSize() - 8);  // size
  PushTypeOnVect<uint32_t>(buf, 20);             // cbSize (size of structure without loop record)
  PushTypeOnVect<uint16_t>(buf, usUnityNote);    // usUnityNote
  PushTypeOnVect<int16_t>(buf, sFineTune);       // sFineTune
  PushTypeOnVect<int32_t>(buf, lAttenuation);    // lAttenuation
  PushTypeOnVect<uint32_t>(buf, 1);              // fulOptions
  PushTypeOnVect<uint32_t>(buf, cSampleLoops);   // cSampleLoops
  if (cSampleLoops)                              // if it loops, write the loop structure
  {
    PushTypeOnVect<uint32_t>(buf, 16);
    PushTypeOnVect<uint32_t>(buf, ulLoopType);  // ulLoopType
    PushTypeOnVect<uint32_t>(buf, ulLoopStart);
    PushTypeOnVect<uint32_t>(buf, ulLoopLength);
  }
}

void DLSWsmp::SetLoopInfo(Loop &loop, VGMSamp *samp) {
  const int origFormatBytesPerSamp = samp->bps / 8;
  double compressionRatio = samp->GetCompressionRatio();

  // If the sample loops, but the loop length is 0, then assume the length should
  // extend to the end of the sample.
  if (loop.loopStatus && loop.loopLength == 0)
    loop.loopLength = samp->dataLength - loop.loopStart;

  cSampleLoops = loop.loopStatus;
  ulLoopType = loop.loopType;
  // In DLS, the value is in number of samples
  // if the val is a raw offset of the original format, multiply it by the compression ratio
  ulLoopStart = (loop.loopStartMeasure == LM_BYTES)
                    ? (uint32_t)((loop.loopStart * compressionRatio) / origFormatBytesPerSamp)
                    : loop.loopStart;

  ulLoopLength = (loop.loopLengthMeasure == LM_BYTES)
                     ? (uint32_t)((loop.loopLength * compressionRatio) / origFormatBytesPerSamp)
                     : loop.loopLength;
}

void DLSWsmp::SetPitchInfo(uint16_t unityNote, short fineTune, long attenuation) {
  usUnityNote = unityNote;
  sFineTune = fineTune;
  lAttenuation = attenuation;
}

//  *******
//  DLSWave
//  *******

uint32_t DLSWave::GetSize() const {
  uint32_t size = 0;
  size += LIST_HDR_SIZE;          //"wave" list
  size += 8;                      //"fmt " chunk + size
  size += 18;                     // fmt chunk data
  size += 8;                      //"data" chunk + size
  size += this->GetSampleSize();  // dataSize;			    //size of sample data

  size += LIST_HDR_SIZE;                       //"INFO" list
  size += 8;                                   //"INAM" + size
  size += static_cast<uint32_t>(m_name.size());  // size of name string
  return size;
}

void DLSWave::Write(vector<uint8_t> &buf) {
  RiffFile::WriteLIST(buf, 0x77617665, GetSize() - 8);  // write "wave" list
  PushTypeOnVectBE<uint32_t>(buf, 0x666D7420);          //"fmt "
  PushTypeOnVect<uint32_t>(buf, 18);                    // size
  PushTypeOnVect<uint16_t>(buf, wFormatTag);            // wFormatTag
  PushTypeOnVect<uint16_t>(buf, wChannels);             // wChannels
  PushTypeOnVect<uint32_t>(buf, dwSamplesPerSec);       // dwSamplesPerSec
  PushTypeOnVect<uint32_t>(buf, dwAveBytesPerSec);      // dwAveBytesPerSec
  PushTypeOnVect<uint16_t>(buf, wBlockAlign);           // wBlockAlign
  PushTypeOnVect<uint16_t>(buf, wBitsPerSample);        // wBitsPerSample

  PushTypeOnVect<uint16_t>(buf, 0);  // cbSize DLS2 specific.  I don't know anything else

  PushTypeOnVectBE<uint32_t>(buf, 0x64617461);  // "data"
  /* size: this is the ACTUAL size, not the even-aligned size */
  PushTypeOnVect<uint32_t>(buf, m_wave_data.size());
  buf.insert(buf.end(), std::begin(m_wave_data), std::end(m_wave_data));  // Write the sample
  if (m_wave_data.size() % 2) {
    buf.push_back(0);
  }

  uint32_t info_sig =
      12 + static_cast<uint32_t>(m_name.size());     //"INFO" + "INAM" + size + the string size
  RiffFile::WriteLIST(buf, 0x494E464F, info_sig);  // write the "INFO" list
  PushTypeOnVectBE<uint32_t>(buf, 0x494E414D);     //"INAM"
  PushTypeOnVect<uint32_t>(buf, static_cast<uint32_t>(m_name.size()));  // size
  PushBackStringOnVector(buf, m_name);
}
