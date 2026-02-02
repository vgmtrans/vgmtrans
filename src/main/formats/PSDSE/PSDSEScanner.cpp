#include "PSDSEScanner.h"

#include "RawFile.h"
#include "PSDSEInstrSet.h"
#include "PSDSESeq.h"
#include "ScannerManager.h"
#include "VGMColl.h"

namespace vgmtrans {
namespace scanners {

ScannerRegistration<PSDSEScanner> s_psdse("PSDSE", {"nds", "swd", "smd"});

}  // namespace scanners
}  // namespace vgmtrans

std::vector<PSDSESampColl *> PSDSEScanner::g_loadedSampColls;
std::vector<PSDSEInstrSet *> PSDSEScanner::g_loadedInstrSets;

PSDSEScanner::PSDSEScanner(Format *format) : VGMScanner(format) {
}

void PSDSEScanner::onInstrSetClose(PSDSEInstrSet *instrSet) {
  std::erase(g_loadedInstrSets, instrSet);
}

void PSDSEScanner::onSampCollClose(PSDSESampColl *sampColl) {
  std::erase(g_loadedSampColls, sampColl);

  // If this SampColl is being deleted, null out references in any InstrSets using it
  for (auto *instrSet : g_loadedInstrSets) {
    if (instrSet->sampColl == sampColl) {
      instrSet->sampColl = nullptr;
    }
  }
}

void PSDSEScanner::scan(RawFile *file, void *offset) {
  uint32_t nFileLength = file->size();

  for (uint32_t i = 0; i + 4 <= nFileLength; i++) {
    uint32_t sig = file->readWordBE(i);
    if (sig == 0x7377646C) {  // swdl
      SWDLHeader header;
      if (header.read(file, i)) {
        PSDSESampColl *sampColl = nullptr;
        if (header.waviOffset != 0 && !header.hasExternalPcmd) {
          sampColl = new PSDSESampColl(PSDSEFormat::name, file, header, header.intName);
          if (sampColl->loadVGMFile(true)) {
            g_loadedSampColls.push_back(sampColl);
          } else {
            delete sampColl;
          }
        } else if (header.hasExternalPcmd) {
          L_INFO("PSDSE: {} references external sample bank", header.intName);
        }

        if (header.prgiOffset != 0) {
          PSDSEInstrSet *instrSet = new PSDSEInstrSet(file, header);
          g_loadedInstrSets.push_back(instrSet);

          if (sampColl) {
            instrSet->sampColl = sampColl;
          } else {
            // Link the first available PSDSE SampColl as a fallback/default
            if (!g_loadedSampColls.empty()) {
              instrSet->sampColl = g_loadedSampColls.back();
            }
          }
          if (!instrSet->loadVGMFile()) {
            delete instrSet;
            // Remove pointer to deleted object
            g_loadedInstrSets.pop_back();
          }
        }
      }
    } else if (sig == 0x736D646C) {  // smdl
      PSDSESeq *seq = new PSDSESeq(file, i);
      if (seq->loadVGMFile()) {
        L_INFO("PSDSE: Loaded Sequence '{}'. InstrSets available: {}", seq->name(),
               g_loadedInstrSets.size());

        VGMColl *coll = new VGMColl(seq->name());
        coll->useSeq(seq);

        if (!g_loadedInstrSets.empty()) {
          PSDSEInstrSet *instrSet = g_loadedInstrSets.back();
          coll->addInstrSet(instrSet);

          if (instrSet->sampColl) {
            coll->addSampColl(instrSet->sampColl);
          }
        } else {
          L_WARN("PSDSE: No Instrument Set available. Creating VGMColl with Sequence only.");
        }

        if (!coll->load()) {
          L_ERROR("PSDSE: Failed to load VGMColl");
          delete coll;
        } else {
          L_INFO("PSDSE: Successfully created VGMColl '{}'", coll->name());
        }
      } else {
        delete seq;
      }
    }
  }
}
