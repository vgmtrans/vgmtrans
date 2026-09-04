#include "PSDSEScanner.h"

#include "RawFile.h"
#include "PSDSEInstrSet.h"
#include "PSDSEPS2Header.h"
#include "PSDSEPS2InstrSet.h"
#include "PSDSEPS2Seq.h"
#include "PSDSESeq.h"
#include "ScannerManager.h"
#include "VGMColl.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace vgmtrans {
namespace scanners {

ScannerRegistration<PSDSEScanner> s_psdse("PSDSE", {"nds", "srl", "swd", "smd", "swdm", "smdm", "seds", "sed", "sad",
                                                    "dse", "sir0"});

}  // namespace scanners
}  // namespace vgmtrans

std::vector<PSDSESampColl*> PSDSEScanner::g_loadedSampColls;
std::vector<PSDSEInstrSet*> PSDSEScanner::g_loadedInstrSets;
std::vector<PSDSEPS2SampColl*> PSDSEScanner::g_loadedPS2SampColls;
std::vector<PSDSEPS2InstrSet*> PSDSEScanner::g_loadedPS2InstrSets;

PSDSEScanner::PSDSEScanner(Format* format) : VGMScanner(format) {
}

namespace {

std::string collectionKey(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const size_t extension = name.find_last_of('.');
  if (extension != std::string::npos) {
    name.resize(extension);
  }
  return name;
}

std::string compactCollectionKey(std::string name) {
  name = collectionKey(std::move(name));
  std::erase_if(name, [](unsigned char c) { return !std::isalnum(c); });
  return name;
}

std::string programBankKey(std::string name) {
  name = collectionKey(std::move(name));
  constexpr std::string_view suffix = "_prog";
  if (name.ends_with(suffix)) {
    name.resize(name.size() - suffix.size());
  }
  return name;
}

struct WaviDescriptor {
  uint8_t rootKey;
  uint8_t volume;
  uint8_t pan;
  uint16_t format;
  uint8_t loop;
  uint32_t sampleRate;
  uint32_t loopStart;
  uint32_t loopLength;

  bool operator==(const WaviDescriptor&) const = default;
};

std::optional<WaviDescriptor> readWaviDescriptor(const RawFile* file, const SWDLHeader& header, size_t slot) {
  if (header.waviOffset == 0 || slot >= header.nbwavislots) {
    return std::nullopt;
  }

  const uint64_t tableOffset = static_cast<uint64_t>(header.waviOffset) + 0x10;
  const uint64_t pointerOffset = tableOffset + slot * sizeof(uint16_t);
  if (pointerOffset + sizeof(uint16_t) > file->size()) {
    return std::nullopt;
  }
  const uint16_t relativeOffset = PSDSE::readU16(file, static_cast<uint32_t>(pointerOffset), header.endianness);
  if (relativeOffset == 0) {
    return std::nullopt;
  }

  const uint64_t descriptorOffset = tableOffset + relativeOffset;
  constexpr size_t descriptorSize = 0x30;
  const uint64_t bankEnd = static_cast<uint64_t>(header.offset) + header.fileLength;
  if (descriptorOffset + descriptorSize > file->size() || descriptorOffset + descriptorSize > bankEnd) {
    return std::nullopt;
  }

  if (header.version == 0x0402) {
    return WaviDescriptor{
        file->readByte(descriptorOffset + 0x04),
        file->readByte(descriptorOffset + 0x06),
        file->readByte(descriptorOffset + 0x07),
        PSDSE::readU16(file, descriptorOffset + 0x08, header.endianness),
        file->readByte(descriptorOffset + 0x11),
        PSDSE::readU16(file, descriptorOffset + 0x12, header.endianness),
        PSDSE::readU32(file, descriptorOffset + 0x18, header.endianness),
        PSDSE::readU32(file, descriptorOffset + 0x1c, header.endianness),
    };
  }

  return WaviDescriptor{
      file->readByte(descriptorOffset + 0x06),
      file->readByte(descriptorOffset + 0x08),
      file->readByte(descriptorOffset + 0x09),
      PSDSE::readU16(file, descriptorOffset + 0x12, header.endianness),
      file->readByte(descriptorOffset + 0x15),
      PSDSE::readU32(file, descriptorOffset + 0x20, header.endianness),
      PSDSE::readU32(file, descriptorOffset + 0x28, header.endianness),
      PSDSE::readU32(file, descriptorOffset + 0x2c, header.endianness),
  };
}

PSDSEInstrSet* findInstrSetForSequence(const PSDSESeq* seq) {
  const std::string rawFileKey = collectionKey(seq->rawFile()->name());
  const auto rawFileMatch = std::find_if(
      PSDSEScanner::g_loadedInstrSets.rbegin(), PSDSEScanner::g_loadedInstrSets.rend(),
      [seq, &rawFileKey](const PSDSEInstrSet* instrSet) {
        return instrSet->rawFile() != seq->rawFile() && collectionKey(instrSet->rawFile()->name()) == rawFileKey;
      });
  if (rawFileMatch != PSDSEScanner::g_loadedInstrSets.rend()) {
    return *rawFileMatch;
  }

  const std::string key = collectionKey(seq->name());
  std::vector<PSDSEInstrSet*> matches;
  std::copy_if(PSDSEScanner::g_loadedInstrSets.begin(), PSDSEScanner::g_loadedInstrSets.end(),
               std::back_inserter(matches),
               [&key](const PSDSEInstrSet* instrSet) { return collectionKey(instrSet->name()) == key; });
  if (matches.empty()) {
    const std::string compactKey = compactCollectionKey(seq->name());
    std::copy_if(
        PSDSEScanner::g_loadedInstrSets.begin(), PSDSEScanner::g_loadedInstrSets.end(), std::back_inserter(matches),
        [&compactKey](const PSDSEInstrSet* instrSet) { return compactCollectionKey(instrSet->name()) == compactKey; });
  }
  if (matches.empty()) {
    // [Rekishi Taisen: Gettenka - Tenkaichi Battle Royale]: NitroFS stores banks such as BG_T10_PROG.SWD, while the
    // bundled download-play executable requests /proc_swd/BG_T10.SWD beside /proc_seq/BG_T10.SMD. The _PROG suffix
    // is packaging metadata and participates in the normal exact-key and collision-resolution path.
    std::copy_if(PSDSEScanner::g_loadedInstrSets.begin(), PSDSEScanner::g_loadedInstrSets.end(),
                 std::back_inserter(matches),
                 [&key](const PSDSEInstrSet* instrSet) { return programBankKey(instrSet->name()) == key; });
  }
  if (matches.empty()) {
    // [Fushigi no Dungeon: Fuurai no Shiren DS 2: Sabaku no Majou]: The ARM9 cue table maps ITEM, LEVEL_UP, MON_UP,
    // NPC, and RANKING to FDS2_BNK_ME, and GAIBARA to SND_BGM_M_SOLO. SMDL and SWDL headers do not contain these
    // executable-defined associations, so a loader must supply that metadata before they can be resolved.
    PSDSEInstrSet* commonBank = nullptr;
    size_t commonBankKeyLength = 0;
    for (PSDSEInstrSet* instrSet : PSDSEScanner::g_loadedInstrSets) {
      const std::string instrKey = collectionKey(instrSet->name());
      if (instrKey.empty() || instrKey.size() >= key.size() || instrKey.size() <= commonBankKeyLength ||
          key.compare(0, instrKey.size(), instrKey) != 0 ||
          (key[instrKey.size()] != '_' && key[instrKey.size()] != '-')) {
        continue;
      }
      commonBank = instrSet;
      commonBankKeyLength = instrKey.size();
    }
    return commonBank;
  }
  PSDSEInstrSet* crossFileFallback = matches.back();

  // [Pokemon Mystery Dungeon: Explorers of Sky]: The 16-byte internal names are not unique, so file layout resolves
  // collisions between banks with the same truncated name.
  std::erase_if(matches, [seq](const PSDSEInstrSet* instrSet) { return instrSet->rawFile() != seq->rawFile(); });
  if (matches.empty()) {
    return crossFileFallback;
  }

  const uint64_t seqEnd = static_cast<uint64_t>(seq->offset()) + seq->length();
  PSDSEInstrSet* nearest = nullptr;
  uint64_t nearestDistance = std::numeric_limits<uint64_t>::max();
  for (PSDSEInstrSet* instrSet : matches) {
    if (instrSet->offset() < seqEnd) {
      continue;
    }
    const uint64_t distance = instrSet->offset() - seqEnd;
    if (distance < nearestDistance) {
      nearest = instrSet;
      nearestDistance = distance;
    }
  }
  if (nearest) {
    return nearest;
  }

  return *std::min_element(matches.begin(), matches.end(),
                           [seq](const PSDSEInstrSet* left, const PSDSEInstrSet* right) {
                             const auto distanceBeforeSequence = [seq](const PSDSEInstrSet* instrSet) {
                               const uint64_t instrEnd = static_cast<uint64_t>(instrSet->offset()) + instrSet->length();
                               return instrEnd < seq->offset() ? seq->offset() - instrEnd : uint64_t{0};
                             };
                             return distanceBeforeSequence(left) < distanceBeforeSequence(right);
                           });
}

PSDSEInstrSet* findInstrSetForBankId(const PSDSESeq* seq, uint16_t bankId) {
  std::vector<PSDSEInstrSet*> candidates;
  std::ranges::copy_if(PSDSEScanner::g_loadedInstrSets, std::back_inserter(candidates),
                       [seq, bankId](const PSDSEInstrSet* instrSet) {
                         return instrSet->m_header.id == bankId && instrSet->m_header.version == seq->version;
                       });
  if (candidates.empty()) {
    return nullptr;
  }

  std::vector<PSDSEInstrSet*> sameFile;
  std::ranges::copy_if(candidates, std::back_inserter(sameFile),
                       [seq](const PSDSEInstrSet* instrSet) { return instrSet->rawFile() == seq->rawFile(); });
  if (!sameFile.empty()) {
    candidates = std::move(sameFile);
  }
  if (candidates.size() == 1) {
    return candidates.front();
  }

  const uint64_t seqEnd = static_cast<uint64_t>(seq->offset()) + seq->length();
  PSDSEInstrSet* nearestFollowing = nullptr;
  uint64_t nearestFollowingDistance = std::numeric_limits<uint64_t>::max();
  PSDSEInstrSet* nearestPreceding = nullptr;
  uint64_t nearestPrecedingDistance = std::numeric_limits<uint64_t>::max();
  for (PSDSEInstrSet* instrSet : candidates) {
    if (instrSet->offset() >= seqEnd) {
      const uint64_t distance = instrSet->offset() - seqEnd;
      if (distance < nearestFollowingDistance) {
        nearestFollowing = instrSet;
        nearestFollowingDistance = distance;
      }
      continue;
    }

    const uint64_t bankEnd = static_cast<uint64_t>(instrSet->offset()) + instrSet->length();
    const uint64_t distance = bankEnd < seq->offset() ? seq->offset() - bankEnd : 0;
    if (distance < nearestPrecedingDistance) {
      nearestPreceding = instrSet;
      nearestPrecedingDistance = distance;
    }
  }

  // [Pokemon Mystery Dungeon: Explorers of Sky]: Song banks commonly follow their SMDL immediately. This layout is
  // used only as a tie-break after the authored SWDL file ID and version match.
  return nearestFollowing != nullptr ? nearestFollowing : nearestPreceding;
}

PSDSESampColl* findSampCollForInstrSet(const PSDSEInstrSet* instrSet) {
  if (instrSet->m_header.sampleStorageKind == 2) {
    std::vector<PSDSESampColl*> idMatches;
    const auto sourcePath = instrSet->rawFile()->path();
    std::copy_if(PSDSEScanner::g_loadedSampColls.begin(), PSDSEScanner::g_loadedSampColls.end(),
                 std::back_inserter(idMatches), [instrSet, &sourcePath](const PSDSESampColl* sampColl) {
                   const bool sameSource = sampColl->rawFile() == instrSet->rawFile() ||
                                           (!sourcePath.empty() && sampColl->rawFile()->path() == sourcePath);
                   return sameSource && sampColl->m_header.version == instrSet->m_header.version &&
                          sampColl->m_header.id == instrSet->m_header.mainBankId;
                 });
    // [Pokemon Mystery Dungeon: Explorers of Sky]: DseSwd_LoadBank reads the storage-kind-2 ID from +0x40 and passes
    // it to DseSwd_LoadWaves. The association is complete when it identifies one bank in the same ROM.
    if (idMatches.size() == 1) {
      return idMatches.front();
    }
    if (idMatches.size() > 1) {
      PSDSESampColl* bestMatch = nullptr;
      std::pair<size_t, size_t> bestScore{};
      bool tied = false;
      for (PSDSESampColl* sampColl : idMatches) {
        size_t exactMatches = 0;
        size_t coveredSlots = 0;
        for (size_t slot = 0; slot < instrSet->m_header.nbwavislots; ++slot) {
          const auto instrDescriptor = readWaviDescriptor(instrSet->rawFile(), instrSet->m_header, slot);
          if (!instrDescriptor) {
            continue;
          }
          const auto sampleDescriptor = readWaviDescriptor(sampColl->rawFile(), sampColl->m_header, slot);
          if (!sampleDescriptor) {
            continue;
          }
          ++coveredSlots;
          exactMatches += *sampleDescriptor == *instrDescriptor;
        }

        const std::pair score{exactMatches, coveredSlots};
        if (score > bestScore) {
          bestMatch = sampColl;
          bestScore = score;
          tied = false;
        } else if (score == bestScore && score.second != 0) {
          tied = true;
        }
      }

      // [Rekishi Taisen: Gettenka - Tenkaichi Battle Royale]: Two waveform banks use ID zero. The bundled
      // download-play executable references 0_ALL_WAVE.SWD, while external banks retain its sparse slot layout with
      // stale loop lengths. Exact descriptor matches followed by covered slots identify the associated bank.
      if (bestMatch && bestScore.second != 0 && !tied) {
        return bestMatch;
      }
    }
  }

  const std::string instrKey = collectionKey(instrSet->name());
  PSDSESampColl* bestMatch = nullptr;
  size_t bestMatchLength = 0;

  for (PSDSESampColl* sampColl : PSDSEScanner::g_loadedSampColls) {
    const std::string sampKey = collectionKey(sampColl->name());
    if (sampKey.empty() || sampKey.size() > instrKey.size()) {
      continue;
    }

    const bool exactMatch = sampKey == instrKey;
    const bool prefixMatch = !exactMatch && instrKey.compare(0, sampKey.size(), sampKey) == 0 &&
                             (instrKey[sampKey.size()] == '_' || instrKey[sampKey.size()] == '-');
    if ((exactMatch || prefixMatch) && sampKey.size() > bestMatchLength) {
      bestMatch = sampColl;
      bestMatchLength = sampKey.size();
    }
  }

  if (bestMatch) {
    return bestMatch;
  }

  // [Luminous Arc 3: Eyes]: External banks zero the PCMD position and copy the other audible WAVI descriptor fields
  // from the owning waveform bank.
  // [Professor Layton and the Diabolical Box]: External banks use the same descriptor relationship.
  // [Rekishi Taisen: Gettenka - Tenkaichi Battle Royale]: External banks use the same descriptor relationship.
  // Matching those fields at the same sparse slot identifies shared banks independently of names such as 00_HAKEI.
  size_t bestDescriptorMatches = 0;
  for (PSDSESampColl* sampColl : PSDSEScanner::g_loadedSampColls) {
    if (sampColl->m_header.version != instrSet->m_header.version || sampColl->sampleCount() == 0) {
      continue;
    }

    size_t describedSlots = 0;
    size_t descriptorMatches = 0;
    for (size_t slot = 0; slot < instrSet->m_header.nbwavislots; ++slot) {
      const auto instrDescriptor = readWaviDescriptor(instrSet->rawFile(), instrSet->m_header, slot);
      if (!instrDescriptor) {
        continue;
      }
      ++describedSlots;

      const auto sampleDescriptor = readWaviDescriptor(sampColl->rawFile(), sampColl->m_header, slot);
      if (sampleDescriptor && *sampleDescriptor == *instrDescriptor) {
        ++descriptorMatches;
      }
    }

    if (describedSlots != 0 && descriptorMatches == describedSlots && descriptorMatches > bestDescriptorMatches) {
      bestMatch = sampColl;
      bestDescriptorMatches = descriptorMatches;
    }
  }
  if (bestMatch) {
    return bestMatch;
  }

  // A number of DSE titles use unrelated internal names for their sole common
  // sample bank. That case is unambiguous; choosing among several banks is not.
  return PSDSEScanner::g_loadedSampColls.size() == 1 ? PSDSEScanner::g_loadedSampColls.front() : nullptr;
}

}  // namespace

void PSDSEScanner::onInstrSetClose(PSDSEInstrSet* instrSet) {
  std::erase(g_loadedInstrSets, instrSet);
}

void PSDSEScanner::onSampCollClose(PSDSESampColl* sampColl) {
  std::erase(g_loadedSampColls, sampColl);

  // If this SampColl is being deleted, null out references in any InstrSets using it
  for (auto* instrSet : g_loadedInstrSets) {
    if (instrSet->sampColl() == sampColl) {
      instrSet->clearSampColl();
    }
  }
}

void PSDSEScanner::onInstrSetClose(PSDSEPS2InstrSet* instrSet) {
  std::erase(g_loadedPS2InstrSets, instrSet);
}

void PSDSEScanner::onSampCollClose(PSDSEPS2SampColl* sampColl) {
  std::erase(g_loadedPS2SampColls, sampColl);
  for (auto* instrSet : g_loadedPS2InstrSets) {
    if (instrSet->sampColl() == sampColl) {
      instrSet->clearSampColl();
    }
  }
}

void PSDSEScanner::scan(RawFile* file, void* offset) {
  uint32_t nFileLength = file->size();
  const auto* fileData = reinterpret_cast<const uint8_t*>(file->data());
  std::vector<PSDSESeq*> sequences;
  std::vector<PSDSEPS2Seq*> ps2Sequences;

  for (uint32_t i = 0; i + 4 <= nFileLength; i++) {
    // Every supported DSE/SSD object begins with ASCII S/s. Rejecting all
    // other offsets before constructing the magic avoids four virtual reads
    // per byte while scanning large ROM and archive payloads.
    if ((fileData[i] | 0x20) != 's') {
      continue;
    }
    const uint32_t word = (static_cast<uint32_t>(fileData[i]) << 24) | (static_cast<uint32_t>(fileData[i + 1]) << 16) |
                          (static_cast<uint32_t>(fileData[i + 2]) << 8) | fileData[i + 3];

    if (word == PSDSEPS2::kSedsMagic) {
      PSDSEPS2::EffectSetHeader header;
      if (header.read(file, i)) {
        for (const auto& effect : header.effects) {
          if (auto* seq = pRoot->loadVGMFileWithMatcher<PSDSEPS2Seq>(false, file, effect)) {
            ps2Sequences.push_back(seq);
          }
        }
        i += header.fileLength - 1;
        continue;
      }
    }

    if (word == PSDSEPS2::kSmdmMagic) {
      PSDSEPS2::SequenceHeader header;
      if (header.read(file, i)) {
        if (auto* seq = pRoot->loadVGMFileWithMatcher<PSDSEPS2Seq>(false, file, header)) {
          ps2Sequences.push_back(seq);
        }
        i += header.fileLength - 1;
        continue;
      }
    }

    if (word == PSDSEPS2::kSwdmMagic) {
      PSDSEPS2::BankHeader header;
      if (header.read(file, i)) {
        PSDSEPS2SampColl* sampColl = nullptr;
        if (header.waveCount != 0 && header.sampleDataSize != 0) {
          sampColl = pRoot->loadVGMFileWithMatcher<PSDSEPS2SampColl>(false, file, header);
          if (sampColl) {
            g_loadedPS2SampColls.push_back(sampColl);
          }
        }

        if (header.programCount != 0 || (header.waveCount != 0 && sampColl != nullptr)) {
          auto* instrSet = pRoot->loadVGMFileWithMatcher<PSDSEPS2InstrSet>(false, file, header, sampColl);
          if (instrSet) {
            g_loadedPS2InstrSets.push_back(instrSet);
          }
        }
        i += header.fileLength - 1;
        continue;
      }
    }

    const PSDSE::MagicInfo magic = PSDSE::magicInfo(word);
    if (magic.kind == PSDSE::FileKind::Bank) {
      SWDLHeader header;
      if (header.read(file, i)) {
        PSDSESampColl* sampColl = nullptr;
        if (header.waviOffset != 0 && !header.hasExternalPcmd) {
          sampColl =
              pRoot->loadVGMFileWithMatcher<PSDSESampColl>(false, PSDSEFormat::name, file, header, header.intName);
          if (sampColl) {
            g_loadedSampColls.push_back(sampColl);
          }
        } else if (header.hasExternalPcmd) {
          L_INFO("PSDSE: {} references external sample bank", header.intName);
        }

        if (header.prgiOffset != 0) {
          auto* instrSet = pRoot->loadVGMFileWithMatcher<PSDSEInstrSet>(false, file, header);
          if (instrSet) {
            if (sampColl) {
              instrSet->attachSampColl(sampColl);
            } else if (PSDSESampColl* externalSampColl = findSampCollForInstrSet(instrSet)) {
              instrSet->attachSampColl(externalSampColl);
            } else if (header.hasExternalPcmd) {
              L_WARN("PSDSE: Could not identify the external sample bank for '{}'", header.intName);
            }
            g_loadedInstrSets.push_back(instrSet);
          }
        }
        i += header.fileLength - 1;
      }
    } else if (magic.kind == PSDSE::FileKind::Sequence) {
      if (i + 0x40 > nFileLength) {
        continue;
      }
      const uint32_t seqLength = PSDSE::readU32(file, i + 0x08, magic.endianness);
      if (seqLength < 0x40 || seqLength > nFileLength - i) {
        continue;
      }
      PSDSESeq* seq = pRoot->loadVGMFileWithMatcher<PSDSESeq>(false, file, i);
      if (seq) {
        sequences.push_back(seq);
      }
      i += seqLength - 1;
    }
  }

  for (PSDSEPS2Seq* seq : ps2Sequences) {
    auto coll = std::make_unique<VGMColl>(seq->name());
    coll->attachSeq(seq);
    std::set<VGMSampColl*> attachedSampleColls;
    std::set<uint16_t> attachedBankIds;
    size_t attachedBanks = 0;

    for (uint16_t bankId : seq->referencedBanks()) {
      std::vector<PSDSEPS2InstrSet*> candidates;
      std::ranges::copy_if(g_loadedPS2InstrSets, std::back_inserter(candidates),
                           [bankId](const PSDSEPS2InstrSet* instrSet) { return instrSet->m_header.bankId == bankId; });
      std::vector<PSDSEPS2InstrSet*> sameFile;
      std::ranges::copy_if(candidates, std::back_inserter(sameFile),
                           [seq](const PSDSEPS2InstrSet* instrSet) { return instrSet->rawFile() == seq->rawFile(); });
      if (!sameFile.empty()) {
        candidates = std::move(sameFile);
      }
      if (PSDSEPS2::isV2(seq->m_header.version) && candidates.size() > 1) {
        PSDSEPS2InstrSet* nearestPreceding = nullptr;
        for (PSDSEPS2InstrSet* instrSet : candidates) {
          const uint64_t bankEnd = static_cast<uint64_t>(instrSet->offset()) + instrSet->length();
          if (bankEnd <= seq->offset() && (!nearestPreceding || instrSet->offset() > nearestPreceding->offset())) {
            nearestPreceding = instrSet;
          }
        }
        if (nearestPreceding) {
          // [Shadow Hearts]: SOUND.PKB stores each SWDM immediately before the SMDM files that use it. Later banks
          // reuse numeric IDs, so the closest preceding bank is the active bank at that sequence position.
          candidates = {nearestPreceding};
        }
      }
      if (candidates.size() > 1) {
        std::vector<PSDSEPS2InstrSet*> sameName;
        std::ranges::copy_if(candidates, std::back_inserter(sameName), [seq](const PSDSEPS2InstrSet* instrSet) {
          return instrSet->m_header.internalName == seq->m_header.internalName;
        });
        if (!sameName.empty()) {
          candidates = std::move(sameName);
        }
      }

      if (candidates.size() != 1) {
        if (!candidates.empty()) {
          L_WARN("PSDSE PS2: {} candidate instrument banks match ID {:#06x} for '{}'", candidates.size(), bankId,
                 seq->name());
        }
        continue;
      }
      PSDSEPS2InstrSet* instrSet = candidates.front();
      coll->attachInstrSet(instrSet);
      attachedBankIds.insert(bankId);
      ++attachedBanks;
      if (instrSet->sampColl() && attachedSampleColls.insert(instrSet->sampColl()).second) {
        coll->attachSampColl(instrSet->sampColl());
      }

      continue;
    }

    for (uint16_t bankId : seq->referencedBanks()) {
      if (attachedBankIds.contains(bankId)) {
        continue;
      }

      std::vector<PSDSEPS2SampColl*> candidates;
      std::ranges::copy_if(g_loadedPS2SampColls, std::back_inserter(candidates),
                           [bankId](const PSDSEPS2SampColl* sampColl) { return sampColl->m_header.bankId == bankId; });
      std::vector<PSDSEPS2SampColl*> sameFile;
      std::ranges::copy_if(candidates, std::back_inserter(sameFile),
                           [seq](const PSDSEPS2SampColl* sampColl) { return sampColl->rawFile() == seq->rawFile(); });
      if (!sameFile.empty()) {
        candidates = std::move(sameFile);
      }
      if (PSDSEPS2::isV2(seq->m_header.version) && candidates.size() > 1) {
        PSDSEPS2SampColl* nearestPreceding = nullptr;
        for (PSDSEPS2SampColl* sampColl : candidates) {
          const uint64_t bankEnd = static_cast<uint64_t>(sampColl->offset()) + sampColl->length();
          if (bankEnd <= seq->offset() && (!nearestPreceding || sampColl->offset() > nearestPreceding->offset())) {
            nearestPreceding = sampColl;
          }
        }
        if (nearestPreceding) {
          candidates = {nearestPreceding};
        }
      }

      if (candidates.size() == 1) {
        if (attachedSampleColls.insert(candidates.front()).second) {
          coll->attachSampColl(candidates.front());
        }
        ++attachedBanks;
      } else {
        L_WARN("PSDSE PS2: {} candidate sample banks match ID {:#06x} for '{}'", candidates.size(), bankId,
               seq->name());
      }
    }

    if (attachedBanks == 0) {
      L_WARN("PSDSE PS2: no unambiguous wave bank matched sequence '{}'", seq->name());
    }
    const std::string collName = coll->name();
    if (!pRoot->loadVGMColl(std::move(coll))) {
      L_ERROR("PSDSE PS2: failed to load collection '{}'", collName);
    }
  }

  for (PSDSESeq* seq : sequences) {
    L_INFO("PSDSE: Loaded Sequence '{}'. InstrSets available: {}", seq->name(), g_loadedInstrSets.size());

    auto coll = std::make_unique<VGMColl>(seq->name());
    coll->attachSeq(seq);
    std::set<PSDSEInstrSet*> attachedInstrSets;
    std::set<VGMSampColl*> attachedSampleColls;

    for (const uint16_t bankId : seq->referencedBanks()) {
      if (PSDSEInstrSet* instrSet = findInstrSetForBankId(seq, bankId);
          instrSet != nullptr && attachedInstrSets.insert(instrSet).second) {
        // [Pokemon Fushigi no Dungeon: Ikuzo! Arashi no Boukendan]: BGM_PW_SYS_MENU selects 0x7933 through
        // SsdSeqBankMSB and SsdSeqBankLSB, matching the common-header file ID in B.SWD. The file ID remains stable
        // across archive packing and truncated internal names.
        coll->attachInstrSet(instrSet);
        if (instrSet->sampColl() && attachedSampleColls.insert(instrSet->sampColl()).second) {
          coll->attachSampColl(instrSet->sampColl());
        }
      }
    }

    if (attachedInstrSets.empty()) {
      if (PSDSEInstrSet* instrSet = findInstrSetForSequence(seq)) {
        coll->attachInstrSet(instrSet);
        if (instrSet->sampColl()) {
          coll->attachSampColl(instrSet->sampColl());
        }
      }
    }

    if (coll->instrSets().empty()) {
      L_WARN("PSDSE: No matching Instrument Set for '{}'. Creating sequence-only collection.", seq->name());
    }

    const std::string collName = coll->name();
    if (!pRoot->loadVGMColl(std::move(coll))) {
      L_ERROR("PSDSE: Failed to load VGMColl '{}'", collName);
    } else {
      L_INFO("PSDSE: Successfully created VGMColl '{}'", collName);
    }
  }
}
