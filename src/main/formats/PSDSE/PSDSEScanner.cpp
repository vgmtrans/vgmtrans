#include "PSDSEScanner.h"

#include "RawFile.h"
#include "PSDSEInstrSet.h"
#include "PSDSESeq.h"
#include "ScannerManager.h"
#include "VGMColl.h"

#include <algorithm>
#include <cctype>
#include <memory>

namespace vgmtrans {
namespace scanners {

ScannerRegistration<PSDSEScanner> s_psdse("PSDSE", {"nds", "srl", "swd", "smd"});

}  // namespace scanners
}  // namespace vgmtrans

std::vector<PSDSESampColl *> PSDSEScanner::g_loadedSampColls;
std::vector<PSDSEInstrSet *> PSDSEScanner::g_loadedInstrSets;

PSDSEScanner::PSDSEScanner(Format *format) : VGMScanner(format) {
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

PSDSEInstrSet *findInstrSetForSequence(const PSDSESeq *seq) {
  const std::string key = collectionKey(seq->name());
  auto match = std::find_if(PSDSEScanner::g_loadedInstrSets.rbegin(),
                            PSDSEScanner::g_loadedInstrSets.rend(),
                            [&key](const PSDSEInstrSet *instrSet) {
                              return collectionKey(instrSet->name()) == key;
                            });
  return match != PSDSEScanner::g_loadedInstrSets.rend() ? *match : nullptr;
}

}  // namespace

void PSDSEScanner::onInstrSetClose(PSDSEInstrSet *instrSet) {
  std::erase(g_loadedInstrSets, instrSet);
}

void PSDSEScanner::onSampCollClose(PSDSESampColl *sampColl) {
  std::erase(g_loadedSampColls, sampColl);

  // If this SampColl is being deleted, null out references in any InstrSets using it
  for (auto *instrSet : g_loadedInstrSets) {
    if (instrSet->sampColl() == sampColl) {
      instrSet->clearSampColl();
    }
  }
}

void PSDSEScanner::scan(RawFile *file, void *offset) {
  uint32_t nFileLength = file->size();
  std::vector<PSDSESeq *> sequences;

  for (uint32_t i = 0; i + 4 <= nFileLength; i++) {
    const PSDSE::MagicInfo magic = PSDSE::magicInfo(file->readWordBE(i));
    if (magic.kind == PSDSE::FileKind::Bank) {
      SWDLHeader header;
      if (header.read(file, i)) {
        PSDSESampColl *sampColl = nullptr;
        if (header.waviOffset != 0 && !header.hasExternalPcmd) {
          sampColl = pRoot->loadVGMFileWithMatcher<PSDSESampColl>(
              false, PSDSEFormat::name, file, header, header.intName);
          if (sampColl) {
            g_loadedSampColls.push_back(sampColl);
          }
        } else if (header.hasExternalPcmd) {
          L_INFO("PSDSE: {} references external sample bank", header.intName);
        }

        if (header.prgiOffset != 0) {
          auto *instrSet =
              pRoot->loadVGMFileWithMatcher<PSDSEInstrSet>(false, file, header);
          if (instrSet) {
            if (sampColl) {
              instrSet->attachSampColl(sampColl);
            } else {
              // Link the first available PSDSE SampColl as a fallback/default
              if (!g_loadedSampColls.empty()) {
                instrSet->attachSampColl(g_loadedSampColls.back());
              }
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
      PSDSESeq *seq = pRoot->loadVGMFileWithMatcher<PSDSESeq>(false, file, i);
      if (seq) {
        sequences.push_back(seq);
      }
      i += seqLength - 1;
    }
  }

  for (PSDSESeq *seq : sequences) {
    L_INFO("PSDSE: Loaded Sequence '{}'. InstrSets available: {}", seq->name(),
           g_loadedInstrSets.size());

    auto coll = std::make_unique<VGMColl>(seq->name());
    coll->attachSeq(seq);

    if (PSDSEInstrSet *instrSet = findInstrSetForSequence(seq)) {
      coll->attachInstrSet(instrSet);
      if (instrSet->sampColl()) {
        coll->attachSampColl(instrSet->sampColl());
      }
    } else {
      L_WARN("PSDSE: No matching Instrument Set for '{}'. Creating sequence-only collection.",
             seq->name());
    }

    const std::string collName = coll->name();
    if (!pRoot->loadVGMColl(std::move(coll))) {
      L_ERROR("PSDSE: Failed to load VGMColl '{}'", collName);
    } else {
      L_INFO("PSDSE: Successfully created VGMColl '{}'", collName);
    }
  }
}
