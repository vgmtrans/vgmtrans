#include "PSDSEPS2InstrSet.h"

#include "LogManager.h"
#include "PSDSEFormat.h"
#include "RawFile.h"
#include "ScaleConversion.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include <fmt/format.h>

namespace {

constexpr uint32_t kSampleRate = 48000;

struct WaveDescriptorLayout {
  uint32_t volume;
  uint32_t tuning;
  uint32_t sampleStart;
  uint32_t loopStart;
};

WaveDescriptorLayout waveDescriptorLayout(uint16_t version) {
  // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named v0301
  // SsdSetWaveToVoiceControl routine reads start, loop, gain, and tuning at +0, +4, +8, and +0xa.
  // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The structurally matching v0320 normal key-on routines read those
  // fields at +4, +8, +0, and +2.
  if (version == 0x0320) {
    return {0x00, 0x02, 0x04, 0x08};
  }
  return {0x08, 0x0a, 0x00, 0x04};
}

std::string descriptorName(const RawFile* file, uint32_t offset, uint32_t index) {
  std::string name = file->readNullTerminatedString(offset + 0x10, 16);
  if (name.empty()) {
    name = fmt::format("Wave {}", index);
  }
  return name;
}

std::string programName(const RawFile* file, uint32_t offset, uint32_t program, uint16_t version) {
  const uint32_t nameOffset = PSDSEPS2::isV2(version) ? 0x08 : 0x50;
  std::string name = file->readNullTerminatedString(offset + nameOffset, 16);
  if (name.empty()) {
    name = fmt::format("Program {}", program);
  }
  return name;
}

}  // namespace

PSDSEPS2SampColl::PSDSEPS2SampColl(RawFile* file, const PSDSEPS2::BankHeader& header)
    : VGMSampColl(PSDSEFormat::name, file, header.offset, header.fileLength, header.internalName + " Samples"),
      m_header(header) {
}

bool PSDSEPS2SampColl::parseHeader() {
  if (PSDSEPS2::isV2(m_header.version)) {
    auto* header = addHeader(offset(), 0x34, "PS2 SWDM v2 Header");
    header->addChild(offset(), 4, "Magic");
    header->addChild(offset() + 0x04, 4, "Checksum Key");
    header->addChild(offset() + 0x08, 4, "File Length");
    header->addChild(offset() + 0x0c, 2, "Version");
    // [Shadow Hearts]: SsdAddWaveData starts its record walk at +0x34 and does not read +0x0e through +0x11 or
    // +0x24 through +0x33. All 1,076 audited v2 banks store zeroes in both ranges.
    header->addChild(offset() + 0x0e, 4, "Zero Padding");
    header->addChild(offset() + 0x12, 2, "File ID");
    header->addChild(offset() + 0x14, 1, "Wave Count");
    header->addChild(offset() + 0x15, 1, "Program Count");
    header->addChild(offset() + 0x16, 2, "Wave Transfer Parameters");
    header->addChild(offset() + 0x18, 4, "Requested SPU2 Address");
    header->addChild(offset() + 0x1c, 4, "ADPCM Data Size");
    header->addChild(offset() + 0x20, 4, "ADPCM Data Offset");
    header->addChild(offset() + 0x24, 0x10, "Zero Padding");
    sampDataOffset = offset() + m_header.sampleDataOffset;
    return true;
  }

  if (PSDSEPS2::isV25(m_header.version)) {
    auto* header = addHeader(offset(), 0x40, "PS2 SWDM v2.5 Header");
    header->addChild(offset(), 4, "Magic");
    header->addChild(offset() + 0x04, 4, "Checksum Key");
    header->addChild(offset() + 0x08, 4, "File Length");
    header->addChild(offset() + 0x0c, 2, "Version");
    header->addChild(offset() + 0x0e, 2, "File ID");
    // [Xenosaga Episode I: Der Wille zur Macht]: SsdSpuWaveMalloc tests bit 0 to choose dynamic-address SPU2
    // allocation when the caller requests a specific address. Other bits are not read by the shipped driver.
    header->addChild(offset() + 0x10, 2, "Wave Allocation Flags");
    header->addChild(offset() + 0x12, 2, "Wave Bank ID");
    header->addChild(offset() + 0x14, 1, "Wave Count");
    header->addChild(offset() + 0x15, 1, "Program Count");
    // [Tokimeki Memorial: Girl's Side]: SsdAddWaveData and SsdSpuWaveMalloc do not read +0x16 through +0x1f or
    // +0x2c through +0x3f. All 645 audited SWDM objects store zeroes in both ranges.
    // [Xenosaga Episode I: Der Wille zur Macht]: All 1,497 audited version 2.5 SWDM objects also store zeroes in
    // both ranges, confirming that these bytes are padding rather than title-specific parameters.
    header->addChild(offset() + 0x16, 0x0a, "Zero Padding");
    header->addChild(offset() + 0x20, 4, "Requested SPU2 Address");
    header->addChild(offset() + 0x24, 4, "ADPCM Data Size");
    header->addChild(offset() + 0x28, 4, "ADPCM Data Offset");
    header->addChild(offset() + 0x2c, 0x14, "Zero Padding");
    sampDataOffset = offset() + m_header.sampleDataOffset;
    return true;
  }

  auto* header = addHeader(offset(), 0x50, "PS2 SWDM Header");
  header->addChild(offset(), 4, "Magic");
  header->addChild(offset() + 0x04, 4, "Checksum Key");
  header->addChild(offset() + 0x08, 4, "File Length");
  if (m_header.version == 0x0300) {
    header->addChild(offset() + 0x0c, 4, "Checksum");
    header->addChild(offset() + 0x10, 2, "Version");
    header->addChild(offset() + 0x12, 2, "File ID");
    // [Bakusou Dekotora Densetsu: Otoko Hanamichi Yume Roman]: The v0300 SsdAddWaveData routine does not read this
    // range, and every audited bank stores zeroes here.
    header->addChild(offset() + 0x14, 0x0c, "Zero Padding");
  } else {
    header->addChild(offset() + 0x0c, 2, "Version");
    header->addChild(offset() + 0x0e, 2, "File ID");
    header->addChild(offset() + 0x10, 4, "Checksum");
    header->addChild(offset() + 0x14, 4, "Checksum Coverage");
    header->addChild(offset() + 0x18, 8, "Creation Timestamp");
  }
  header->addChild(offset() + 0x20, 16, "Internal Name");
  header->addChild(offset() + 0x30, 2, "Data Flags");
  if (m_header.version == 0x0320) {
    header->addChild(offset() + 0x32, 1, "Wave Count");
    header->addChild(offset() + 0x33, 1, "Program Count");
    header->addChild(offset() + 0x34, 1, "Wave Data Loader Type");
    header->addChild(offset() + 0x35, 4, "Zero Padding");
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The reconstructed version 0x0320 allocator does not read +0x39.
    // Its observed values of 0, 23, and 47 match inclusive highest voice numbers for one or both SPU2 cores.
    header->addChild(offset() + 0x39, 1, "Authoring Highest Voice Number (Driver Unused)");
    header->addChild(offset() + 0x3a, 2, "Zero Padding");
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The v0320 SsdSpuWaveMalloc routine reads the requested address at
    // +0x3c before dispatching on +0x34.
    header->addChild(offset() + 0x3c, 4, "Requested SPU2 Address");
  } else {
    header->addChild(offset() + 0x32, 2, "Bank ID Mirror");
    header->addChild(offset() + 0x34, 2, "Wave Group");
    // [Bakusou Dekotora Densetsu: Otoko Hanamichi Yume Roman]: The named version 0x0300 SsdSpuWaveMalloc routine
    // maps loader types 0, 2, 3, and 4 to standard, fixed-address, dynamic-address, and segmented allocation.
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named version 0x0301 routine changes the
    // dispatch: types 0 and 3 are fixed-address, types 1 and 2 are the two standard allocators, type 4 is dynamic,
    // and type 5 is segmented. The following byte and +0x3a through +0x3c are zero in all 851 audited v0x0301 banks.
    header->addChild(offset() + 0x36, 1, "Wave Data Loader Type");
    header->addChild(offset() + 0x37, 1, "Zero Padding");
    header->addChild(offset() + 0x38, 1, "Wave Count");
    header->addChild(offset() + 0x39, 1, "Program Count");
    header->addChild(offset() + 0x3a, 3, "Zero Padding");
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named allocator does not read +0x3d, and
    // every audited version 0x0301 bank stores 47, the inclusive highest voice number across both SPU2 cores.
    header->addChild(offset() + 0x3d, 1, "Authoring Highest Voice Number (Driver Unused)");
    header->addChild(offset() + 0x3e, 2, "Zero Padding");
  }
  if (m_header.version == 0x0320) {
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: Loader type 4 passes +0x44 to its allocation routine. Other loader
    // types ignore it, and +0x40 is loader-specific rather than the requested address used by the common path.
    header->addChild(offset() + 0x40, 4, "Wave Loader Parameter 1");
    header->addChild(offset() + 0x44, 4, "Wave Loader Parameter 2");
  } else {
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named SsdSpuWaveMalloc routine reads the
    // requested address from +0x40. Loader type 5 passes +0x44 to SsdSpuMallocAdrsSegment, which rejects values below
    // 0x40 and advances candidate addresses by this amount.
    header->addChild(offset() + 0x40, 4, "Requested SPU2 Address");
    header->addChild(offset() + 0x44, 4, "SPU2 Address Segment Size");
  }
  header->addChild(offset() + 0x48, 4, "ADPCM Data Size");
  header->addChild(offset() + 0x4c, 4, "ADPCM Data Offset");
  sampDataOffset = offset() + m_header.sampleDataOffset;
  return true;
}

bool PSDSEPS2SampColl::parseSampleInfo() {
  initializeSampleSlots(m_header.waveCount);
  reserveSamples(m_header.waveCount);
  if (m_header.waveCount == 0 || m_header.sampleDataSize == 0) {
    return false;
  }

  const uint32_t dataBegin = offset() + m_header.sampleDataOffset;
  const uint32_t dataEnd = dataBegin + m_header.sampleDataSize;
  const WaveDescriptorLayout layout = waveDescriptorLayout(m_header.version);
  std::map<std::pair<uint32_t, uint32_t>, size_t> sampleByLocation;

  for (size_t wave = 0; wave < m_header.waveDescriptorOffsets.size(); ++wave) {
    const uint32_t descriptor = m_header.waveDescriptorOffsets[wave];
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The v0301 driver treats zero descriptor gain
    // as an unused wave slot.
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The v0320 driver uses the same sentinel. A zero sample address is
    // valid, while zero gain identifies thousands of empty table slots.
    if (readByte(descriptor + layout.volume) == 0) {
      continue;
    }

    const uint32_t relativeStart = readWord(descriptor + layout.sampleStart);
    if (relativeStart >= m_header.sampleDataSize) {
      L_WARN("PSDSE PS2: wave {} in '{}' starts outside its ADPCM payload", wave, name());
      continue;
    }

    const uint32_t sampleStart = dataBegin + relativeStart;
    uint32_t descriptorEnd = dataEnd;
    for (size_t next = 0; next < m_header.waveDescriptorOffsets.size(); ++next) {
      const uint32_t nextDescriptor = m_header.waveDescriptorOffsets[next];
      if (readByte(nextDescriptor + layout.volume) == 0) {
        continue;
      }
      const uint32_t nextStart = readWord(nextDescriptor + layout.sampleStart);
      if (nextStart > relativeStart && nextStart < m_header.sampleDataSize && dataBegin + nextStart < descriptorEnd) {
        descriptorEnd = dataBegin + nextStart;
      }
    }

    bool loopsFromFlags = false;
    uint32_t sampleLength = PSXSamp::getSampleLength(rawFile(), sampleStart, descriptorEnd, loopsFromFlags);
    if (sampleLength == 0) {
      sampleLength = descriptorEnd - sampleStart;
    }
    if (sampleLength == 0) {
      continue;
    }

    const auto key = std::pair{sampleStart, sampleLength};
    if (const auto existing = sampleByLocation.find(key); existing != sampleByLocation.end()) {
      mapSampleSlot(wave, existing->second);
      continue;
    }

    const size_t sampleIndex = sampleCount();
    auto* sample = addSamp<PSXSamp>(this, sampleStart, sampleLength, sampleStart, sampleLength, 1, BPS::PCM16,
                                    kSampleRate, descriptorName(rawFile(), descriptor, wave), false);
    sampleByLocation.emplace(key, sampleIndex);
    mapSampleSlot(wave, sampleIndex);

    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: Descriptor gain belongs to the direct-wave
    // channel path; the program path multiplies program and region gain without reading descriptor gain.
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The v0320 program path has the same gain stages.

    uint32_t loopStart = readWord(descriptor + layout.loopStart);
    if (PSDSEPS2::isV2(m_header.version) || PSDSEPS2::isV25(m_header.version)) {
      // [Shadow Hearts]: Descriptor +6 stores a little-endian count of eight-byte units. For every audited looped
      // wave it equals the embedded PS-ADPCM loop-flag byte offset. One-shot waves also store a predictor start
      // value here, so the ADPCM looping flag determines whether the sample repeats.
      // [Tokimeki Memorial: Girl's Side]: All 2,763 looping version 2.5 waves use the same representation, while
      // all 1,182 one-shot waves retain a nonzero predictor position that must not enable looping.
      // [Xenosaga Episode I: Der Wille zur Macht]: All 2,955 looping waves across XENOSAGA.01 and XENOSAGA.02
      // match the embedded PS-ADPCM loop-start flag after converting descriptor +6 from eight-byte units.
      loopStart = static_cast<uint32_t>(readShort(descriptor + 0x06)) * 8;
    }
    const bool usesEmbeddedLoopFlag = PSDSEPS2::isV2(m_header.version) || PSDSEPS2::isV25(m_header.version);
    const bool loops = usesEmbeddedLoopFlag ? loopsFromFlags : (loopsFromFlags || loopStart != 0);
    const bool validLoop = loops && loopStart < sampleLength;
    sample->setLoopStatus(validLoop ? 1 : 0);
    if (validLoop) {
      sample->setLoopStartMeasure(LM_BYTES);
      sample->setLoopLengthMeasure(LM_BYTES);
      sample->setLoopOffset(loopStart);
      sample->setLoopLength(sampleLength - loopStart);
    }
  }

  return hasSamples();
}

PSDSEPS2InstrSet::PSDSEPS2InstrSet(RawFile* file, const PSDSEPS2::BankHeader& header, PSDSEPS2SampColl* sampColl)
    : VGMInstrSet(PSDSEFormat::name, file, header.offset, header.fileLength, header.internalName), m_header(header) {
  if (sampColl) {
    attachSampColl(sampColl);
  }
}

bool PSDSEPS2InstrSet::parseHeader() {
  if (PSDSEPS2::isV2(m_header.version)) {
    auto* header = addHeader(offset(), 0x34, "PS2 SWDM v2 Header");
    header->addChild(offset(), 4, "Magic");
    header->addChild(offset() + 0x08, 4, "File Length");
    header->addChild(offset() + 0x0c, 2, "Version");
    header->addChild(offset() + 0x12, 2, "File ID");
    header->addChild(offset() + 0x14, 1, "Wave Count");
    header->addChild(offset() + 0x15, 1, "Program Count");
    header->addChild(offset() + 0x18, 4, "Requested SPU2 Address");
    header->addChild(offset() + 0x1c, 4, "ADPCM Data Size");
    header->addChild(offset() + 0x20, 4, "ADPCM Data Offset");
    return (m_header.programDataOffset != 0 && m_header.programCount != 0) ||
           (m_header.programCount == 0 && m_header.waveCount != 0 && sampColl() != nullptr);
  }

  if (PSDSEPS2::isV25(m_header.version)) {
    auto* header = addHeader(offset(), 0x40, "PS2 SWDM v2.5 Header");
    header->addChild(offset(), 4, "Magic");
    header->addChild(offset() + 0x08, 4, "File Length");
    header->addChild(offset() + 0x0c, 2, "Version");
    header->addChild(offset() + 0x0e, 2, "File ID");
    header->addChild(offset() + 0x10, 2, "Wave Allocation Flags");
    header->addChild(offset() + 0x12, 2, "Wave Bank ID");
    header->addChild(offset() + 0x14, 1, "Wave Count");
    header->addChild(offset() + 0x15, 1, "Program Count");
    header->addChild(offset() + 0x20, 4, "Requested SPU2 Address");
    header->addChild(offset() + 0x24, 4, "ADPCM Data Size");
    header->addChild(offset() + 0x28, 4, "ADPCM Data Offset");
    return (m_header.programDataOffset != 0 && m_header.programCount != 0) ||
           (m_header.programCount == 0 && m_header.waveCount != 0 && sampColl() != nullptr);
  }

  auto* header = addHeader(offset(), 0x50, "PS2 SWDM Header");
  header->addChild(offset(), 4, "Magic");
  header->addChild(offset() + 0x08, 4, "File Length");
  header->addChild(offset() + (m_header.version == 0x0300 ? 0x10 : 0x0c), 2, "Version");
  header->addChild(offset() + (m_header.version == 0x0300 ? 0x12 : 0x0e), 2, "File ID");
  header->addChild(offset() + 0x20, 16, "Internal Name");
  if (m_header.version == 0x0320) {
    header->addChild(offset() + 0x32, 1, "Wave Count");
    header->addChild(offset() + 0x33, 1, "Program Count");
    header->addChild(offset() + 0x34, 1, "Wave Data Loader Type");
  } else {
    header->addChild(offset() + 0x32, 2, "Bank ID Mirror");
    header->addChild(offset() + 0x36, 1, "Wave Data Loader Type");
    header->addChild(offset() + 0x38, 1, "Wave Count");
    header->addChild(offset() + 0x39, 1, "Program Count");
  }
  return (m_header.programDataOffset != 0 && m_header.programCount != 0) ||
         (m_header.programCount == 0 && m_header.waveCount != 0 && sampColl() != nullptr);
}

bool PSDSEPS2InstrSet::parseInstrPointers() {
  if (m_header.programCount == 0) {
    auto* samples = dynamic_cast<PSDSEPS2SampColl*>(sampColl());
    if (!samples) {
      return false;
    }

    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: SsdSeqWaveChange selects a wave directly, and
    // effect banks deliberately contain no program records. Each occupied wave slot therefore needs an exported
    // instrument for the MIDI and SoundFont representation.
    const WaveDescriptorLayout layout = waveDescriptorLayout(m_header.version);
    for (uint32_t wave = 0; wave < m_header.waveDescriptorOffsets.size() && wave < 128; ++wave) {
      const auto sampleIndex = samples->sampleIndexForSlot(wave);
      if (!sampleIndex) {
        continue;
      }
      const uint32_t descriptor = m_header.waveDescriptorOffsets[wave];
      auto* instr = addInstr<VGMInstr>(this, descriptor, 0x20, PSDSEPS2::midiBankNumber(m_header.bankId), wave,
                                      fmt::format("Wave {}", wave), 0);
      auto* rgn = instr->addRgn<VGMRgn>(instr, descriptor, 0x20, "Direct Wave Region");
      rgn->sampNum = static_cast<uint32_t>(*sampleIndex);
      rgn->sampCollPtr = samples;
      rgn->setUnityKey(60);
      rgn->setVolume(readByte(descriptor + layout.volume) / 127.0);

      const uint32_t tuningOffset = descriptor + layout.tuning;
      const int16_t rawTune = static_cast<int16_t>(readShort(tuningOffset));
      const int tuneCents = static_cast<int>(std::lround(rawTune * 100.0 / 256.0));
      const int coarse = tuneCents / 100;
      const int fine = tuneCents - coarse * 100;
      if (coarse != 0) {
        rgn->addCoarseTune(coarse, tuningOffset, 2);
      }
      if (fine != 0) {
        rgn->addFineTune(fine, tuningOffset, 2);
      }
    }
    return instrCount() != 0;
  }

  const uint32_t payload = m_header.programDataOffset;
  const uint32_t payloadEnd = payload + m_header.programDataSize;
  if (PSDSEPS2::isV2(m_header.version)) {
    // [Shadow Hearts]: SsdSeqProgramChange advances through this array by 0xd8 and compares program +2 with the
    // requested ID. SsdGetProgramSplitPtr begins at +0x18 and advances through fixed 0x0c-byte regions.
    for (uint32_t slot = 0; slot < m_header.programCount; ++slot) {
      const uint32_t programOffset = payload + slot * 0xd8;
      if (programOffset > payloadEnd || payloadEnd - programOffset < 0xd8) {
        return false;
      }
      const uint8_t regionCount = readByte(programOffset + 0x03);
      if (regionCount == 0 || regionCount > 16) {
        L_WARN("PSDSE PS2: program {} in '{}' has invalid v2 region count {}", slot, name(), regionCount);
        continue;
      }
      const uint8_t programId = readByte(programOffset + 0x02);
      addInstr<PSDSEPS2Instr>(this, programOffset, 0xd8, PSDSEPS2::midiBankNumber(m_header.bankId), programId,
                              programName(rawFile(), programOffset, programId, m_header.version));
    }
    return instrCount() != 0;
  }

  const uint32_t regionSize = PSDSEPS2::isV25(m_header.version) ? 0x10 : 0x20;
  for (uint32_t slot = 0; slot < m_header.programCount; ++slot) {
    const uint16_t relativeOffset = readShort(payload + slot * 2);
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: Zero is the empty v0301 program-slot sentinel.
    // [Tokimeki Memorial: Girl's Side]: Zero is the empty v0250 program-slot sentinel.
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: Zero is also the v0320 sentinel; tables retain program numbers even
    // when most slots are unused.
    if (relativeOffset == 0) {
      continue;
    }
    const uint32_t programOffset = payload + relativeOffset;
    if (relativeOffset < static_cast<uint32_t>(m_header.programCount) * 2 || programOffset > payloadEnd ||
        payloadEnd - programOffset < 0x60) {
      L_WARN("PSDSE PS2: invalid program {} pointer in '{}'", slot, name());
      continue;
    }

    uint32_t programEnd = payloadEnd;
    for (uint32_t next = 0; next < m_header.programCount; ++next) {
      const uint16_t nextRelative = readShort(payload + next * 2);
      if (nextRelative > relativeOffset) {
        programEnd = std::min(programEnd, payload + nextRelative);
      }
    }
    const uint8_t regionCount = readByte(programOffset + 0x03);
    const uint32_t requiredLength = 0x60 + static_cast<uint32_t>(regionCount) * regionSize;
    if (programEnd - programOffset < requiredLength) {
      L_WARN("PSDSE PS2: program {} in '{}' has {} truncated regions", slot, name(), regionCount);
      continue;
    }

    const uint8_t programId = readByte(programOffset + 0x02);
    addInstr<PSDSEPS2Instr>(this, programOffset, requiredLength, PSDSEPS2::midiBankNumber(m_header.bankId), programId,
                            programName(rawFile(), programOffset, programId, m_header.version));
  }
  return instrCount() != 0;
}

PSDSEPS2Instr::PSDSEPS2Instr(VGMInstrSet* instrSet, uint32_t offset, uint32_t length, uint32_t bank, uint32_t instrNum,
                             std::string name)
    : VGMInstr(instrSet, offset, length, bank, instrNum, std::move(name)) {
}

bool PSDSEPS2Instr::loadInstr() {
  auto* instrSet = static_cast<PSDSEPS2InstrSet*>(parInstrSet);
  if (PSDSEPS2::isV2(instrSet->m_header.version)) {
    addChild(offset(), 1, "Program Flags");
    addChild(offset() + 0x01, 1, "Program Type");
    addChild(offset() + 0x02, 1, "Program ID");
    const uint8_t regionCount = readByte(offset() + 0x03);
    addChild(offset() + 0x03, 1, "Region Count");
    addChild(offset() + 0x04, 2, "Default SPU2 ADSR1");
    addChild(offset() + 0x06, 2, "Default SPU2 ADSR2");
    addChild(offset() + 0x08, 16, "Program Name");
    for (uint32_t region = 0; region < regionCount; ++region) {
      auto* rgn = addRgn<PSDSEPS2Rgn>(this, offset() + 0x18 + region * 0x0c, 0x0c);
      if (!rgn->loadRgn()) {
        return false;
      }
    }
    return regionCount != 0;
  }

  addChild(offset(), 1, "Program Flags");
  addChild(offset() + 0x01, 1, "Program Type");
  addChild(offset() + 0x02, 1, "Program ID");
  const uint8_t regionCount = readByte(offset() + 0x03);
  addChild(offset() + 0x03, 1, "Region Count");
  addChild(offset() + 0x04, 2, "Default ADSR1");
  addChild(offset() + 0x06, 2, "Default ADSR2");
  addChild(offset() + 0x08, 1, "Voice Priority");
  addChild(offset() + 0x09, 1, "Voice Limit");
  if (PSDSEPS2::isV25(instrSet->m_header.version)) {
    // [Tokimeki Memorial: Girl's Side]: The version 2.5 program key-on routine does not read +0x0a or +0x0b, and
    // all 1,919 audited programs store zeroes there. Gain and pan come from the wave and region records instead.
    addChild(offset() + 0x0a, 2, "Zero Padding");
  } else {
    addChild(offset() + 0x0a, 1, "Program Volume");
    addChild(offset() + 0x0b, 1, "Program Pan");
  }
  // [Graffiti Kingdom]: SsdInitLfoParam receives the four blocks below, and the named driver routines define their
  // runtime waveform and routing behavior. SF2 and DLS cannot encode that complete state.
  addChild(offset() + 0x10, 0x40, "LFO Parameters (Runtime Synthesis Stub)");
  addChild(offset() + 0x50, 0x10, "Program Name");

  const uint32_t regionSize = PSDSEPS2::isV25(instrSet->m_header.version) ? 0x10 : 0x20;
  for (uint32_t region = 0; region < regionCount; ++region) {
    auto* rgn = addRgn<PSDSEPS2Rgn>(this, offset() + 0x60 + region * regionSize, regionSize);
    if (!rgn->loadRgn()) {
      return false;
    }
  }
  return regionCount != 0;
}

PSDSEPS2Rgn::PSDSEPS2Rgn(VGMInstr* instr, uint32_t offset, uint32_t length) : VGMRgn(instr, offset, length, "Region") {
}

bool PSDSEPS2Rgn::loadRgn() {
  auto* instr = static_cast<PSDSEPS2Instr*>(parInstr);
  auto* instrSet = static_cast<PSDSEPS2InstrSet*>(instr->parInstrSet);
  const bool isV2 = PSDSEPS2::isV2(instrSet->m_header.version);
  const bool isV25 = PSDSEPS2::isV25(instrSet->m_header.version);
  const bool isV320 = instrSet->m_header.version == 0x0320;

  if (!isV2 && !isV25) {
    addGeneralItem(offset(), 2, "Region Flags");
  }
  const uint32_t waveOffset = (isV2 || isV25) ? 0x00 : 0x02;
  const uint8_t waveId = readByte(offset() + waveOffset);
  addSampNum(waveId, offset() + waveOffset);
  const uint32_t rootKeyOffset = (isV2 || isV25) ? 0x01 : (isV320 ? 0x04 : 0x03);
  const uint32_t volumeOffset = (isV2 || isV25) ? 0x04 : (isV320 ? 0x08 : 0x04);
  const uint32_t panOffset = (isV2 || isV25) ? 0x05 : (isV320 ? 0x09 : 0x05);
  const uint32_t voiceControlOffset = isV2 ? 0x0a : (isV25 ? 0x06 : (isV320 ? 0x0a : 0x06));
  const uint32_t adsrOffset = isV2 ? 0x06 : (isV25 ? 0x08 : (isV320 ? 0x0c : 0x08));
  addUnityKey(readByte(offset() + rootKeyOffset), offset() + rootKeyOffset);
  const uint8_t regionVolume = readByte(offset() + volumeOffset);
  addPan(readByte(offset() + panOffset), offset() + panOffset);
  addGeneralItem(offset() + voiceControlOffset, isV25 ? 1 : 2, isV2 ? "Region Control" : "Voice Control");
  const uint16_t adsr1 = readShort(offset() + adsrOffset);
  const uint16_t adsr2 = readShort(offset() + adsrOffset + 2);
  addADSRValue(offset() + adsrOffset, 2, "SPU2 ADSR1");
  addADSRValue(offset() + adsrOffset + 2, 2, "SPU2 ADSR2");
  if (isV25) {
    // [Tokimeki Memorial: Girl's Side]: The version 2.5 program key-on routine reads one control byte at +0x06,
    // skips the zero byte at +0x07, and advances over the constant four-byte authoring tail without reading it.
    addGeneralItem(offset() + 0x07, 1, "Zero Padding");
    addGeneralItem(offset() + 0x0c, 4, "Authoring Metadata (Driver Unused)");
  } else if (isV320) {
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: All 5,522 audited version 0x0320 regions store zero at +0x03. The
    // reconstructed program key-on path instead adds the signed root-key delta at +0x05 to the played note.
    addGeneralItem(offset() + 0x03, 1, "Zero Padding");
    addGeneralItem(offset() + 0x05, 1, "Root Key Delta");
  } else if (!isV2) {
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named version 0x0301 program key-on path
    // does not read +0x0c through +0x0f, and all 1,045 audited regions store zeroes in this range.
    addGeneralItem(offset() + 0x0c, 4, "Zero Padding");
  }
  if (isV2 || isV25) {
    addKeyLow(readByte(offset() + 0x02), offset() + 0x02);
    addKeyHigh(readByte(offset() + 0x03), offset() + 0x03);
  } else {
    addKeyLow(readByte(offset() + 0x10), offset() + 0x10);
    addKeyHigh(readByte(offset() + 0x11), offset() + 0x11);
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named version 0x0301 key-on path tests the
    // runtime key and velocity ranges at +0x10 and +0x14 but never reads the authoring copies at +0x12 and +0x16.
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: Version 0x0320 data can differ between the runtime and authoring key
    // ranges, so these fields are not invariants that the parser should require to match.
    addGeneralItem(offset() + 0x12, 2, "Authoring Key Range (Driver Unused)");
    addVelLow(readByte(offset() + 0x14), offset() + 0x14);
    addVelHigh(readByte(offset() + 0x15), offset() + 0x15);
    addGeneralItem(offset() + 0x16, 2, "Authoring Velocity Range (Driver Unused)");
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: All 1,045 audited version 0x0301 regions copy
    // pan into +0x18 and +0x19, store zeroes from +0x1a through +0x1e, and store 47 at +0x1f; the named key-on path
    // reads none of these bytes.
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: All 5,522 audited version 0x0320 regions retain the same field shape,
    // but +0x1f contains 23, 47, or 48 and therefore cannot be assumed to be an inclusive SPU2 voice index.
    addGeneralItem(offset() + 0x18, 2, "Authoring Pan Range (Driver Unused)");
    addGeneralItem(offset() + 0x1a, 5, "Zero Padding");
    addGeneralItem(offset() + 0x1f, 1, "Authoring Voice Metadata (Driver Unused)");
  }

  const uint8_t programVolume = (isV2 || isV25) ? 127 : readByte(instr->offset() + 0x0a);
  setVolume((programVolume / 127.0) * (regionVolume / 127.0));

  if (auto* samples = dynamic_cast<PSDSEPS2SampColl*>(instrSet->sampColl())) {
    if (const auto sampleIndex = samples->sampleIndexForSlot(waveId)) {
      setSampNum(static_cast<uint32_t>(*sampleIndex));
      sampCollPtr = samples;
    }

    if (waveId < samples->m_header.waveDescriptorOffsets.size()) {
      const uint32_t descriptor = samples->m_header.waveDescriptorOffsets[waveId];
      const WaveDescriptorLayout layout = waveDescriptorLayout(samples->m_header.version);
      // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The v0301 program key-on path reads tuning
      // from the wave descriptor as a signed 8.8-semitone value added to native SPU2 pitch.
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The v0320 path reads the same representation from region +6.
      const uint32_t tuningOffset = isV320 ? offset() + 0x06 : descriptor + layout.tuning;
      const int16_t rawTune = static_cast<int16_t>(readShort(tuningOffset));
      const int tuneCents = static_cast<int>(std::lround(rawTune * 100.0 / 256.0));
      const int coarse = tuneCents / 100;
      const int fine = tuneCents - coarse * 100;
      if (coarse != 0) {
        addCoarseTune(coarse, tuningOffset, 2);
      }
      if (fine != 0) {
        addFineTune(fine, tuningOffset, 2);
      }
    }
  }

  psxConvADSR(this, adsr1, adsr2, true);
  return true;
}
