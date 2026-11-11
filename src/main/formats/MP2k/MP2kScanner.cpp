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

#include "base/Types.h"
#include "LogManager.h"
#include "MP2kInstrSet.h"
#include "MP2kSeq.h"
#include "ScannerManager.h"
#include "VGMMetadataHint.h"
#include "VGMColl.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <spdlog/fmt/fmt.h>

namespace vgmtrans::scanners {
ScannerRegistration<MP2kScanner> s_mp2k("MP2k", {"gba", "gsf", "minigsf", "gsflib"});
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

struct MP2kSeqWithIndex {
  u32 song_index;
  std::string collection_name;
  MP2kSeq *seq;
};

constexpr auto MP2K_FORMAT_NAME = "MP2k";
constexpr u32 GBA_ROM_BASE = 0x08000000;

const VGMMetadataHint* findMP2kMetadataHint(RawFile* file, u32 song_index, u32 song_pointer) {
  if (const auto* hint = file->findMetadataHint(VGMMetadataHintQuery{
      .targetFormat = MP2K_FORMAT_NAME,
      .songIndex = song_index,
  })) {
    return hint;
  }

  if (const auto* hint = file->findMetadataHint(VGMMetadataHintQuery{
      .targetFormat = MP2K_FORMAT_NAME,
      .romAddress = GBA_ROM_BASE | song_pointer,
  })) {
    return hint;
  }

  return file->findMetadataHint(VGMMetadataHintQuery{
      .targetFormat = MP2K_FORMAT_NAME,
      .fileOffset = song_pointer,
  });
}

/* Test if an area of ROM is eligible to be the base pointer */
static bool test_pointer_validity(RawFile *file, size_t offset, u32 inGBA_length) {
  EngineParams params(file->get<u32>(offset));

  /* Compute supposed address of song table and check validity */
  u32 song_tbl_adr = (file->get<u32>(offset + 8) & 0x3FFFFFF) + 12 * file->get<u32>(offset + 4);
  auto valid_table =
      file->get<u32>(offset + 4) < 256 && song_tbl_adr < inGBA_length &&
      ((file->get<u32>(offset) & 0xff000000) == 0);

  return params.valid() && valid_table;
}

void MP2kScanner::scan(RawFile *file, void *) {
  /* Detect if the sound engine is actually MP2K */
  const auto engine = detectMP2K(file);
  if (!engine) {
    return;
  }

  EngineParams engine_settings(file->get<u32>(engine->settingsOffset));
  const int engine_sample_rate = samplerate_LUT[engine_settings.sampling_rate_index];
  const u32 psg_sample_rate = engine_sample_rate > 0 ? static_cast<u32>(engine_sample_rate) : 32768;

  const u32 song_tbl_ptr = engine->songTableOffset;
  if (song_tbl_ptr >= file->size()) {
    return;
  }

  auto* psg_sampcoll = pRoot->loadVGMFile<MP2kPSGColl>(file, psg_sample_rate, psg_sample_rate);
  if (!psg_sampcoll) {
    return;
  }

  // Each eight-byte entry contains a sequence pointer and player info.
  std::set<size_t> soundbanks;
  std::map<u32, std::vector<MP2kSeqWithIndex>> seqs;
  for (size_t entry_offset = song_tbl_ptr; entry_offset + 8 <= file->size(); entry_offset += 8) {
    const u32 song_index = static_cast<u32>((entry_offset - song_tbl_ptr) / 8);
    const u32 song_address = file->get<u32>(entry_offset);

    // Empty slots can occur anywhere in the table. Keep their indices for miniGSF metadata.
    if (song_address == 0) {
      continue;
    }
    if (!isGbaRomAddress(song_address)) {
      break;
    }

    const u32 song_pointer = gbaAddressToOffset(song_address);
    if (song_pointer >= file->size()) {
      L_DEBUG("Song pointer is out of bounds {:#x}/{:#x}", song_address, file->size());
      break;
    }

    const auto* hint = findMP2kMetadataHint(file, song_index, song_pointer);
    const bool hasHintTitle = hint != nullptr && hint->tag.hasTitle();
    const auto seq_name = hasHintTitle ? hint->tag.title : std::string("MP2kSeq");

    auto* nseq = pRoot->loadVGMFile<MP2kSeq>(file, song_pointer, seq_name);
    if (!nseq) {
      continue;
    }

    /* Load the soundbanks later because we need to know the number of instruments in each of
     * them */
    u32 inst_pointer = file->get<u32>(song_pointer + 4) & 0x1FFFFFF;
    const auto collection_name =
        hasHintTitle ? hint->tag.title : fmt::format("MP2k Collection #{}", song_index);
    soundbanks.insert(inst_pointer);
    if (auto inst = seqs.find(inst_pointer); inst != seqs.end()) {
      inst->second.push_back({song_index, collection_name, nseq});
    } else {
      seqs.insert({inst_pointer, std::vector<MP2kSeqWithIndex>{{song_index, collection_name, nseq}}});
    }
  }

  for (auto it = soundbanks.begin(); it != soundbanks.end(); std::advance(it, 1)) {
    auto seq = seqs.find(*it);
    if (seq == seqs.end() || *it >= file->size()) {
      continue;
    }

    // Banks have no stored length. Reading unused entries can run into adjacent keymaps or samples.
    u32 required_count = 1;  // Tracks may use the default program without a program-change event.
    for (const auto& seqval : seq->second) {
      for (u32 program : seqval.seq->aInstrumentsUsed) {
        required_count = std::max(required_count, program + 1);
      }
    }
    int count = static_cast<int>(std::min<u32>(128, required_count));
    if (auto next_addr = std::next(it);
        next_addr != soundbanks.end() && (*next_addr - *it) / 12 < 128) {
      count = std::min(count, static_cast<int>((*next_addr - *it) / 12));
    }
    count = std::min(count, static_cast<int>((file->size() - *it) / 12));
    if (count == 0) {
      continue;
    }

    auto iset = std::make_unique<MP2kInstrSet>(file, samplerate_LUT[engine_settings.sampling_rate_index], *it, count,
                                               psg_sampcoll);
    auto* rawInstrSet = iset.get();
    if (pRoot->loadVGMFile(std::move(iset))) {
      for (auto seqval : seq->second) {
        auto coll = std::make_unique<VGMColl>(seqval.collection_name);
        coll->attachSeq(seqval.seq);
        coll->attachInstrSet(rawInstrSet);
        if (rawInstrSet->sampColl() != nullptr && rawInstrSet->sampColl()->hasSamples()) {
          coll->attachSampColl(rawInstrSet->sampColl());
        }
        coll->attachSampColl(psg_sampcoll);

        pRoot->loadVGMColl(std::move(coll));
      }
      seqs.erase(seq);
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

std::optional<MP2kScanner::EngineLocation> MP2kScanner::detectMP2K(RawFile *file) {
  auto select_song = MP2KPatternSearch(file, file->begin());

  constexpr u32 M4A_OFFSET_SONGTABLE = 40;
  while (select_song != file->end()) {
    u32 songtable_addr = file->get<u32>(select_song - file->begin() + M4A_OFFSET_SONGTABLE);
    if (!isGbaRomAddress(songtable_addr)) {
      select_song = MP2KPatternSearch(file, std::next(select_song));
      continue;
    }

    u32 songtable_ofs_tmp = gbaAddressToOffset(songtable_addr);
    if (!isValidOffset(songtable_ofs_tmp + 4 - 1, file->size())) {
      select_song = MP2KPatternSearch(file, std::next(select_song));
      continue;
    }

    u32 validsongcount = 0;
    for (u32 songindex = 0; validsongcount < 1; songindex++) {
      u32 songaddroffset = songtable_ofs_tmp + (songindex * 8);
      if (!isValidOffset(songaddroffset + 4 - 1, file->size())) {
        break;
      }

      u32 songaddr = file->readWord(songaddroffset);
      if (songaddr == 0) {
        continue;
      }

      if (!isGbaRomAddress(songaddr)) {
        break;
      }

      if (!isValidOffset(gbaAddressToOffset(songaddr) + 4 - 1, file->size())) {
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
  if (!isValidOffset(main_ofs_tmp + 2 - 1, file->size())) {
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

  // Use the table referenced by the selection routine; it need not follow the player array.
  return EngineLocation{
      .settingsOffset = main_ofs - (valid_m16 ? 16 : 32),
      .songTableOffset = gbaAddressToOffset(
          file->get<u32>(select_song - file->begin() + M4A_OFFSET_SONGTABLE)),
  };
}

bool MP2kScanner::isValidOffset(u32 offset, u32 romsize) {
  return (offset < romsize);
}

bool MP2kScanner::isGbaRomAddress(u32 address) {
  u8 region = (address >> 24) & 0xFE;
  return (region == 8);
}

u32 MP2kScanner::gbaAddressToOffset(u32 address) {
  if (!isGbaRomAddress(address)) {
    L_WARN("Address {:#x} is not a ROM address", address);
  }

  return address & 0x01FFFFFF;
}
