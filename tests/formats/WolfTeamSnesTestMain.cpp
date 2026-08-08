/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ValueFormats.h"
#include "value/formats/WolfTeamSnes/WolfTeamSnes.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <exception>
#include <filesystem>
#include <iostream>

void runWolfTeamSnesModuleTests();

namespace {

int scanArchive(const std::filesystem::path& path) {
  using namespace vgmtrans::core;

  Session session;
  vgmtrans::formats::registerValueFormats(session);
  const SourceId source = session.addSourceFromPath(path);
  session.scanSource(source);
  const SessionSnapshot snapshot = session.snapshot();
  unsigned ramSources = 0;
  for (const SourceFile& sourceFile : snapshot.sources()) {
    ramSources += sourceFile.derived() && sourceFile.name.ends_with(" - ram");
  }
  unsigned wolfSequences = 0;
  unsigned renderFailures = 0;
  for (const Asset& asset : snapshot.assets()) {
    const auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
    if (metadata(asset).format != "WolfTeamSnes" || sequence == nullptr) {
      continue;
    }
    ++wolfSequences;
    const PerformanceSequence performance =
        SequenceVm(LoopPolicy::PlayOnce)
            .render(sequence->program, vgmtrans::formats::wolf_team_snes::sequenceDialect());
    renderFailures += performance.tracks.empty() || !performance.diagnostics.empty();
  }
  std::cout << "sources " << snapshot.sources().size() << ", assets " << snapshot.assets().size() << ", collections "
            << snapshot.collections().size() << ", WolfTeamSnes sequences " << wolfSequences << '/' << ramSources
            << ", render failures " << renderFailures << '\n';
  for (const Diagnostic& diagnostic : snapshot.diagnostics()) {
    std::cerr << "diagnostic: " << diagnostic.message << '\n';
  }
  return wolfSequences == ramSources && ramSources != 0 && renderFailures == 0 ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2) {
      return scanArchive(argv[1]);
    }
    runWolfTeamSnesModuleTests();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
  return 0;
}
