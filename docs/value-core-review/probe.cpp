// Architecture-review probe; deliberately not part of the regression suite.
// Build and run commands are in ../VALUE_CORE_REVIEW.md.
#include "value/export/Export.h"
#include "value/export/CollectionBinding.h"
#include "value/sequence/SequenceVm.h"
#include "tests/core/SessionSnapshotBuilder.h"
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace vgmtrans::core;

static Effects play(const SourceCommand&, std::any&, std::any&, PerformanceEmitter& out, VmApi&) {
  out.instrument(InstrumentIdentity{.domain = "review", .key = 5});
  out.updateEnvelope(Envelope{.attackSeconds = 0.25}, EnvelopeFields::Attack);
  out.note(60, 1, 4);
  return Effects::wait(4);
}
static void save(const char* name, const Artifact& artifact) {
  std::ofstream stream(std::string("/tmp/vgmtrans-value-review/") + name, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(artifact.bytes.data()), artifact.bytes.size());
  std::cout << name << ": bytes=" << artifact.bytes.size() << " diagnostics=" << artifact.diagnostics.size() << '\n';
  for (const auto& d : artifact.diagnostics) std::cout << "  " << d.message << '\n';
}
static void noteAddress(const CollectionWorkspace& w, const char* label) {
  for (const auto& event : w.performance()->tracks.front().events) {
    if (const auto* n = std::get_if<NotePerformanceEvent>(&event)) {
      std::cout << label << ": note override=";
      if (n->instrumentAddress) std::cout << n->instrumentAddress->bank << ':' << n->instrumentAddress->program;
      else std::cout << "none";
      std::cout << " instrument count=" << w.collection.soundBanks().front().instruments.size() << '\n';
    }
  }
}
int main() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "review.pcm"}, std::vector<u8>(64, 0));
  const SourceRange range{source, 0, 64};
  SequenceProgramAsset song{
    .metadata = {.id = AssetId{0}, .format = "Review", .name = "Song", .range = range},
    .program = {.runtime = {.execute = play}, .tracks = {TrackProgram{
      .startAddress = Address{0}, .commands = {SourceCommand{.address = Address{0}, .range = {source, 0, 1},
        .flow = CommandFlow::end(Address{1})}}}}},
  };
  SoundBankAsset bank{
    .metadata = {.id = AssetId{1}, .format = "Review", .name = "Bank", .range = range},
    .instruments = {Instrument{.identity = InstrumentIdentity{.domain = "review", .key = 5}, .name = "Preset 5",
      .regions = {Region{.sample = SampleRef::resolved(AssetId{1}, 0), .envelope = {.attackSeconds = 1.0}}}}},
    .localSamples = {.samples = {Sample{.name = "Silence", .codec = AudioCodec::PcmS16, .encodedData = range, .sampleRate = 22050}}},
  };
  test::SessionSnapshotBuilder builder;
  builder.sources = sources.sourceFiles();
  builder.assets = {song, bank};
  builder.collections = {Collection{.id = CollectionId{0}, .name = "Song", .members = {.sequence = AssetId{0}, .soundBanks = {AssetId{1}}}}};
  const auto snapshot = builder.finish();
  save("direct.mid", exportSequenceMidi(snapshot, sources, AssetId{0}, {}));
  save("standalone.sf2", exportSoundBank(snapshot, sources, AssetId{1}, SynthExportFormat::SoundFont2, {}));
  save("ignore.mid", exportCollection(snapshot, sources, CollectionId{0}, ExportRequest{.kinds = {ExportKind::Midi}, .dynamicEnvelopes = DynamicEnvelopePolicy::Ignore}).front());
  auto pair = exportCollection(snapshot, sources, CollectionId{0}, ExportRequest{.kinds = {ExportKind::Midi, ExportKind::SoundFont2}});
  save("paired.mid", pair[0]); save("paired.sf2", pair[1]);
  auto binding = bindCollection(snapshot, CollectionId{0});
  CollectionWorkspace workspace{std::move(*binding.collection), std::move(binding.diagnostics)};
  workspace.render({}, DynamicEnvelopePolicy::InstrumentVariants); noteAddress(workspace, "first render");
  workspace.render({}, DynamicEnvelopePolicy::Ignore); noteAddress(workspace, "second render (outside documented contract)");
  const auto removed = sources.removeFamily(source);
  save("removed-source.sf2", exportSoundBank(snapshot, sources, AssetId{1}, SynthExportFormat::SoundFont2, {}));
}
