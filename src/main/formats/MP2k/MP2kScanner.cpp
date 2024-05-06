/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file

 * GBA MusicPlayer2000 (Sappy)
 *
 * Special Thanks / Respects To:
 * GBAMusRiper (c) 2012 by Bregalad
 * http://www.romhacking.net/utilities/881/
 * http://www.romhacking.net/documents/%5B462%5Dsappy.txt
 *
 * Sappy sound engine detector (c) 2014 by Bregalad, loveemu
 */

#include "MP2kScanner.h"

#include <optional>
#include <functional>
#include <set>
#include <span>

#include "VGMColl.h"
#include "MP2kSeq.h"
#include "MP2kInstrSet.h"
#include "LogManager.h"
#include "ScannerManager.h"
namespace vgmtrans::scanners {
ScannerRegistration<MP2kScanner> s_mp2k("MP2K", {"gba", "gsf", "minigsf", "gsflib"});
}

static constexpr int samplerate_LUT[16] = {-1,    5734,  7884,  10512, 13379, 15768, 18157, 21024,
                                           26758, 31536, 36314, 40137, 42048, -1,    -1,    -1};

struct EngineParams {
  EngineParams(u32 data)
      : polyphony((data & 0x000F00) >> 8), main_vol((data & 0x00F000) >> 12),
        sampling_rate_index((data & 0x0F0000) >> 16), dac_bits(17 - ((data & 0xF00000) >> 20)){};

  bool valid() const noexcept {
    return main_vol != 0 && polyphony < 13 && dac_bits >= 6 && dac_bits <= 9 &&
           sampling_rate_index >= 1 && sampling_rate_index <= 12;
  }

  const u32 polyphony;
  const u32 main_vol;
  const u32 sampling_rate_index;
  const u32 dac_bits;
};

/* Test if an area of ROM is eligible to be the base pointer */
static bool test_pointer_validity(RawFile *file, size_t offset, uint32_t inGBA_length) {
  EngineParams params(file->get<u32>(offset));

  /* Compute supposed address of song table and check validity */
  u32 song_tbl_adr = (file->get<u32>(offset + 8) & 0x3FFFFFF) + 12 * file->get<u32>(offset + 4);
  auto valid_table =
      file->get<u32>(offset + 4) < 256 && ((file->get<u32>(offset) & 0xff000000) == 0);

  return params.valid() && valid_table;
}

void MP2kScanner::Scan(RawFile *file, void *info) {
  /* Detect if the sound engine is actually MP2K */
  size_t sound_engine_adr = 0;
  if (auto engine = DetectMP2K(file); engine.has_value()) {
    sound_engine_adr = engine.value();
  } else {
    return;
  }

  EngineParams engine_settings(file->get<u32>(sound_engine_adr));

  /* Compute song table location */
  u32 song_levels = file->get<u32>(sound_engine_adr + 4);  // Read # of song levels
  u32 song_tbl_ptr = (file->get<u32>(sound_engine_adr + 8) & 0x1FFFFFF) + 12 * song_levels;
  auto song_tbl = std::span<const u32>(reinterpret_cast<const u32 *>(file->begin() + song_tbl_ptr),
                                       reinterpret_cast<const u32 *>(file->end()));

  /* Sometimes the song table contains null-pointers (e.g. Fire Emblem).
   * We will ignore those entries */
  auto song_entry =
      std::find_if(song_tbl.begin(), song_tbl.end(), [](u32 pointer) { return pointer != 0; });
  if (song_entry == song_tbl.end()) {
    return;
  }

  /* First 32 bytes are the pointer, the rest is song info */
  std::set<size_t> soundbanks;
  std::map<u32, std::vector<MP2kSeq *>> seqs;
  for (auto it = song_entry; it < song_tbl.end(); std::advance(it, 2)) {
    u32 song_pointer = *it & 0x1FFFFFF;
    if (song_pointer >= file->size()) {
      L_DEBUG("Song pointer is out of bounds {:#x}/{:#x}", *it, file->size());
      break;
    }

    /* Terminate on invalid pointer for safety */
    if (!song_pointer || song_pointer >= file->size()) {
      break;
    }

    auto nseq = new MP2kSeq(file, song_pointer);
    if (!nseq->LoadVGMFile()) {
      delete nseq;
    }

    /* Load the soundbanks later because we need to know the number of instruments in each of
     * them */
    u32 inst_pointer = file->get<u32>(song_pointer + 4) & 0x1FFFFFF;
    soundbanks.insert(inst_pointer);
    if (auto inst = seqs.find(inst_pointer); inst != seqs.end()) {
      inst->second.emplace_back(nseq);
    } else {
      seqs.insert(std::pair(inst_pointer, std::vector<MP2kSeq *>{nseq}));
    }
  }

  for (auto it = soundbanks.begin(); it != soundbanks.end(); std::advance(it, 1)) {
    int count = 128;
    if (auto next_addr = std::next(it);
        next_addr != soundbanks.end() && (*next_addr - *it) / 12 < 128) {
      count = (*next_addr - *it) / 12;
    }

    if (auto iset =
            new MP2kInstrSet(file, samplerate_LUT[engine_settings.sampling_rate_index], *it, count);
        !iset->LoadVGMFile()) {
      delete iset;
    } else {
      auto seq = seqs.find(*it);
      if (seq != seqs.end()) {
        for (auto seqval : seq->second) {
          auto coll = new VGMColl("MP2k Collection");
          coll->UseSeq(seqval);
          coll->AddInstrSet(iset);
          coll->AddSampColl(iset->sampColl);

          if (!coll->Load()) {
            delete coll;
          }
        }
        seqs.erase(seq);
      }
    }
  }
}

static auto MP2KPatternSearch(RawFile *file, const char *offset) {
  static constexpr std::array<u8, 0x1E> SONGSELECT_PATTERN = {
      0x00, 0xB5, 0x00, 0x04, 0x07, 0x4A, 0x08, 0x49, 0x40, 0x0B, 0x40, 0x18, 0x83, 0x88, 0x59,
      0x00, 0xC9, 0x18, 0x89, 0x00, 0x89, 0x18, 0x0A, 0x68, 0x01, 0x68, 0x10, 0x1C, 0x00, 0xF0,
  };

  static constexpr std::array<u8, 0x1E> SONGSELECT_PATTERN_V2 = {
      0x00, 0xB5, 0x00, 0x04, 0x07, 0x4B, 0x08, 0x49, 0x40, 0x0B, 0x40, 0x18, 0x82, 0x88, 0x51,
      0x00, 0x89, 0x18, 0x89, 0x00, 0xC9, 0x18, 0x0A, 0x68, 0x01, 0x68, 0x10, 0x1C, 0x00, 0xF0,
  };

  static const auto v1_searcher =
      std::boyer_moore_horspool_searcher(SONGSELECT_PATTERN.begin(), SONGSELECT_PATTERN.end());

  static const auto v2_searcher = std::boyer_moore_horspool_searcher(SONGSELECT_PATTERN_V2.begin(),
                                                                     SONGSELECT_PATTERN_V2.end());

  auto off = reinterpret_cast<const u8 *>(offset);
  auto end = reinterpret_cast<const u8 *>(file->end());

  /* Scan for the song selection code pattern */
  auto select_song = std::search(off, end, v1_searcher);

  /* Not found, let's try the updated version */
  if (select_song == end) {
    select_song = std::search(off, end, v2_searcher);
  }

  return reinterpret_cast<const char *>(select_song);
}

std::optional<size_t> MP2kScanner::DetectMP2K(RawFile *file) {
  auto select_song = MP2KPatternSearch(file, file->begin());

  constexpr u32 M4A_OFFSET_SONGTABLE = 40;
  while (select_song != file->end()) {
    u32 songtable_addr = file->get<u32>(select_song - file->begin() + M4A_OFFSET_SONGTABLE);
    if (!IsGBAROMAddress(songtable_addr)) {
      select_song = MP2KPatternSearch(file, std::next(select_song));
      continue;
    }

    u32 songtable_ofs_tmp = GBAAddressToOffset(songtable_addr);
    if (!IsValidOffset(songtable_ofs_tmp + 4 - 1, file->size())) {
      select_song = MP2KPatternSearch(file, std::next(select_song));
      continue;
    }

    u32 validsongcount = 0;
    for (u32 songindex = 0; validsongcount < 1; songindex++) {
      u32 songaddroffset = songtable_ofs_tmp + (songindex * 8);
      if (!IsValidOffset(songaddroffset + 4 - 1, file->size())) {
        break;
      }

      u32 songaddr = file->GetWord(songaddroffset);
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
      select_song = MP2KPatternSearch(file, std::next(select_song));
      continue;
    } else {
      break;
    }
  }

  /* Now we know this isn't using MP2K */
  if (select_song == file->end()) {
    return std::nullopt;
  }

  u32 main_ofs_tmp = select_song - file->begin();
  if (!IsValidOffset(main_ofs_tmp + 2 - 1, file->size())) {
    return std::nullopt;
  }

  /* Pick the very first instance of the code pattern */
  u32 main_ofs = 0;
  while (main_ofs_tmp > 0 &&
         main_ofs_tmp > (static_cast<u32>(select_song - file->begin()) - 0x20)) {
    if (file->get<u16>(main_ofs_tmp) == 0xB500) {
      main_ofs = main_ofs_tmp;
    }

    main_ofs_tmp--;
  }

  /* Settings location used by most games */
  bool valid_m16 = test_pointer_validity(file, main_ofs - 16, file->size());

  /* Settings location used by Pok�mon */
  bool valid_m32 = test_pointer_validity(file, main_ofs - 32, file->size());

  if (!(valid_m16 || valid_m32)) {
    L_WARN("Couldn't find settings block around {} in {}", main_ofs, file->name());
    return std::nullopt;
  }

  return main_ofs - (valid_m16 ? 16 : 32);
}

bool MP2kScanner::IsValidOffset(u32 offset, u32 romsize) {
  return (offset < romsize);
}

bool MP2kScanner::IsGBAROMAddress(u32 address) {
  u8 region = (address >> 24) & 0xFE;
  return (region == 8);
}

u32 MP2kScanner::GBAAddressToOffset(u32 address) {
  if (!IsGBAROMAddress(address)) {
    L_WARN("Address {:#x} is not a ROM address", address);
  }

  return address & 0x01FFFFFF;
}
