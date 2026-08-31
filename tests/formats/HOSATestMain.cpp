/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueFormatCorpus.h"

#include <exception>
#include <iostream>
#include <string_view>

void hosaSequencePreservesAuditedGrammarAndMixer();
void hosaVibratoUsesExactDriverTables();
void hosaUnterminatedFinalTrackStopsAtZeroPadding();
void hosaTracksMayShareSequenceData();
void hosaSequenceLoopRestoresEveryTrack();
void hosaModuleBuildsDriverAccurateRegions();

int main(int argc, char** argv) {
  try {
    if (argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--render-only")) {
      vgmtrans::tests::ValueFormatCorpus corpus{.format = "HOSA", .requireSoundBank = true};
      if (argc == 2) {
        corpus.exports = vgmtrans::core::ExportRequest{
            .kinds = {vgmtrans::core::ExportKind::Midi, vgmtrans::core::ExportKind::SoundFont2},
            .sequence = {.loopPolicy = vgmtrans::core::LoopPolicy::PlayOnce},
            .dynamicEnvelopes = vgmtrans::core::DynamicEnvelopePolicy::InstrumentVariants,
        };
      }
      return vgmtrans::tests::scanValueFormatArchive(argv[1], corpus);
    }
    hosaSequencePreservesAuditedGrammarAndMixer();
    hosaVibratoUsesExactDriverTables();
    hosaUnterminatedFinalTrackStopsAtZeroPadding();
    hosaTracksMayShareSequenceData();
    hosaSequenceLoopRestoresEveryTrack();
    hosaModuleBuildsDriverAccurateRegions();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
