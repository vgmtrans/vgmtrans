/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueFormatCorpus.h"
#include "value/formats/FalcomSnes/FalcomSnes.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>

void runFalcomSnesModuleTests();

int main(int argc, char** argv) {
  try {
    if (argc == 2 || argc == 3) {
      return vgmtrans::tests::scanValueFormatArchive(
          argv[1], vgmtrans::tests::ValueFormatCorpus{
                       .format = "FalcomSnes",
                       .exports =
                           vgmtrans::core::ExportRequest{
                               .kinds = {vgmtrans::core::ExportKind::Midi, vgmtrans::core::ExportKind::SoundFont2},
                               .sequence = {.loopPolicy = vgmtrans::core::LoopPolicy::PlayOnce},
                               // Active-voice ADSR writes remain exact in the
                               // performance model but have no portable
                               // SoundFont/MIDI equivalent.
                               .dynamicEnvelopes = vgmtrans::core::DynamicEnvelopePolicy::Ignore,
                           },
                       .outputDirectory =
                           argc == 3 ? std::optional<std::filesystem::path>{argv[2]} : std::nullopt,
                   });
    }
    runFalcomSnesModuleTests();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
