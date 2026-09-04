#pragma once

#include "RawFile.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

namespace PSDSEPS2 {

inline constexpr uint32_t kSwdmMagic = 0x7377646d;
inline constexpr uint32_t kSmdmMagic = 0x736d646d;
inline constexpr uint32_t kSedsMagic = 0x73656473;

inline bool isV2(uint16_t version) {
  return version == 0x0200 || version == 0x0201;
}

inline std::string objectName(const char* magic, uint16_t id) {
  return fmt::format("{} {:04X}", magic, id);
}

std::string decodeShiftJis(std::string_view input);

struct SequenceTrackRecord {
  uint32_t recordOffset = 0;
  uint32_t recordLength = 0;
  uint32_t eventOffset = 0;
  uint32_t eventLength = 0;
  uint8_t flags = 0;
  uint8_t trackId = 0;
  uint8_t channel = 0;
  uint8_t outputGroup = 0;
};

struct SequenceHeader {
  uint32_t offset = 0;
  uint32_t fileLength = 0;
  uint16_t version = 0;
  uint16_t fileId = 0;
  uint16_t defaultBankId = 0;
  uint8_t trackCount = 0;
  uint8_t channelCount = 0;
  uint8_t initialVoiceVelocity = 127;
  bool isEffect = false;
  uint16_t effectNumber = 0;
  uint8_t effectHeaderSize = 0;
  std::string internalName;
  std::vector<SequenceTrackRecord> tracks;

  bool read(const RawFile* file, uint32_t readOffset) {
    offset = readOffset;
    if (readOffset > file->size() || file->size() - readOffset < 0x28 || file->readWordBE(readOffset) != kSmdmMagic) {
      return false;
    }

    fileLength = file->readWord(readOffset + 0x08);
    if (fileLength < 0x2c || fileLength > file->size() - readOffset || (fileLength & 3) != 0) {
      return false;
    }

    const uint16_t commonHeaderVersion = file->readShort(readOffset + 0x0c);
    if (isV2(commonHeaderVersion)) {
      // [Shadow Hearts]: The unstripped SSD Ver 2.0.010511 driver reads the sequence ID, default wave-bank ID, and
      // track and channel counts at these offsets. SsdSetDataAddress walks 16-bit type and length records from
      // +0x28; type 2 is descriptive text, and type 3 is an eight-byte track header followed by events.
      version = commonHeaderVersion;
      const uint16_t year = file->readShort(readOffset + 0x10);
      if (year < 1996 || year > 2099) {
        return false;
      }
      fileId = file->readShort(readOffset + 0x18);
      defaultBankId = file->readShort(readOffset + 0x1e);
      trackCount = file->readByte(readOffset + 0x20);
      channelCount = file->readByte(readOffset + 0x21);
      if (trackCount == 0) {
        return false;
      }

      bool foundTerminator = false;
      uint32_t cursor = readOffset + 0x28;
      const uint32_t fileEnd = readOffset + fileLength;
      while (cursor <= fileEnd && fileEnd - cursor >= 4) {
        const uint16_t type = file->readShort(cursor);
        const uint16_t recordSize = file->readShort(cursor + 2);
        if (recordSize < 4 || recordSize > fileEnd - cursor) {
          return false;
        }
        if (type == 0) {
          foundTerminator = true;
          break;
        }
        if (type == 2 && recordSize > 4 && internalName.empty()) {
          // [Shadow Hearts]: Type 2 records store descriptive strings as Shift-JIS, including Japanese-only
          // sequence titles.
          internalName = decodeShiftJis(file->readNullTerminatedString(cursor + 4, recordSize - 4));
        } else if (type == 3) {
          if (recordSize < 9) {
            return false;
          }
          tracks.push_back({cursor, recordSize, cursor + 8, static_cast<uint32_t>(recordSize - 8),
                            file->readByte(cursor + 4), file->readByte(cursor + 5), file->readByte(cursor + 6),
                            file->readByte(cursor + 7)});
        }
        cursor += recordSize;
      }

      if (internalName.empty()) {
        internalName = objectName("SMDM", fileId);
      }
      return foundTerminator && tracks.size() == trackCount;
    }

    if (fileLength < 0x60 || file->size() - readOffset < 0x50) {
      return false;
    }
    if (commonHeaderVersion == 0x0301 || commonHeaderVersion == 0x0320) {
      version = commonHeaderVersion;
      fileId = file->readShort(readOffset + 0x0e);
      const uint32_t checksumCoverage = file->readWord(readOffset + 0x14);
      const uint16_t year = file->readShort(readOffset + 0x18);
      if (checksumCoverage < 0x10 || checksumCoverage > fileLength || year < 1996 || year > 2099) {
        return false;
      }
    } else if (file->readShort(readOffset + 0x10) == 0x0300) {
      version = 0x0300;
      fileId = file->readShort(readOffset + 0x12);
    } else {
      return false;
    }

    internalName = file->readNullTerminatedString(readOffset + 0x20, 16);
    if (internalName.empty()) {
      return false;
    }
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The v0320 channel setup resolves this ID through the loaded SWDM
    // list. All 696 same-name sequence and bank pairs match SMDM +0x32 to the SWDM common-header file ID.
    // [Bakusou Dekotora Densetsu: Otoko Hanamichi Yume Roman]: The named v0300 driver uses the same SMDM field.
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named v0301 driver uses the same field.
    defaultBankId = file->readShort(readOffset + 0x32);
    trackCount = file->readByte(readOffset + 0x40);
    channelCount = file->readByte(readOffset + 0x41);
    if (trackCount == 0) {
      return false;
    }

    bool foundTerminator = false;
    uint32_t cursor = readOffset + 0x50;
    const uint32_t fileEnd = readOffset + fileLength;
    while (cursor <= fileEnd && fileEnd - cursor >= 0x10) {
      const uint32_t type = file->readWord(cursor);
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The v0320 driver reads aligned and logical record sizes at +4
      // and +8; the track preamble remains at +0x10.
      // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The v0301 driver reads those sizes at +8 and
      // +0xc.
      const uint32_t sizeFieldOffset = version == 0x0320 ? 0x04 : 0x08;
      const uint32_t alignedSize = file->readWord(cursor + sizeFieldOffset);
      const uint32_t logicalSize = file->readWord(cursor + sizeFieldOffset + 4);
      if (alignedSize < 0x10 || alignedSize > fileEnd - cursor || (alignedSize & 0x0f) != 0) {
        return false;
      }
      if (type == 0) {
        foundTerminator = true;
        break;
      }
      if (type == 3) {
        // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The SSD driver receives the four-byte track
        // preamble separately from the event pointer. logicalSize includes the record header, while alignedSize
        // advances to the next 16-byte-aligned record.
        if (logicalSize < 0x14 || logicalSize > alignedSize) {
          return false;
        }
        tracks.push_back({cursor, alignedSize, cursor + 0x14, logicalSize - 0x14, file->readByte(cursor + 0x10),
                          file->readByte(cursor + 0x11), file->readByte(cursor + 0x12), file->readByte(cursor + 0x13)});
      }
      cursor += alignedSize;
    }

    return foundTerminator && tracks.size() == trackCount;
  }
};

struct EffectSetHeader {
  uint32_t offset = 0;
  uint32_t fileLength = 0;
  uint16_t version = 0;
  uint16_t fileId = 0;
  uint16_t defaultBankId = 0;
  uint16_t effectSlotCount = 0;
  uint32_t effectTableOffset = 0;
  std::string internalName;
  std::vector<SequenceHeader> effects;

  bool read(const RawFile* file, uint32_t readOffset) {
    offset = readOffset;
    if (readOffset > file->size() || file->size() - readOffset < 0x38 || file->readWordBE(readOffset) != kSedsMagic) {
      return false;
    }

    fileLength = file->readWord(readOffset + 0x08);
    if (fileLength < 0x38 || fileLength > file->size() - readOffset || (fileLength & 3) != 0) {
      return false;
    }

    const uint16_t commonHeaderVersion = file->readShort(readOffset + 0x0c);
    if (isV2(commonHeaderVersion)) {
      // [Shadow Hearts]: The unstripped SSD Ver 2.0.010511 SsdAddEffectData and SsdCheckEffectData routines identify
      // a loaded set by +0x1c. SsdPlaySeqEffectNormal reads the bank at +0x1e, the effect track count at record +3,
      // and 16-bit track offsets from record +8.
      version = commonHeaderVersion;
      effectSlotCount = file->readShort(readOffset + 0x1a);
      fileId = file->readShort(readOffset + 0x1c);
      defaultBankId = file->readShort(readOffset + 0x1e);
      effectTableOffset = 0x34;
    } else {
      if (fileLength < 0x40) {
        return false;
      }
      if (commonHeaderVersion == 0x0301 || commonHeaderVersion == 0x0320) {
        version = commonHeaderVersion;
        fileId = file->readShort(readOffset + 0x0e);
        const uint32_t checksumCoverage = file->readWord(readOffset + 0x14);
        const uint16_t year = file->readShort(readOffset + 0x18);
        if (checksumCoverage < 0x10 || checksumCoverage > fileLength || year < 1996 || year > 2099) {
          return false;
        }
      } else if (file->readShort(readOffset + 0x10) == 0x0300) {
        version = 0x0300;
        fileId = file->readShort(readOffset + 0x12);
      } else {
        return false;
      }

      effectTableOffset = 0x40;
      if (version == 0x0320) {
        // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The v0320 driver accepts effect numbers through +0x30
        // inclusive and resolves the default SWDM bank at +0x32. Its record layout places the track-offset table at
        // +0x10.
        effectSlotCount = static_cast<uint16_t>(file->readShort(readOffset + 0x30) + 1);
        defaultBankId = file->readShort(readOffset + 0x32);
      } else {
        // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named SsdPlayEffectParamData routine
        // treats +0x32 as an exclusive effect count, +0x34 as the bank ID, and record +8 as the offset table.
        effectSlotCount = file->readShort(readOffset + 0x32);
        defaultBankId = file->readShort(readOffset + 0x34);
      }
    }

    if (effectSlotCount == 0 || effectSlotCount > (fileLength - effectTableOffset) / 2) {
      return false;
    }
    internalName = file->readNullTerminatedString(readOffset + 0x20, 16);
    if (internalName.empty()) {
      internalName = objectName("SEDS", fileId);
    }

    std::vector<uint16_t> effectOffsets(effectSlotCount);
    const uint32_t firstEffectOffset = effectTableOffset + static_cast<uint32_t>(effectSlotCount) * 2;
    for (uint16_t effect = 0; effect < effectSlotCount; ++effect) {
      const uint16_t relativeOffset = file->readShort(readOffset + effectTableOffset + effect * 2);
      if (relativeOffset != 0 && (relativeOffset < firstEffectOffset || relativeOffset >= fileLength)) {
        return false;
      }
      effectOffsets[effect] = relativeOffset;
    }

    for (uint16_t effect = 0; effect < effectSlotCount; ++effect) {
      const uint32_t relativeOffset = effectOffsets[effect];
      if (relativeOffset == 0) {
        continue;
      }

      uint32_t relativeEnd = fileLength;
      for (uint16_t next = effect + 1; next < effectSlotCount; ++next) {
        if (effectOffsets[next] != 0) {
          if (effectOffsets[next] <= relativeOffset) {
            return false;
          }
          relativeEnd = effectOffsets[next];
          break;
        }
      }

      const uint32_t recordLength = relativeEnd - relativeOffset;
      const uint32_t recordOffset = readOffset + relativeOffset;
      const uint8_t effectHeaderSize = version == 0x0320 ? 0x10 : 0x08;
      if (recordLength < effectHeaderSize) {
        continue;
      }
      const uint8_t trackCount = file->readByte(recordOffset + 3);
      if (trackCount == 0 || static_cast<uint32_t>(trackCount) * 2 > recordLength - effectHeaderSize) {
        continue;
      }

      SequenceHeader sequence;
      sequence.offset = recordOffset;
      sequence.fileLength = recordLength;
      sequence.version = version;
      sequence.fileId = fileId;
      sequence.defaultBankId = defaultBankId;
      sequence.trackCount = trackCount;
      sequence.channelCount = trackCount;
      // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The named SsdPlaySeqEffectNormal routine
      // copies record +1 into 16.16 track state consumed by SsdNormalTrackEffect and SsdSetChannelVoiceVelocity.
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The v0320 driver performs the same operation with record +0xc
      // and does not read the authoring bytes at +0xd through +0xf.
      sequence.initialVoiceVelocity = file->readByte(recordOffset + (version == 0x0320 ? 0x0c : 0x01));
      sequence.isEffect = true;
      sequence.effectNumber = effect;
      sequence.effectHeaderSize = effectHeaderSize;
      sequence.internalName = fmt::format("{} Effect {}", internalName, effect);

      std::vector<uint16_t> trackOffsets(trackCount);
      bool validEffect = true;
      for (uint8_t track = 0; track < trackCount; ++track) {
        const uint16_t trackOffset = file->readShort(recordOffset + effectHeaderSize + track * 2);
        if (trackOffset != 0 &&
            (trackOffset < effectHeaderSize + static_cast<uint32_t>(trackCount) * 2 || trackOffset >= recordLength)) {
          validEffect = false;
          break;
        }
        trackOffsets[track] = trackOffset;
      }
      if (!validEffect) {
        continue;
      }

      for (uint8_t track = 0; track < trackCount; ++track) {
        const uint32_t trackOffset = trackOffsets[track];
        if (trackOffset == 0) {
          continue;
        }
        uint32_t trackEnd = recordLength;
        for (uint8_t next = track + 1; next < trackCount; ++next) {
          if (trackOffsets[next] != 0) {
            if (trackOffsets[next] <= trackOffset) {
              validEffect = false;
              break;
            }
            trackEnd = trackOffsets[next];
            break;
          }
        }
        if (!validEffect) {
          break;
        }
        sequence.tracks.push_back({recordOffset, effectHeaderSize, recordOffset + trackOffset, trackEnd - trackOffset,
                                   file->readByte(recordOffset), track, static_cast<uint8_t>(track & 0x0f),
                                   static_cast<uint8_t>(track >> 4)});
      }
      if (validEffect && !sequence.tracks.empty()) {
        effects.push_back(std::move(sequence));
      }
    }

    return true;
  }
};

struct BankHeader {
  uint32_t offset = 0;
  uint32_t fileLength = 0;
  uint16_t version = 0;
  uint16_t fileId = 0;
  uint16_t bankId = 0;
  uint8_t waveDataLoaderType = 0;
  uint8_t waveCount = 0;
  uint8_t programCount = 0;
  uint32_t sampleDataSize = 0;
  uint32_t sampleDataOffset = 0;
  uint32_t programDataOffset = 0;
  uint32_t programDataSize = 0;
  std::string internalName;
  std::vector<uint32_t> waveDescriptorOffsets;

  bool read(const RawFile* file, uint32_t readOffset) {
    offset = readOffset;
    if (readOffset > file->size() || file->size() - readOffset < 0x38 || file->readWordBE(readOffset) != kSwdmMagic) {
      return false;
    }

    fileLength = file->readWord(readOffset + 0x08);
    if (fileLength < 0x38 || fileLength > file->size() - readOffset || (fileLength & 3) != 0) {
      return false;
    }

    // [Bakusou Dekotora Densetsu: Otoko Hanamichi Yume Roman]: The unstripped MODULES/SSD.IRX checks v0300 at +0x10.
    // [Graffiti Kingdom]: The driver checks v0301 at +0x0c.
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: The later unstripped driver checks v0301 at
    // +0x0c.
    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The driver checks v0320 at +0x0c. Version branches below correspond
    // to layout differences established by these drivers and their data.
    const uint16_t commonHeaderVersion = file->readShort(readOffset + 0x0c);
    if (isV2(commonHeaderVersion)) {
      // [Shadow Hearts]: The named SsdAddWaveData routine reads this compact header directly, then copies waveCount
      // 32-byte descriptors and programCount 216-byte programs from 16-bit type and length chunks at +0x34.
      version = commonHeaderVersion;
      fileId = file->readShort(readOffset + 0x12);
      bankId = fileId;
      waveCount = file->readByte(readOffset + 0x14);
      programCount = file->readByte(readOffset + 0x15);
      sampleDataSize = file->readWord(readOffset + 0x1c);
      sampleDataOffset = file->readWord(readOffset + 0x20);
      if (sampleDataSize != 0 && (sampleDataOffset < 0x38 || sampleDataOffset > fileLength ||
                                  sampleDataSize > fileLength - sampleDataOffset)) {
        return false;
      }
      internalName = objectName("SWDM", bankId);

      bool foundTerminator = false;
      uint32_t cursor = readOffset + 0x34;
      const uint32_t fileEnd = readOffset + fileLength;
      while (cursor <= fileEnd && fileEnd - cursor >= 4) {
        const uint16_t type = file->readShort(cursor);
        const uint16_t recordSize = file->readShort(cursor + 2);
        if (recordSize < 4 || recordSize > fileEnd - cursor) {
          return false;
        }
        if (type == 0) {
          foundTerminator = true;
          break;
        }
        if (type == 3) {
          const uint32_t descriptorBytes = static_cast<uint32_t>(waveCount) * 0x20;
          if (recordSize < 0x10 || static_cast<uint32_t>(recordSize - 0x10) < descriptorBytes) {
            return false;
          }
          for (uint32_t descriptor = cursor + 0x10; descriptor < cursor + 0x10 + descriptorBytes; descriptor += 0x20) {
            waveDescriptorOffsets.push_back(descriptor);
          }
        } else if (type == 4) {
          const uint32_t programBytes = static_cast<uint32_t>(programCount) * 0xd8;
          if (programDataOffset != 0 || recordSize < 0x10 || static_cast<uint32_t>(recordSize - 0x10) < programBytes) {
            return false;
          }
          programDataOffset = cursor + 0x10;
          programDataSize = programBytes;
        }
        cursor += recordSize;
      }

      return foundTerminator && waveDescriptorOffsets.size() == waveCount &&
             (programCount == 0 || programDataOffset != 0);
    }

    if (fileLength < 0x60 || file->size() - readOffset < 0x50) {
      return false;
    }
    if (commonHeaderVersion == 0x0301 || commonHeaderVersion == 0x0320) {
      version = commonHeaderVersion;
      fileId = file->readShort(readOffset + 0x0e);
      const uint32_t checksumCoverage = file->readWord(readOffset + 0x14);
      const uint16_t year = file->readShort(readOffset + 0x18);
      if (checksumCoverage < 0x10 || checksumCoverage > fileLength || year < 1996 || year > 2099) {
        return false;
      }
    } else if (file->readShort(readOffset + 0x10) == 0x0300) {
      version = 0x0300;
      fileId = file->readShort(readOffset + 0x12);
    } else {
      return false;
    }

    internalName = file->readNullTerminatedString(readOffset + 0x20, 16);
    if (internalName.empty()) {
      return false;
    }

    // [Tokimeki Memorial: Girl's Side 2nd Kiss]: SSD registers a loaded bank by the common-header file ID at SWDM
    // +0x0e and compares it with the channel's 16-bit bank selector during every program lookup.
    // [Bakusou Dekotora Densetsu: Otoko Hanamichi Yume Roman]: Version 0x0300 files mirror that ID at +0x32.
    // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: Version 0x0301 files mirror that ID at +0x32.
    bankId = fileId;
    waveCount = file->readByte(readOffset + (version == 0x0320 ? 0x32 : 0x38));
    programCount = file->readByte(readOffset + (version == 0x0320 ? 0x33 : 0x39));
    if (version == 0x0320) {
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The v0320 loader uses this byte as a 0 through 5 dispatch index
      // for the wave-data loading routine. Every audited file selects loader type 3.
      waveDataLoaderType = file->readByte(readOffset + 0x34);
      if (waveDataLoaderType > 5) {
        return false;
      }
    }
    sampleDataSize = file->readWord(readOffset + 0x48);
    sampleDataOffset = file->readWord(readOffset + 0x4c);
    if (sampleDataSize != 0) {
      if (sampleDataOffset < 0x50 || sampleDataOffset > fileLength) {
        return false;
      }
      const uint32_t physicalSampleDataSize = fileLength - sampleDataOffset;
      if (version == 0x0320) {
        // [Tokimeki Memorial: Girl's Side 2nd Kiss]: SWDM +0x48 stores the requested SPU2 allocation size rounded
        // up to 0x40 bytes, while the file omits final allocation padding. All 741 audited banks follow this
        // relationship, so the physical payload determines the decoded sample length.
        const uint32_t alignedAllocationSize = (physicalSampleDataSize + 0x3f) & ~uint32_t{0x3f};
        if (sampleDataSize != alignedAllocationSize) {
          return false;
        }
        sampleDataSize = physicalSampleDataSize;
      } else if (sampleDataSize > physicalSampleDataSize) {
        return false;
      }
    }

    bool foundTerminator = false;
    uint32_t cursor = readOffset + 0x50;
    const uint32_t fileEnd = readOffset + fileLength;
    while (cursor <= fileEnd && fileEnd - cursor >= 0x10) {
      const uint32_t type = file->readWord(cursor);
      // [Tokimeki Memorial: Girl's Side 2nd Kiss]: The version 0x0320 loader reads aligned and logical sizes at +4
      // and +8 and uses record types 1 and 2.
      // [Bakusou Dekotora Densetsu: Otoko Hanamichi Yume Roman]: The named version 0x0300 routines read sizes at +8
      // and +0xc and use record types 3 and 4.
      // [Daito Giken Koushiki Pachi-Slot Simulator: 24 - Twenty Four]: Version 0x0301 uses the same fields and record
      // types as version 0x0300. Payloads begin at +0x10 in all three versions.
      const uint32_t sizeFieldOffset = version == 0x0320 ? 0x04 : 0x08;
      const uint32_t waveRecordType = version == 0x0320 ? 1 : 3;
      const uint32_t programRecordType = version == 0x0320 ? 2 : 4;
      const uint32_t recordSize = file->readWord(cursor + sizeFieldOffset);
      const uint32_t payloadSize = file->readWord(cursor + sizeFieldOffset + 4);
      if (recordSize < 0x10 || recordSize > fileEnd - cursor || payloadSize > recordSize - 0x10) {
        return false;
      }

      if (type == 0) {
        foundTerminator = true;
        break;
      }
      if (type == waveRecordType) {
        if ((payloadSize % 0x20) != 0) {
          return false;
        }
        for (uint32_t descriptor = cursor + 0x10; descriptor < cursor + 0x10 + payloadSize; descriptor += 0x20) {
          waveDescriptorOffsets.push_back(descriptor);
        }
      } else if (type == programRecordType) {
        if (programDataOffset != 0 || payloadSize < static_cast<uint32_t>(programCount) * 2) {
          return false;
        }
        programDataOffset = cursor + 0x10;
        programDataSize = payloadSize;
      }

      cursor += recordSize;
    }

    return foundTerminator && waveDescriptorOffsets.size() == waveCount &&
           (programCount == 0 || programDataOffset != 0);
  }
};

}  // namespace PSDSEPS2
