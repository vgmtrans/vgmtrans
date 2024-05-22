/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "HeartBeatPS1Scanner.h"
#include "HeartBeatPS1Format.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<HeartBeatPS1Scanner> s_heartbeat_ps1("HEARTBEATPS1");
}

#define SRCH_BUF_SIZE 0x20000

HeartBeatPS1Scanner::HeartBeatPS1Scanner() {}

HeartBeatPS1Scanner::~HeartBeatPS1Scanner() {}

void HeartBeatPS1Scanner::Scan(RawFile *file, void* /*info*/) {
  SearchForHeartBeatPS1VGMFile(file);
}

std::vector<VGMFile *> HeartBeatPS1Scanner::SearchForHeartBeatPS1VGMFile(RawFile *file) {
  std::vector<VGMFile *> loadedFiles;

  for (uint32_t offset = 0; offset + HEARTBEATPS1_SND_HEADER_SIZE <= file->size(); offset++) {
    uint32_t curOffset = offset;

    uint32_t seq_size = file->GetWord(curOffset);
    uint16_t seq_id = file->GetShort(curOffset + 4);
    uint8_t num_instrsets = file->GetByte(curOffset + 6);

    // check SEQ size
    if (seq_size != 0) {
      if (seq_size < 0x13 || seq_size > 0x200000) {
        continue;
      }
    }

    // SEQ id 0xFFFF is used for invalid id,
    // it must not be used, perhaps.
    if (seq_id == 0xFFFF) {
      continue;
    }

    // bank count cannot be greater than 4,
    // because of the file design.
    if (num_instrsets > 4) {
      continue;
    }

    curOffset += 0x0c;

    // validate instrument header
    bool valid_instrset = true;
    uint32_t total_instr_size = 0;
    std::vector<uint16_t> insetset_ids;
    for (uint8_t instrset_index = 0; instrset_index < 4; instrset_index++) {
      uint32_t sampcoll_size = file->GetWord(curOffset);
      uint32_t instrset_size = file->GetWord(curOffset + 0x04);
      uint16_t instrset_id = file->GetShort(curOffset + 0x08);

      // check instrset id
      if (instrset_id > 4 && instrset_id != 0xFFFF) {
        valid_instrset = false;
        break;
      }

      // check sample/instrset size
      if (sampcoll_size != 0 || instrset_size != 0) {
        // check instrset id collision
        if (std::ranges::find(insetset_ids, instrset_id) != insetset_ids.end()) {
          valid_instrset = false;
          break;
        }
        insetset_ids.push_back(instrset_id);

        // check missing sample/instrset
        if (sampcoll_size == 0 || instrset_size == 0) {
          valid_instrset = false;
          break;
        }

        // check sample/instrset size limit
        if (sampcoll_size > 0x200000 || instrset_size > 0x200000) {
          valid_instrset = false;
          break;
        }

        // check PSX-ADPCM alignment
        if (sampcoll_size % 16 != 0) {
          valid_instrset = false;
          break;
        }

        // check instrset alignment
        if (instrset_size % 4 != 0) {
          valid_instrset = false;
          break;
        }
      }

      // check PSX-ADPCM content
      if (sampcoll_size != 0) {
        uint32_t sampLoopInfoOffset = offset + HEARTBEATPS1_SND_HEADER_SIZE + total_instr_size + 1;
        if (sampLoopInfoOffset >= file->size()) {
          valid_instrset = false;
          break;
        }

        // check unused bits of the first loop flag
        uint8_t sampLoopInfo = file->GetByte(sampLoopInfoOffset);
        if ((sampLoopInfo & 0xF8) != 0) {
          valid_instrset = false;
          break;
        }
      }

      total_instr_size += sampcoll_size;
      total_instr_size += instrset_size;

      curOffset += 0x0c;
    }

    if (!valid_instrset) {
      continue;
    }

    // check total file size
    uint32_t total_size = HEARTBEATPS1_SND_HEADER_SIZE + total_instr_size + seq_size;
    if (total_size > 0x200000 || offset + total_size > file->size()) {
      continue;
    }

    // validate sequence header
    uint32_t seq_offset = HEARTBEATPS1_SND_HEADER_SIZE + total_instr_size;
    if (seq_size != 0) {
      constexpr uint8_t SEQ_SIGNATURE[4] = {'q', 'Q', 'E', 'S'};
      if (!file->MatchBytes(SEQ_SIGNATURE, offset + seq_offset, sizeof(SEQ_SIGNATURE))) {
        continue;
      }
    }

    // valid file, open it as VGMFile
    if (total_instr_size != 0) {
      //LogDebug("HeartBeatPS1InstrSet 0x%X", offset);
    }

    if (seq_size != 0) {
      HeartBeatPS1Seq *newHeartBeatPS1Seq = new HeartBeatPS1Seq(file, offset, total_size);
      if (newHeartBeatPS1Seq->LoadVGMFile()) {
        loadedFiles.push_back(newHeartBeatPS1Seq);
      } else {
        delete newHeartBeatPS1Seq;
      }
    }

    // skip known bytes, next
    offset += total_size - 1;
  }

  return loadedFiles;
}
