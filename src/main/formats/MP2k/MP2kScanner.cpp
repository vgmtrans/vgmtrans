/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 *
 * GBA MusicPlayer2000 (Sappy)
 *
 * Special Thanks / Respects To:
 * GBAMusRiper (c) 2012 by Bregalad
 * http://www.romhacking.net/utilities/881/
 * http://www.romhacking.net/documents/%5B462%5Dsappy.txt
 */

#include "MP2kScanner.h"
#include "MP2kSeq.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<MP2kScanner> s_mp2k("MP2K", {"gba", "gsf", "minigsf", "gsflib"});
}

#define SRCH_BUF_SIZE 0x20000

typedef struct {
  unsigned int polyphony           : 4;
  unsigned int main_vol            : 4;
  unsigned int sampling_rate_index : 4;
  unsigned int dac_bits            : 4;
}
    sound_engine_param_t;

static sound_engine_param_t sound_engine_param(uint32_t data) {
  sound_engine_param_t s;
  s.polyphony = (data & 0x000F00) >> 8;
  s.main_vol = (data & 0x00F000) >> 12;
  s.sampling_rate_index = (data & 0x0F0000) >> 16;
  s.dac_bits = 17 - ((data & 0xF00000) >> 20);
  return s;
}

// Test if an area of ROM is eligible to be the base pointer
static bool test_pointer_validity(RawFile *file, off_t offset, uint32_t inGBA_length) {
  sound_engine_param_t params = sound_engine_param(file->GetWord(offset));

  /* Compute (supposed ?) address of song table */
  uint32_t song_tbl_adr = (file->GetWord(offset + 8) & 0x3FFFFFF) + 12 * file->GetWord(offset + 4);

  /* Prevent illegal values for all fields */
  return params.main_vol != 0
      && params.polyphony <= 12
      && params.dac_bits >= 6
      && params.dac_bits <= 9
      && params.sampling_rate_index >= 1
      && params.sampling_rate_index <= 12
      && song_tbl_adr < inGBA_length
      && file->GetWord(offset + 4) < 256
      && ((file->GetWord(offset) & 0xff000000) == 0);
}

MP2kScanner::MP2kScanner(void) {
//	USE_EXTENSION("gba")
}

MP2kScanner::~MP2kScanner(void) {
}

void MP2kScanner::Scan(RawFile *file, void *info) {
  uint32_t sound_engine_adr;

  // detect the engine
  if (!Mp2kDetector(file, sound_engine_adr)) {
    return;
  }

  // compute address of song table
  uint32_t song_levels = file->GetWord(sound_engine_adr + 4);    // Read # of song levels
  uint32_t song_tbl_ptr = (file->GetWord(sound_engine_adr + 8) & 0x1FFFFFF) + 12 * song_levels;

  // Ignores entries which are made of 0s at the start of the song table
  // this fix was necessarily for the game Fire Emblem
  uint32_t song_pointer;
  while (true) {
    // check address range
    if (song_tbl_ptr + 4 > file->size()) {
      return;
    }

    song_pointer = file->GetWord(song_tbl_ptr);
    if (song_pointer != 0) break;
    song_tbl_ptr += 4;
  }

  unsigned int i = 0;
  while (true) {
    uint32_t song_entry = song_tbl_ptr + (i * 8);
    song_pointer = file->GetWord(song_entry) & 0x1FFFFFF;

    // Stop as soon as we met with an invalid pointer
    if (song_pointer == 0 || song_pointer >= file->size()) {
      break;
    }

    MP2kSeq *NewMP2kSeq = new MP2kSeq(file, song_pointer);
    if (!NewMP2kSeq->LoadVGMFile()) {
      delete NewMP2kSeq;
    }

    i++;
  };

  return;
}

// Sappy sound engine detector (c) 2014 by Bregalad, loveemu
bool MP2kScanner::Mp2kDetector(RawFile *file, uint32_t &mp2k_offset) {
  const uint8_t CODESEG_SELECTSONG[0x1E] =
      {
          0x00, 0xB5, 0x00, 0x04, 0x07, 0x4A, 0x08, 0x49,
          0x40, 0x0B, 0x40, 0x18, 0x83, 0x88, 0x59, 0x00,
          0xC9, 0x18, 0x89, 0x00, 0x89, 0x18, 0x0A, 0x68,
          0x01, 0x68, 0x10, 0x1C, 0x00, 0xF0,
      };

  const int M4A_MAIN_PATT_COUNT = 1;
  const int M4A_MAIN_LEN = 2;

  const uint8_t CODESEG_MAIN[M4A_MAIN_PATT_COUNT][M4A_MAIN_LEN] = {
      {0x00, 0xB5}
  };

  const int M4A_INIT_PATT_COUNT = 2;
  const int M4A_INIT_LEN = 2;

  const off_t M4A_OFFSET_SONGTABLE = 40;

  off_t m4a_selectsong_offset = -1;
  off_t m4a_main_offset = -1;

  off_t m4a_selectsong_search_offset = 0;
  while (m4a_selectsong_search_offset != -1) {
    m4a_selectsong_offset =
        LooseSearch(file, CODESEG_SELECTSONG, sizeof(CODESEG_SELECTSONG), m4a_selectsong_search_offset, 1, 0);
    if (m4a_selectsong_offset != -1) {
      // obtain song table address
      uint32_t m4a_songtable_address = file->GetWord(m4a_selectsong_offset + M4A_OFFSET_SONGTABLE);
      if (!IsGBAROMAddress(m4a_songtable_address)) {
        m4a_selectsong_search_offset = m4a_selectsong_offset + 1;
        continue;
      }
      uint32_t m4a_songtable_offset_tmp = GBAAddressToOffset(m4a_songtable_address);
      if (!IsValidOffset(m4a_songtable_offset_tmp + 4 - 1, file->size())) {
        m4a_selectsong_search_offset = m4a_selectsong_offset + 1;
        continue;
      }

      // song table must have more than one song
      int validsongcount = 0;
      for (int songindex = 0; validsongcount < 1; songindex++) {
        uint32_t songaddroffset = m4a_songtable_offset_tmp + (songindex * 8);
        if (!IsValidOffset(songaddroffset + 4 - 1, file->size())) {
          break;
        }

        uint32_t songaddr = file->GetWord(songaddroffset);
        if (songaddr == 0) {
          continue;
        }

        if (!IsGBAROMAddress(songaddr)) {
          break;
        }
        if (!IsValidOffset(GBAAddressToOffset(songaddr) + 4 - 1, file->size())) {
          break;
        }
        validsongcount++;
      }
      if (validsongcount < 1) {
        m4a_selectsong_search_offset = m4a_selectsong_offset + 1;
        continue;
      }
      break;
    }
    else {
      m4a_selectsong_search_offset = -1;
    }
  }
  if (m4a_selectsong_offset == -1) {
    return false;
  }

  uint32_t m4a_main_offset_tmp = m4a_selectsong_offset;
  if (!IsValidOffset(m4a_main_offset_tmp + M4A_MAIN_LEN - 1, file->size())) {
    return false;
  }
  while (m4a_main_offset_tmp > 0 && m4a_main_offset_tmp > ((uint32_t) m4a_selectsong_offset - 0x20)) {
    for (int mainpattern = 0; mainpattern < M4A_MAIN_PATT_COUNT; mainpattern++) {
      if (file->MatchBytes(CODESEG_MAIN[mainpattern], m4a_main_offset_tmp, M4A_INIT_LEN)) {
        m4a_main_offset = (long) m4a_main_offset_tmp;
        break;
      }
    }
    m4a_main_offset_tmp--;
  }

  // Test validity of engine offset with -16 and -32
  bool valid_m16 = test_pointer_validity(file, m4a_main_offset - 16, file->size());    // For most games
  bool valid_m32 = test_pointer_validity(file, m4a_main_offset - 32, file->size());    // For pokemon

  // If neither is found there is an error
  if (!valid_m16 && !valid_m32) {
    // Only a partial sound engine was found
    return false;
  }
  mp2k_offset = m4a_main_offset - (valid_m16 ? 16 : 32);
  return true;
}

bool MP2kScanner::IsValidOffset(uint32_t offset, uint32_t romsize) {
  return (offset < romsize);
}

bool MP2kScanner::IsGBAROMAddress(uint32_t address) {
  uint8_t region = (address >> 24) & 0xFE;
  return (region == 8);
}

uint32_t MP2kScanner::GBAAddressToOffset(uint32_t address) {
  if (!IsGBAROMAddress(address)) {
    fprintf(stderr, "Warning: the address $%08X is not ROM address\n", address);
  }
  return address & 0x01FFFFFF;
}

off_t MP2kScanner::LooseSearch(RawFile *file, const uint8_t *src, size_t srcsize, off_t file_offset,
                               size_t alignment, int diff_threshold) {
  // alignment must be more than 0
  if (alignment == 0) {
    return -1;
  }

  // aligns the start offset
  if (file_offset % alignment != 0) {
    file_offset += alignment - (file_offset % alignment);
  }

  // do loose filemem search
  for (size_t offset = file_offset; (offset + srcsize) <= file->size(); offset += alignment) {
    int diff = 0;

    for (size_t i = 0; i < srcsize; i++) {
      // count how many bytes are different
      if (file->GetByte(offset + i) != src[i]) {
        diff++;
      }

      // if diffs exceeds threshold, quit the matching for the offset
      if (diff > diff_threshold) {
        break;
      }
    }

    // if diffs is less or equal than threshold, it's matched
    if (diff <= diff_threshold) {
      return offset;
    }
  }

  return -1;
}
