/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatSnes/HeartBeatSnes.h"
#include "value/formats/ValueFormats.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>

void runHeartBeatSnesModuleTests();

namespace {

int scanArchive(const std::filesystem::path& path,
                const std::optional<std::filesystem::path>& outputDirectory = std::nullopt) {
  using namespace vgmtrans::core;

  Session session;
  vgmtrans::formats::registerValueFormats(session);
  const SourceId source = session.addSourceFromPath(path);
  session.scanSource(source);
  const SessionSnapshot snapshot = session.snapshot();
  unsigned ramSources = 0;
  unsigned sequences = 0;
  unsigned renderFailures = 0;
  unsigned exportFailures = 0;
  for (const SourceFile& file : snapshot.sources()) {
    ramSources += file.derived() && file.name.ends_with(" - ram");
  }
  for (const Asset& asset : snapshot.assets()) {
    const auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
    if (metadata(asset).format != "HeartBeatSnes" || sequence == nullptr) {
      continue;
    }
    ++sequences;
    const PerformanceSequence performance =
        SequenceVm(LoopPolicy::PlayOnce)
            .render(sequence->program, vgmtrans::formats::heartbeat_snes::sequenceDialect());
    renderFailures += performance.tracks.empty() || !performance.diagnostics.empty();
  }
  for (const Collection& collection : snapshot.collections()) {
    const auto artifacts =
        session.exportCollection(collection.id, ExportRequest{
                                                    .kinds = {ExportKind::Midi, ExportKind::SoundFont2},
                                                    .sequence = {.loopPolicy = LoopPolicy::PlayOnce},
                                                    .dynamicEnvelopes = DynamicEnvelopePolicy::InstrumentVariants,
                                                });
    exportFailures += artifacts.size() != 2 || std::ranges::any_of(artifacts, [](const Artifact& artifact) {
                        return artifact.bytes.empty() || !artifact.diagnostics.empty();
                      });
    if (outputDirectory) {
      std::filesystem::create_directories(*outputDirectory);
      for (const Artifact& artifact : artifacts) {
        std::ofstream output(*outputDirectory / artifact.filename, std::ios::binary);
        output.write(reinterpret_cast<const char*>(artifact.bytes.data()),
                     static_cast<std::streamsize>(artifact.bytes.size()));
        exportFailures += !output;
      }
    }
  }
  std::cout << "sources " << snapshot.sources().size() << ", assets " << snapshot.assets().size() << ", collections "
            << snapshot.collections().size() << ", HeartBeatSnes sequences " << sequences << '/' << ramSources
            << ", render failures " << renderFailures << ", MIDI/SF2 export failures " << exportFailures << '\n';
  for (const Diagnostic& diagnostic : snapshot.diagnostics()) {
    std::cerr << "diagnostic: " << diagnostic.message << '\n';
  }
  return sequences == ramSources && ramSources != 0 && renderFailures == 0 && exportFailures == 0 ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2) {
      return scanArchive(argv[1]);
    }
    if (argc == 3) {
      return scanArchive(argv[1], std::filesystem::path(argv[2]));
    }
    runHeartBeatSnesModuleTests();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
