/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "ValueFormatCorpus.h"
#include <exception>
#include <iostream>

void runSculptSoftSnesModuleTests();

int main(int argc, char** argv) {
  try {
    if (argc >= 2) {
      return vgmtrans::tests::scanValueFormatArchive(
          argv[1], vgmtrans::tests::ValueFormatCorpus{
                       .format = "SculptSoftSnes",
                       .exports =
                           vgmtrans::core::ExportRequest{
                               .kinds = {vgmtrans::core::ExportKind::Midi, vgmtrans::core::ExportKind::SoundFont2},
                               .sequence = {.loopPolicy = vgmtrans::core::LoopPolicy::PlayOnce, .sequenceLoops = 0}},
                       .outputDirectory = argc == 3 ? std::optional<std::filesystem::path>{argv[2]} : std::nullopt,
                       .requireSoundBank = true});
    }
    runSculptSoftSnesModuleTests();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
