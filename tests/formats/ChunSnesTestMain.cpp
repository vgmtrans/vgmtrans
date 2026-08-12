/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ChunSnes/ChunSnes.h"
#include "value/formats/ValueFormats.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <variant>

void runChunSnesModuleTests();

namespace {

int scanArchive(const std::filesystem::path& path) {
  using namespace vgmtrans::core;

  Session session;
  vgmtrans::formats::registerValueFormats(session);
  const SourceId source = session.addSourceFromPath(path);
  session.scanSource(source);
  const SessionSnapshot snapshot = session.snapshot();
  unsigned aramSources = 0;
  unsigned sequences = 0;
  unsigned renderFailures = 0;
  for (const SourceFile& item : snapshot.sources()) {
    aramSources += item.derived() && item.name.ends_with(" - ram");
  }
  for (const Asset& asset : snapshot.assets()) {
    const auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
    if (sequence == nullptr || metadata(asset).format != "ChunSnes") {
      continue;
    }
    ++sequences;
    const PerformanceSequence rendered =
        SequenceVm(LoopPolicy::PlayOnce).render(sequence->program, vgmtrans::formats::chun_snes::sequenceDialect());
    renderFailures += rendered.tracks.empty();
  }
  std::cout << "ARAM " << aramSources << ", ChunSnes sequences " << sequences << ", render failures " << renderFailures
            << '\n';
  for (const Diagnostic& diagnostic : snapshot.diagnostics()) {
    std::cerr << "diagnostic: " << diagnostic.message;
    if (diagnostic.range) {
      const auto found = std::ranges::find(snapshot.sources(), diagnostic.range->source, &SourceFile::id);
      if (found != snapshot.sources().end()) {
        std::cerr << " [" << found->name << ']';
      }
    }
    std::cerr << '\n';
  }
  return aramSources != 0 && sequences == aramSources && renderFailures == 0 ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2) {
      return scanArchive(argv[1]);
    }
    runChunSnesModuleTests();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
  return 0;
}
