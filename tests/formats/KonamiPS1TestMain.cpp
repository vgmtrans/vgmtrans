/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ValueFormats.h"
#include "value/export/CollectionBinding.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/session/Session.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <variant>

void runKonamiPs1ModuleTests();

int main(int argc, char** argv) {
  try {
    runKonamiPs1ModuleTests();
    if (argc < 2) {
      return 0;
    }
    using namespace vgmtrans::core;
    Session session;
    vgmtrans::formats::registerValueFormats(session);
    session.scanSource(session.addSourceFromPath(std::filesystem::path(argv[1])));
    const SessionSnapshot snapshot = session.snapshot();
    u32 sequences = 0;
    u32 renderFailures = 0;
    u32 midiFailures = 0;
    u32 missingBanks = 0;
    u32 bindingFailures = 0;
    for (const Asset& asset : snapshot.assets()) {
      const auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
      if (sequence == nullptr || sequence->metadata.format != "KonamiPS1") {
        continue;
      }
      ++sequences;
      std::cout << "  " << sequence->metadata.name << " at 0x" << std::hex << sequence->metadata.range.offset
                << std::dec << '\n';
      const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program);
      renderFailures += performance.tracks.empty() || !performance.diagnostics.empty();
      const auto collection = std::ranges::find_if(snapshot.collections(), [&](const Collection& candidate) {
        return candidate.members.sequence == sequence->metadata.id;
      });
      CollectionBindingResult binding;
      std::vector<const SoundBankAsset*> soundBanks;
      if (collection != snapshot.collections().end()) {
        binding = bindCollection(snapshot, collection->id);
        if (binding.collection) {
          for (const auto& bank : binding.collection->soundBanks()) {
            soundBanks.push_back(&bank);
          }
        } else if (!binding.diagnostics.empty()) {
          std::cerr << "  binding: " << binding.diagnostics.front().message << '\n';
        }
      }
      bindingFailures += !binding.collection.has_value();
      const auto midi =
          renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators, soundBanks);
      midiFailures += midi.tracks.empty() || !midi.diagnostics.empty();
      missingBanks += soundBanks.empty();
    }
    std::cout << "KonamiPS1 sequences " << sequences << ", render failures " << renderFailures << ", MIDI failures "
              << midiFailures << ", missing banks " << missingBanks << ", binding failures " << bindingFailures
              << '\n';
    return sequences != 0 && renderFailures == 0 && midiFailures == 0 && missingBanks == 0 && bindingFailures == 0 ? 0
                                                                                                                  : 2;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
