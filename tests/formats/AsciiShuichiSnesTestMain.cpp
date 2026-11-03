/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueFormatCorpus.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>

void runAsciiShuichiSnesModuleTests();

int main(int argc, char** argv) {
  try {
    if (argc == 2 || argc == 3) {
      return vgmtrans::tests::scanValueFormatArchive(
          argv[1], vgmtrans::tests::ValueFormatCorpus{
                       .format = "AsciiShuichiSnes",
                       .exports =
                           vgmtrans::core::ExportRequest{
                               .kinds = {vgmtrans::core::ExportKind::Midi, vgmtrans::core::ExportKind::SoundFont2},
                               .sequence = {.loopPolicy = vgmtrans::core::LoopPolicy::PlayOnce},
                               .dynamicEnvelopes = vgmtrans::core::DynamicEnvelopePolicy::InstrumentVariants,
                           },
                       .outputDirectory = argc == 3 ? std::optional<std::filesystem::path>{argv[2]} : std::nullopt,
                   });
    }
    runAsciiShuichiSnesModuleTests();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
