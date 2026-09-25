/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../TestSupport.h"
#include "DiagnosticTestSupport.h"
#include "SequenceTestSupport.h"
#include "SessionSnapshotBuilder.h"
#include "SynthExportTestSupport.h"

#include "value/export/CollectionBinding.h"
#include "value/export/AssetPreparation.h"
#include "value/export/Export.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/export/synth/SynthExportData.h"
#include "value/session/Session.h"
#include "value/synth/PsxSpu.h"
#include "value/synth/SnesDsp.h"
#include "value/validation/SynthValidation.h"

#include <algorithm>
#include <array>
#include <limits>

using namespace vgmtrans::core;

namespace {

void standaloneSynthExportsKeepNativeModulation() {
  constexpr double lfoStepHertz = 1000.0 / 16384.0;
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});
  SamplePoolAsset samples{
      .metadata = AssetMetadata{.id = AssetId{2}, .format = "Probe", .name = "Samples"},
      .pool = SamplePool{.samples = {Sample{
                             .codec = AudioCodec::SnesBrr,
                             .encodedData = SourceRange{.source = source, .offset = 0, .size = 9},
                             .sampleRate = 16000,
                         }}},
  };
  SoundBankAsset instruments{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "Probe", .name = "Instruments"},
      .instruments = {Instrument{
          .regions = {Region{.sample = SampleRef::resolved(samples.metadata.id, 0)}},
          .modulation = InstrumentModulation{.vibrato =
                                                 VibratoSpec{
                                                     .maxDepthCents = 100.0,
                                                     .rateHertz = {lfoStepHertz, lfoStepHertz},
                                                 }},
      }},
  };

  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(instruments);
  builder.assets.emplace_back(samples);
  const SessionSnapshot snapshot = builder.finish();
  const Artifact soundFont =
      exportSoundBank(snapshot, sources, instruments.metadata.id, SynthExportFormat::SoundFont2, ExportRequest{});
  const Artifact dls =
      exportSoundBank(snapshot, sources, instruments.metadata.id, SynthExportFormat::Dls, ExportRequest{});

  expect(soundFontGeneratorContains(soundFont.bytes, "igen", 24, -8479),
         "standalone SoundFont export should retain native modulation when no MIDI replacement exists");
  expect(dlsArt2ContainsConnection(dls.bytes, 0x0000, 0x0114, -8479 * 65536),
         "standalone DLS export should retain native modulation when no MIDI replacement exists");

  auto unresolved = instruments;
  unresolved.metadata.id = AssetId{3};
  unresolved.instruments.front().regions.front().sample = SampleRef::unbound(0);
  test::SessionSnapshotBuilder unresolvedBuilder;
  unresolvedBuilder.assets.emplace_back(unresolved);
  const auto unresolvedExport = exportSoundBank(unresolvedBuilder.finish(), sources, unresolved.metadata.id,
                                                SynthExportFormat::SoundFont2, ExportRequest{});
  expect(unresolvedExport.bytes.empty(), "an uncollected sound bank should not export unresolved sample references");
  diagnosticWithMessage(unresolvedExport.diagnostics, "Synth region has an unresolved sample reference");

  auto wrongOwner = instruments;
  wrongOwner.metadata.id = AssetId{5};
  wrongOwner.instruments.front().regions.front().sample = SampleRef::resolved(AssetId{6}, 0);
  test::SessionSnapshotBuilder wrongOwnerBuilder;
  wrongOwnerBuilder.assets.emplace_back(wrongOwner);
  wrongOwnerBuilder.assets.emplace_back(MiscAsset{.metadata = AssetMetadata{.id = AssetId{6}}});
  const auto wrongOwnerExport = exportSoundBank(wrongOwnerBuilder.finish(), sources, wrongOwner.metadata.id,
                                                SynthExportFormat::SoundFont2, ExportRequest{});
  expect(wrongOwnerExport.bytes.empty(), "standalone export should reject a sample owner of another asset type");
  diagnosticWithMessage(wrongOwnerExport.diagnostics,
                        "Asset dependency refers to a missing or wrong-type provider");

  auto selfContained = instruments;
  selfContained.metadata.id = AssetId{4};
  selfContained.localSamples = samples.pool;
  auto& selfContainedSample = selfContained.instruments.front().regions.front().sample;
  selfContainedSample = SampleRef::resolved(selfContained.metadata.id, selfContainedSample.index());
  test::SessionSnapshotBuilder selfContainedBuilder;
  selfContainedBuilder.assets.emplace_back(selfContained);
  const auto selfContainedExport = exportSoundBank(selfContainedBuilder.finish(), sources, selfContained.metadata.id,
                                                   SynthExportFormat::SoundFont2, ExportRequest{});
  expect(!selfContainedExport.bytes.empty() && selfContainedExport.diagnostics.empty(),
         "an uncollected self-contained sound bank should export directly");
}

void sampleReferenceValidationEnforcesOwnership() {
  SamplePoolAsset external{
      .metadata = AssetMetadata{.id = AssetId{2}},
      .pool = SamplePool{.samples = {Sample{}}},
  };
  SoundBankAsset bank{
      .metadata = AssetMetadata{.id = AssetId{1}},
      .instruments = {Instrument{.regions = {Region{}}}},
      .localSamples = SamplePool{.samples = {Sample{}}},
  };
  const SoundBankAsset otherBank{.metadata = AssetMetadata{.id = AssetId{3}}};
  const std::array<const SamplePoolAsset*, 1> selected{&external};
  struct ReferenceCase {
    SampleRef sample;
    std::span<const SamplePoolAsset* const> pools;
    bool allowUnbound = false;
    std::string_view error;
  };
  const ReferenceCase cases[] = {
      {SampleRef::resolved(bank.metadata.id, 1), {}, false, "synth.sample-reference.out-of-range"},
      {SampleRef::unbound(0), {}, false, "synth.sample-reference.unresolved"},
      {SampleRef::unbound(0), {}, true, {}},
      {SampleRef::none(), {}, false, {}},
      {SampleRef::resolved(external.metadata.id, 0), selected, false, {}},
      {SampleRef::resolved(external.metadata.id, 1), selected, false, "synth.sample-reference.out-of-range"},
      {SampleRef::resolved(otherBank.metadata.id, 0), {}, false, "synth.sample-reference.external-owner"},
  };

  for (const auto& test : cases) {
    bank.instruments.front().regions.front().sample = test.sample;
    const auto report = validateSampleReferences(bank, test.pools, test.allowUnbound);
    const bool matches =
        test.error.empty() ? report.empty() : !report.empty() && report.diagnostics().front().code == test.error;
    expect(matches, "sample-reference validation should enforce every owner/index state");
  }
}

void collectionSynthExportsCanExportOnlyUsedInstruments() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "usage.pcm"}, {0, 0, 0});

  const SequenceProgramConfig config = probeSequenceConfig();
  TrackProgram track{.startAddress = Address{0}};
  const std::array<u8, 3> defaultNote{0x90, 0x3c, 0x01};
  const std::array<u8, 2> selectLead{0x80, 0x01};
  const std::array<u8, 3> leadNote{0x90, 0x40, 0x01};
  const std::array<u8, 1> end{0xff};
  addProbeCommand(track, Address{0}, probeRange(0, defaultNote.size()), defaultNote);
  addProbeCommand(track, Address{3}, probeRange(3, selectLead.size()), selectLead);
  addProbeCommand(track, Address{5}, probeRange(5, leadNote.size()), leadNote);
  addProbeCommand(track, Address{8}, probeRange(8, end.size()), end);

  const SequenceProgramAsset sequence{
      .metadata = AssetMetadata{.id = AssetId{0}, .format = "Probe", .name = "Usage"},
      .program =
          SequenceProgram{
              .runtime = probeSequenceRuntime(),
              .timebase = config.timebase,
              .behavior = config.behavior,
              .tracks = {track},
          },
  };
  const AssetId samplePoolId{2};
  const auto sample = [&](std::string name, u64 offset) {
    return Sample{
        .name = std::move(name),
        .codec = AudioCodec::PcmS8,
        .encodedData = SourceRange{.source = source, .offset = offset, .size = 1},
        .sampleRate = 16000,
    };
  };
  const SamplePoolAsset samples{
      .metadata = AssetMetadata{.id = samplePoolId, .format = "Probe", .name = "Samples"},
      .pool = SamplePool{.samples =
                             {
                                 sample("Piano Wave", 0),
                                 sample("Lead Wave", 1),
                                 sample("Noise Wave", 2),
                             }},
  };
  const auto instrument = [&](std::string name, u32 program, u32 sampleIndex) {
    return Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = program},
        .name = std::move(name),
        .regions = {Region{.sample = SampleRef::resolved(samplePoolId, sampleIndex)}},
    };
  };
  const SoundBankAsset instruments{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "Probe", .name = "Instruments"},
      .instruments =
          {
              instrument("Piano", 0, 0),
              instrument("Lead", 1, 1),
              instrument("Noise", 2, 2),
          },
  };

  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(sequence);
  builder.assets.emplace_back(instruments);
  builder.assets.emplace_back(samples);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Usage",
      .members =
          {
              .sequence = sequence.metadata.id,
              .soundBanks = {instruments.metadata.id},
              .samplePools = {samples.metadata.id},
          },
  });
  const SessionSnapshot snapshot = builder.finish();

  const auto complete = exportCollection(snapshot, sources, CollectionId{0},
                                         ExportRequest{.kinds = {ExportKind::SoundFont2, ExportKind::Dls}});
  const auto restricted = exportCollection(snapshot, sources, CollectionId{0},
                                           ExportRequest{
                                               .kinds = {ExportKind::SoundFont2, ExportKind::Dls},
                                               .exportOnlyUsedInstruments = true,
                                           });

  expect(complete.size() == 2 && restricted.size() == 2, "collection fixture should export SF2 and DLS pairs");
  expect(chunkSize(complete[0].bytes, "phdr") == 4 * 38 && chunkSize(complete[0].bytes, "shdr") == 4 * 46,
         "unrestricted SF2 export should retain all three instruments and samples");
  expect(chunkSize(restricted[0].bytes, "phdr") == 3 * 38 && chunkSize(restricted[0].bytes, "shdr") == 3 * 46,
         "restricted SF2 export should retain two used instruments and samples plus terminal records");
  expect(readLe32(complete[1].bytes, asciiOffset(complete[1].bytes, "colh") + 8) == 3 &&
             chunkSize(complete[1].bytes, "ptbl") == 20,
         "unrestricted DLS export should retain all three instruments and samples");
  expect(readLe32(restricted[1].bytes, asciiOffset(restricted[1].bytes, "colh") + 8) == 2 &&
             chunkSize(restricted[1].bytes, "ptbl") == 16,
         "restricted DLS export should retain only the two used instruments and samples");
  for (const auto& artifact : restricted) {
    expect(containsAscii(artifact.bytes, "Piano") && containsAscii(artifact.bytes, "Lead") &&
               !containsAscii(artifact.bytes, "Noise"),
           "restricted collection exports should preserve used data order and omit unused data");
  }

  const ExportRequest onlyUsed{.exportOnlyUsedInstruments = true};
  const auto uniqueSoundFont =
      exportSoundBank(snapshot, sources, instruments.metadata.id, SynthExportFormat::SoundFont2, onlyUsed);
  const auto uniqueDls = exportSoundBank(snapshot, sources, instruments.metadata.id, SynthExportFormat::Dls, onlyUsed);
  expect(chunkSize(uniqueSoundFont.bytes, "phdr") == 3 * 38 && chunkSize(uniqueSoundFont.bytes, "shdr") == 3 * 46 &&
             readLe32(uniqueDls.bytes, asciiOffset(uniqueDls.bytes, "colh") + 8) == 2 &&
             chunkSize(uniqueDls.bytes, "ptbl") == 16,
         "instrument-set export should cull data when exactly one collection supplies sequence context");

  auto otherBank = instruments;
  otherBank.metadata.id = AssetId{3};
  otherBank.instruments = {instrument("Other Bank", 7, 2)};
  test::SessionSnapshotBuilder multiBankBuilder;
  multiBankBuilder.assets = {sequence, instruments, samples, otherBank};
  auto multiBankCollection = snapshot.collections().front();
  multiBankCollection.members.soundBanks.push_back(otherBank.metadata.id);
  multiBankBuilder.collections.push_back(std::move(multiBankCollection));
  const auto multiBankSnapshot = multiBankBuilder.finish();
  const auto firstBank = exportSoundBank(multiBankSnapshot, sources, instruments.metadata.id,
                                         SynthExportFormat::SoundFont2, ExportRequest{});
  const auto secondBank = exportSoundBank(multiBankSnapshot, sources, otherBank.metadata.id,
                                          SynthExportFormat::SoundFont2, ExportRequest{});
  expect(containsAscii(firstBank.bytes, "Piano") && !containsAscii(firstBank.bytes, "Other Bank"),
         "direct export of the first bank should exclude the other collection bank");
  expect(containsAscii(secondBank.bytes, "Other Bank"), "direct export should contain the selected second bank");
  expect(!containsAscii(secondBank.bytes, "Piano"),
         "direct export of the second bank should exclude the first collection bank");

  test::SessionSnapshotBuilder ambiguousBuilder;
  auto otherSamples = samples;
  otherSamples.metadata.id = AssetId{3};
  ambiguousBuilder.assets = {sequence, instruments, samples, otherSamples};
  ambiguousBuilder.collections = {
      snapshot.collections().front(),
      Collection{
          .id = CollectionId{1},
          .name = "Other Usage",
          .members =
              {
                  .sequence = sequence.metadata.id,
                  .soundBanks = {instruments.metadata.id},
                  .samplePools = {otherSamples.metadata.id},
              },
      },
  };
  const SessionSnapshot ambiguousSnapshot = ambiguousBuilder.finish();
  const auto ambiguousSoundFont =
      exportSoundBank(ambiguousSnapshot, sources, instruments.metadata.id, SynthExportFormat::SoundFont2, onlyUsed);
  const auto ambiguousDls =
      exportSoundBank(ambiguousSnapshot, sources, instruments.metadata.id, SynthExportFormat::Dls, onlyUsed);
  expect(ambiguousSoundFont.bytes.empty() && ambiguousDls.bytes.empty(),
         "sound-bank export should not choose between collections with different pools and addresses");
  diagnosticWithMessage(ambiguousSoundFont.diagnostics,
                        "Sound bank belongs to multiple collections; export a specific collection instead");
  diagnosticWithMessage(ambiguousDls.diagnostics,
                        "Sound bank belongs to multiple collections; export a specific collection instead");

  const InstrumentIdentity semanticIdentity{.domain = "probe.instrument", .key = 2};
  auto semanticInstruments = instruments;
  semanticInstruments.instruments[2].identity = semanticIdentity;
  PerformanceSequence semanticPerformance{
      .tracks = {PerformanceTrack{
          .events =
              {
                  InstrumentPerformanceEvent{.instrument = semanticIdentity},
                  NotePerformanceEvent{},
              },
      }},
  };
  const std::array<const SoundBankAsset*, 1> semanticSets{&semanticInstruments};
  const std::array<const SamplePoolAsset*, 1> sampleSets{&samples};
  const auto semanticData = prepareSynthData(
      SynthExportInput{
          .soundBanks = semanticSets,
          .samplePools = sampleSets,
          .sequenceUsage = &semanticPerformance,
      },
      sources);
  expect(semanticData.instruments.size() == 1 && semanticData.instruments[0].instrument->name == "Noise" &&
             semanticData.samples.size() == 1 && semanticData.samples[0].name == "Noise Wave",
         "shared synth preparation should resolve semantic instrument identities and their samples");

  auto logicalBankInstruments = instruments;
  logicalBankInstruments.instruments[1].explicitAddress = InstrumentAddress{.bank = 1, .program = 1};
  PerformanceSequence logicalBankPerformance{
      .tracks = {PerformanceTrack{
          .events =
              {
                  InstrumentPerformanceEvent{.instrument = InstrumentAddress{.bank = 1, .program = 1}},
                  NotePerformanceEvent{},
              },
      }},
  };
  const std::array<const SoundBankAsset*, 1> logicalBankSets{&logicalBankInstruments};
  const auto logicalBankData = prepareSynthData(
      SynthExportInput{
          .soundBanks = logicalBankSets,
          .samplePools = sampleSets,
          .sequenceUsage = &logicalBankPerformance,
      },
      sources);
  expect(logicalBankData.instruments.size() == 1 && logicalBankData.instruments[0].instrument->name == "Lead" &&
             logicalBankData.samples.size() == 1 && logicalBankData.samples[0].name == "Lead Wave",
         "shared synth preparation should use logical instrument banks directly");

  auto exactBankInstruments = logicalBankInstruments;
  exactBankInstruments.instruments[2].explicitAddress = InstrumentAddress{.bank = 1 << 7, .program = 1};
  PerformanceSequence exactBankPerformance{
      .tracks = {PerformanceTrack{
          .events =
              {
                  InstrumentPerformanceEvent{.instrument = InstrumentAddress{.bank = 1 << 7, .program = 1}},
                  NotePerformanceEvent{},
              },
      }},
  };
  const std::array<const SoundBankAsset*, 1> exactBankSets{&exactBankInstruments};
  const auto exactBankData = prepareSynthData(
      SynthExportInput{
          .soundBanks = exactBankSets,
          .samplePools = sampleSets,
          .sequenceUsage = &exactBankPerformance,
      },
      sources);
  expect(exactBankData.instruments.size() == 1 && exactBankData.instruments[0].instrument->name == "Noise" &&
             exactBankData.samples.size() == 1 && exactBankData.samples[0].name == "Noise Wave",
         "a large logical instrument bank should not be reinterpreted as packed MIDI");

  SequenceProgramAsset missingRuntimeSequence = sequence;
  missingRuntimeSequence.program.runtime = {};
  test::SessionSnapshotBuilder missingRuntimeBuilder;
  missingRuntimeBuilder.assets = {missingRuntimeSequence, instruments, samples};
  missingRuntimeBuilder.collections = snapshot.collections();
  const SessionSnapshot missingRuntimeSnapshot = missingRuntimeBuilder.finish();

  const auto baseline = exportCollection(missingRuntimeSnapshot, sources, CollectionId{0},
                                         ExportRequest{
                                             .kinds = {ExportKind::Dls},
                                             .dynamicEnvelopes = DynamicEnvelopePolicy::Ignore,
                                         });
  expect(baseline.size() == 1 && !baseline[0].bytes.empty(),
         "ordinary full-bank synth export should survive a sequence rendering failure");
  diagnosticWithMessage(baseline[0].diagnostics, "Sequence program has no runtime executor");

  for (const auto kind : {ExportKind::SoundFont2, ExportKind::Dls}) {
    const auto failed = exportCollection(missingRuntimeSnapshot, sources, CollectionId{0},
                                         ExportRequest{
                                             .kinds = {kind},
                                             .dynamicEnvelopes = DynamicEnvelopePolicy::Ignore,
                                             .exportOnlyUsedInstruments = true,
                                         });
    expect(failed.size() == 1 && failed[0].bytes.empty(),
           "used-instrument export should stop when sequence rendering fails");
    diagnosticWithMessage(failed[0].diagnostics, "Sequence program has no runtime executor");
    expect(std::ranges::none_of(failed[0].diagnostics,
                                [](const Diagnostic& diagnostic) {
                                  return diagnostic.message.starts_with("No decodable samples available");
                                }),
           "used-instrument export should report the sequence failure instead of a sample error");
  }
}

void bindInstrumentSet(BankPreparationContext& context) {
  const AssetId samples = context.inputs.front().asset;
  auto& instruments = context.bank;
  instruments.instruments = {Instrument{
      .name = "Prepared Instrument",
      .regions = {Region{.sample = SampleRef::resolved(samples, 0)}},
  }};
}

struct PreparedProbeProgramState {
  PreparedProbeProgramState(const SequenceProgram&, bool fail) : shouldFail(fail) {}

  void finalizePerformance(PerformanceSequence& performance) const {
    if (shouldFail) {
      throw std::runtime_error("test finalizer failure");
    }
    for (auto& track : performance.tracks) {
      track.events.emplace_back(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoDepth,
          .pitchDepthSemitones = 1.0,
      });
      track.events.emplace_back(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoRate,
          .context = LfoPerformanceContext{.frequencyHz = 6.0},
      });
      track.events.emplace_back(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::TremoloDepth,
          .amount = 0.5,
      });
    }
  }

  bool shouldFail = false;
};

struct ForeignRuntimeTrackState {};

struct ForeignRuntimePlayback : SequencePlayback<ForeignRuntimeTrackState> {};

SequenceRuntime bindPerformanceRuntime(SequencePreparationContext& context) {
  const bool fail = context.sequence.metadata.name == "Failing Sequence";
  context.warning("Collection binding warning");
  return makeCompiledRuntime<ProbePlayback, PreparedProbeProgramState>(fail);
}

void collectionBindingAppliesToWholeExport() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "performance-finalizer.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});
  const SequenceProgramConfig config = probeSequenceConfig();
  TrackProgram track{.startAddress = Address{0}};
  const std::array<u8, 3> noteBytes{0x90, 0x3c, 0x04};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand(track, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand(track, Address{3}, probeRange(3, endBytes.size()), endBytes);

  SequenceProgramAsset sequence{
      .metadata = AssetMetadata{.id = AssetId{0}, .format = "Performance Finalizer", .name = "Sequence"},
      .program =
          SequenceProgram{
              .runtime = makeCompiledRuntime<ProbePlayback, PreparedProbeProgramState>(false),
              .timebase = config.timebase,
              .behavior = config.behavior,
              .tracks = {track},
          },
  };
  const SoundBankAsset instruments{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "Performance Finalizer", .name = "Durable Bank"},
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 0, .program = 0},
          .name = "Durable Instrument",
          .regions = {Region{.sample = SampleRef::resolved(AssetId{2}, 0)}},
          .modulation = {.tremolo = TremoloSpec{.maxDepthDb = 6.0, .rateHertz = {5.0, 5.0}}},
      }},
  };
  const SamplePoolAsset samples{
      .metadata = AssetMetadata{.id = AssetId{2}, .format = "Performance Finalizer", .name = "Samples"},
      .pool = SamplePool{.samples = {Sample{
                             .name = "Zero",
                             .codec = AudioCodec::SnesBrr,
                             .encodedData = SourceRange{.source = source, .offset = 0, .size = 9},
                             .sampleRate = 16000,
                         }}},
  };
  sequence.prepare = bindPerformanceRuntime;
  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(sequence);
  builder.assets.emplace_back(instruments);
  builder.assets.emplace_back(samples);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Performance Finalizer",
      .members =
          {
              .sequence = sequence.metadata.id,
              .soundBanks = {instruments.metadata.id},
              .samplePools = {samples.metadata.id},
          },
  });
  auto failingSequence = sequence;
  failingSequence.metadata.id = AssetId{3};
  failingSequence.metadata.name = "Failing Sequence";
  builder.assets.emplace_back(failingSequence);
  auto failingCollection = builder.collections.front();
  failingCollection.id = CollectionId{1};
  failingCollection.members.sequence = failingSequence.metadata.id;
  builder.collections.push_back(std::move(failingCollection));

  const SessionSnapshot snapshot = builder.finish();
  const CollectionPlayback playback = prepareCollectionPlayback(snapshot, sources, CollectionId{0}, PlaybackRequest{});
  expect(playback.soundFont.size() >= 12 && containsAscii(playback.soundFont, "Durable Instrument"),
         "a runtime-only collection binding should preserve durable instrument sets for synth export");
  expect(soundFontImodContains(playback.soundFont, 129, 6, 100),
         "collection binding should run before sequence modulation is analyzed");

  const auto synthOnly =
      exportCollection(snapshot, sources, CollectionId{0}, ExportRequest{.kinds = {ExportKind::SoundFont2}});
  expect(synthOnly.size() == 1 && !synthOnly[0].bytes.empty() && soundFontImodContains(synthOnly[0].bytes, 129, 6, 100),
         "synth-only export should render the authoritative bound runtime before applying modulation");

  for (const auto scaling :
       {ModulationScalingPolicy::FullFormatRange, ModulationScalingPolicy::ObservedSequenceRange}) {
    ExportRequest request{.kinds = {ExportKind::Midi, ExportKind::SoundFont2}, .modulationScaling = scaling};
    const auto paired = exportCollection(snapshot, sources, CollectionId{0}, request);
    request.kinds.clear();
    const auto midiOnly = exportCollection(snapshot, sources, CollectionId{0}, request);
    request.kinds = {ExportKind::SoundFont2};
    const auto bankOnly = exportCollection(snapshot, sources, CollectionId{0}, request);
    const bool observed = scaling == ModulationScalingPolicy::ObservedSequenceRange;
    const std::array<u8, 3> tremolo{0xb0, 92, static_cast<u8>(observed ? 127 : 64)};
    expect(paired.size() == 2 && midiOnly.size() == 1 && bankOnly.size() == 1 && !paired[0].bytes.empty() &&
               paired[0].bytes == midiOnly[0].bytes && paired[1].bytes == bankOnly[0].bytes,
           "default MIDI-only, synth-only, and paired export must share the same modulation policy");
    expect(std::search(paired[0].bytes.begin(), paired[0].bytes.end(), tremolo.begin(), tremolo.end()) !=
                   paired[0].bytes.end() &&
               soundFontImodContains(paired[1].bytes, 220, 13, observed ? 30 : 60),
           "observed-range scaling must expand MIDI controls and reduce companion synth depth by the same ratio");
  }

  const ExportRequest forwardRequest{
      .kinds = {ExportKind::Midi, ExportKind::SoundFont2, ExportKind::Dls},
      .modulationConversion = ModulationConversionPolicy::SequenceEventSimulation,
  };
  auto reverseRequest = forwardRequest;
  reverseRequest.kinds = {ExportKind::Dls, ExportKind::SoundFont2, ExportKind::Midi};
  const auto forward = exportCollection(snapshot, sources, CollectionId{0}, forwardRequest);
  const auto reverse = exportCollection(snapshot, sources, CollectionId{0}, reverseRequest);
  expect(forward.size() == 3 && reverse.size() == 3 && forward[0].bytes == reverse[2].bytes &&
             forward[1].bytes == reverse[1].bytes && forward[2].bytes == reverse[0].bytes,
         "collection binding should make multi-artifact export independent of requested output order");

  const auto failed = exportCollection(snapshot, sources, CollectionId{1}, ExportRequest{.kinds = {ExportKind::Midi}});
  expect(failed.size() == 1, "a failing collection performance finalizer should produce one MIDI artifact");
  diagnosticWithMessage(failed.front().diagnostics, "Collection binding warning");
  diagnosticWithMessage(failed.front().diagnostics, "Sequence rendering failed: test finalizer failure");
}

void sequencePreparationValidatesRuntimeReplacement() {
  const SourceRange sequenceRange{.source = SourceId{1}, .offset = 16, .size = 8};
  const auto bind = [&](SequenceRuntime runtime, SequencePreparer prepare) {
    test::SessionSnapshotBuilder builder;
    builder.assets = {SequenceProgramAsset{.metadata = {.id = AssetId{1}, .range = sequenceRange},
                                           .program = {.runtime = std::move(runtime)},
                                           .prepare = std::move(prepare)}};
    builder.collections = {{.id = CollectionId{1}, .members = {.sequence = AssetId{1}}}};
    return bindCollection(builder.finish(), CollectionId{1});
  };
  const auto unchanged = [](SequencePreparationContext&) { return std::nullopt; };
  bool createdOriginalState = false;
  auto original = probeSequenceRuntime();
  original.createProgramState = [factory = original.createProgramState,
                                 &createdOriginalState](const SequenceProgram& program) {
    createdOriginalState = true;
    return factory(program);
  };
  const auto kept = bind(original, unchanged);
  expect(kept.collection && renderCollection(*kept.collection, {}).performance && createdOriginalState,
         "returning nullopt must retain the scanned runtime's state factory");
  expect(bind({}, unchanged).collection.has_value(), "a no-change hook must not require an executable runtime");

  const struct {
    SequenceRuntime original;
    SequenceRuntime replacement;
    std::string_view error;
  } cases[] = {
      {original, {}, "Collection binding produced a replacement sequence runtime with no executor"},
      {{}, original, "Collection binding cannot replace a sequence runtime with no executor"},
      {original, makeCompiledRuntime<ForeignRuntimePlayback>(),
       "Collection binding produced an incompatible sequence runtime family"},
  };
  for (const auto& entry : cases) {
    const auto result = bind(entry.original, [&](SequencePreparationContext&) { return entry.replacement; });
    expect(!result.collection && result.diagnostics.size() == 1 &&
               result.diagnostics.front().severity == Severity::Error &&
               result.diagnostics.front().range == sequenceRange && result.diagnostics.front().message == entry.error,
           "invalid runtime replacement must fail once at the sequence range with the specific cause");
  }
}

void collectionBindingProducesAnImmutableInstrumentView() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});
  const SamplePoolAsset samples{
      .metadata = AssetMetadata{.id = AssetId{2}, .format = "Prepared Probe", .name = "Samples"},
      .pool = SamplePool{.samples = {Sample{
                             .name = "Zero",
                             .codec = AudioCodec::SnesBrr,
                             .encodedData = SourceRange{.source = source, .offset = 0, .size = 9},
                             .sampleRate = 16000,
                         }}},
  };
  const SoundBankAsset durable{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "Prepared Probe", .name = "Durable Bank"},
      .instruments = {Instrument{
          .name = "Durable Instrument",
          .regions = {Region{.sample = SampleRef::resolved(samples.metadata.id, 0)}},
      }},
  };
  const MiscAsset manifest{
      .metadata = AssetMetadata{.id = AssetId{3}, .format = "Prepared Probe", .name = "Manifest"},
      .payload = {0x2a},
      .privateData = AssetPrivateData::make(u32{42}),
  };

  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(durable);
  builder.assets.emplace_back(samples);
  builder.assets.emplace_back(manifest);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Prepared Probe",
      .members =
          {
              .soundBanks = {durable.metadata.id},
              .samplePools = {samples.metadata.id},
              .miscAssets = {manifest.metadata.id},
          },
      .dependencies = {{.owner = durable.metadata.id,
                        .role = DependencyRole::SamplePool,
                        .targets = {{samples.metadata.id, {}}}}},
  });

  const auto snapshotWithBinder = [&](BankPreparer binder) {
    auto copy = builder;
    std::get<SoundBankAsset>(copy.assets.front()).prepare = std::move(binder);
    return copy.finish();
  };
  const SessionSnapshot snapshot = snapshotWithBinder(bindInstrumentSet);
  const auto binding = bindCollection(snapshot, CollectionId{0});
  expect(binding.collection && binding.collection->soundBanks().size() == 1 &&
             binding.collection->soundBanks().front().metadata.id == durable.metadata.id &&
             binding.collection->soundBanks().front().instruments.front().name == "Prepared Instrument" &&
             snapshot.asset<SoundBankAsset>(durable.metadata.id)->instruments.front().name == "Durable Instrument",
         "collection binding should preserve selected asset identity without mutating durable assets");

  expect(snapshot.collection(CollectionId{0})->members.miscAssets == std::vector{manifest.metadata.id} &&
             *snapshot.asset<MiscAsset>(manifest.metadata.id)->privateData.get<u32>() == 42,
         "supplemental assets should remain available for collection inspection");
  auto missingMiscBuilder = builder;
  missingMiscBuilder.collections.front().members.miscAssets = {AssetId{99}};
  const auto missingMisc = bindCollection(missingMiscBuilder.finish(), CollectionId{0});
  expect(!missingMisc.collection, "binding should still validate supplemental asset membership");
  diagnosticWithMessage(missingMisc.diagnostics, "Collection miscellaneous asset was not found");
  const auto artifacts =
      exportCollection(snapshot, sources, CollectionId{0}, ExportRequest{.kinds = {ExportKind::Dls}});

  expect(artifacts.size() == 1 && !artifacts.front().bytes.empty(), "collection binding fixture should export a DLS");
  const auto& dls = artifacts.front().bytes;
  expect(readLe32(dls, asciiOffset(dls, "colh") + 8) == 1,
         "bound instrument data should replace durable data instead of being appended");
  expect(containsAscii(dls, "Prepared Instrument") && !containsAscii(dls, "Durable Instrument"),
         "collection export should use only the binder's authoritative instrument view");

  const auto usedOnly = exportCollection(snapshot, sources, CollectionId{0},
                                         ExportRequest{.kinds = {ExportKind::Dls}, .exportOnlyUsedInstruments = true});
  expect(usedOnly.size() == 1 && usedOnly.front().bytes.empty(),
         "used-instrument export should still require a sequence even for bank-only collections");
  diagnosticWithMessage(usedOnly.front().diagnostics, "Collection does not reference a sequence asset");

  const auto threw = bindCollection(snapshotWithBinder([](BankPreparationContext& context) {
                                      context.bank.instruments.front().name = "Partially Bound";
                                      throw std::runtime_error("expected binding exception");
                                    }),
                                    CollectionId{0});
  expect(!threw.collection,
         "an exception should abort collection binding instead of publishing the callback's partial changes");
  diagnosticWithMessage(threw.diagnostics, "Asset preparation failed: expected binding exception");

  const auto changedIdentity = bindCollection(snapshotWithBinder([](BankPreparationContext& context) {
                                                context.bank.metadata.id = AssetId{99};
                                                context.bank.metadata.format = "Changed";
                                              }),
                                              CollectionId{0});
  expect(!changedIdentity.collection,
         "collection binding should reject changes to selected instrument identity or order");
  diagnosticWithMessage(changedIdentity.diagnostics, "Asset preparation changed sound bank identity, format, or order");

  auto missingPoolBuilder = builder;
  missingPoolBuilder.collections.front().members.samplePools.clear();
  const auto missingPool = bindCollection(missingPoolBuilder.finish(), CollectionId{0});
  expect(!missingPool.collection, "collection binding should reject an external pool outside its membership");
  diagnosticWithMessage(missingPool.diagnostics, "Dependency provider is not a selected collection member");

  const auto unresolved = bindCollection(snapshotWithBinder([](BankPreparationContext& context) {
                                           context.bank.instruments.front().regions.front().sample =
                                               SampleRef::unbound(0);
                                         }),
                                         CollectionId{0});
  expect(!unresolved.collection, "collection binding should reject an unresolved reference left by its binder");
  diagnosticWithMessage(unresolved.diagnostics, "Synth region has an unresolved sample reference");

  const auto malformed = bindCollection(snapshotWithBinder([](BankPreparationContext& context) {
                                          context.bank.instruments.front().regions.front().pan =
                                              std::numeric_limits<double>::quiet_NaN();
                                        }),
                                        CollectionId{0});
  expect(!malformed.collection, "collection binding should revalidate the complete synthesized bank");
  diagnosticWithMessage(malformed.diagnostics, "Synth region pan was outside the 0.0 to 1.0 range");
}

u32 synthOnlySequenceExecutions = 0;

Effects countSynthOnlySequenceExecution(const SourceCommand& command, std::any& programState, std::any& trackState,
                                        PerformanceEmitter& out, VmApi& vm) {
  ++synthOnlySequenceExecutions;
  static const auto execute = probeSequenceRuntime().execute;
  return execute(command, programState, trackState, out, vm);
}

void synthOnlyExportRendersSequencesWithoutOriginalModulation() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "no-modulation.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  const SequenceProgramConfig config = probeSequenceConfig();
  SequenceRuntime runtime = probeSequenceRuntime();
  runtime.execute = countSynthOnlySequenceExecution;
  TrackProgram track{.startAddress = Address{0}};
  const std::array<u8, 3> noteBytes{0x90, 0x3c, 0x04};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand(track, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand(track, Address{3}, probeRange(3, endBytes.size()), endBytes);

  const SequenceProgramAsset sequence{
      .metadata = AssetMetadata{.id = AssetId{0}, .format = "Probe", .name = "No Modulation"},
      .program =
          SequenceProgram{
              .runtime = std::move(runtime),
              .timebase = config.timebase,
              .behavior = config.behavior,
              .tracks = {track},
          },
  };
  const SamplePoolAsset samples{
      .metadata = AssetMetadata{.id = AssetId{2}, .format = "Probe", .name = "Samples"},
      .pool = SamplePool{.samples = {Sample{
                             .codec = AudioCodec::SnesBrr,
                             .encodedData = SourceRange{.source = source, .offset = 0, .size = 9},
                             .sampleRate = 16000,
                         }}},
  };
  const SoundBankAsset instruments{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "Probe", .name = "Instruments"},
      .instruments = {Instrument{
          .regions = {Region{.sample = SampleRef::resolved(samples.metadata.id, 0)}},
      }},
  };

  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(sequence);
  builder.assets.emplace_back(instruments);
  builder.assets.emplace_back(samples);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "No Modulation",
      .members =
          {
              .sequence = sequence.metadata.id,
              .soundBanks = {instruments.metadata.id},
              .samplePools = {samples.metadata.id},
          },
  });
  synthOnlySequenceExecutions = 0;
  const auto artifacts = exportCollection(builder.finish(), sources, CollectionId{0},
                                          ExportRequest{
                                              .kinds = {ExportKind::Dls},
                                              .modulationScaling = ModulationScalingPolicy::ObservedSequenceRange,
                                              .modulationConversion = ModulationConversionPolicy::SynthModulators,
                                          });
  expect(artifacts.size() == 1 && !artifacts[0].bytes.empty(),
         "synth-only export should still write an instrument artifact without sequence modulation");
  expect(synthOnlySequenceExecutions > 0,
         "synth-only export should render once instead of planning from the original command semantics");
}

void exportDiagnosticsPreserveSourceRanges() {
  SourceStore sources;
  const auto validSource = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  const SourceRange missingSampleRange{.source = SourceId{99}, .offset = 0x12, .size = 9};
  SamplePoolAsset missingSamplePool{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Missing Samples",
          },
      .pool =
          SamplePool{
              .samples = {Sample{
                  .name = "Missing",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = missingSampleRange,
              }},
          },
  };

  test::SessionSnapshotBuilder builder;
  builder.assets.push_back(missingSamplePool);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Probe",
      .members = {.samplePools = {missingSamplePool.metadata.id}},
  });
  const SessionSnapshot project = builder.finish();

  const auto wavArtifacts =
      exportCollection(project, sources, CollectionId{0}, ExportRequest{.kinds = {ExportKind::Wav}});
  expect(wavArtifacts.size() == 1, "WAV export should return one artifact for one sample");
  expectDiagnosticRange(wavArtifacts[0].diagnostics, "Sample source was not found", missingSampleRange);
  const auto directPoolWav = exportSamples(project, sources, missingSamplePool.metadata.id);
  expect(directPoolWav.size() == 1, "direct sample-pool export should return one artifact per sample");
  expectDiagnosticRange(directPoolWav[0].diagnostics, "Sample source was not found", missingSampleRange);

  const SoundBankAsset localSampleBank{
      .metadata = AssetMetadata{.id = AssetId{4}, .format = "Probe", .name = "Local Samples"},
      .localSamples =
          SamplePool{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = SourceRange{.source = validSource, .offset = 0, .size = 9},
                  .sampleRate = 32000,
              }},
          },
  };
  test::SessionSnapshotBuilder localSampleBuilder;
  localSampleBuilder.assets.push_back(localSampleBank);
  const auto localBankWav = exportSamples(localSampleBuilder.finish(), sources, localSampleBank.metadata.id);
  expect(localBankWav.size() == 1 && containsAscii(localBankWav[0].bytes, "WAVE"),
         "direct sound-bank sample export should decode its contained sample pool");
  const std::array<const SamplePoolAsset*, 1> missingSamples{&missingSamplePool};
  const auto sf2MissingSample = buildSoundFont2(
      SynthExportInput{
          .name = "Probe",
          .samplePools = missingSamples,
      },
      sources);
  expectDiagnosticRange(sf2MissingSample.diagnostics, "Sample source was not found", missingSampleRange);

  const auto dlsMissingSample = buildDls(
      SynthExportInput{
          .name = "Probe",
          .samplePools = missingSamples,
      },
      sources);
  expectDiagnosticRange(dlsMissingSample.diagnostics, "Sample source was not found", missingSampleRange);

  const SourceRange sampleRange{.source = validSource, .offset = 0, .size = 9};
  const SourceRange regionRange{.source = validSource, .offset = 0x40, .size = 6};
  SamplePoolAsset validSamplePool{
      .metadata =
          AssetMetadata{
              .id = AssetId{3},
              .format = "Probe",
              .name = "Valid Samples",
          },
      .pool =
          SamplePool{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = sampleRange,
                  .sampleRate = 16000,
              }},
          },
  };
  SoundBankAsset badRegionSet{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Bad Region Set",
          },
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 0, .program = 0},
          .name = "Lead",
          .regions = {Region{
              .sample = SampleRef::resolved(validSamplePool.metadata.id, 9),
              .range = regionRange,
          }},
      }},
  };

  const std::array<const SoundBankAsset*, 1> soundBanks{&badRegionSet};
  const std::array<const SamplePoolAsset*, 1> validSamples{&validSamplePool};
  const auto sf2BadRegion = buildSoundFont2(
      SynthExportInput{
          .name = "Probe",
          .soundBanks = soundBanks,
          .samplePools = validSamples,
      },
      sources);
  expectDiagnosticRange(sf2BadRegion.diagnostics, "Region sample reference was not found", regionRange);
  static_cast<void>(
      diagnosticWithMessage(sf2BadRegion.diagnostics, "No playable instruments available for SoundFont2 export"));
  expect(sf2BadRegion.bytes.empty(),
         "SoundFont export should reject sample-only output after every instrument region fails to resolve");

  const auto dlsBadRegion = buildDls(
      SynthExportInput{
          .name = "Probe",
          .soundBanks = soundBanks,
          .samplePools = validSamples,
      },
      sources);
  expectDiagnosticRange(dlsBadRegion.diagnostics, "Region sample reference was not found", regionRange);
  static_cast<void>(
      diagnosticWithMessage(dlsBadRegion.diagnostics, "No playable instruments available for DLS export"));
  expect(dlsBadRegion.bytes.empty(),
         "DLS export should reject sample-only output after every instrument region fails to resolve");
}

void collectionPlaybackPreparesOneRenderedMidiAndSoundFontPair() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "playback.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  const SequenceProgramConfig config = probeSequenceConfig();
  TrackProgram track{.streams = {{.channels = {3}}}, .startAddress = Address{0}};
  const std::array<u8, 3> noteBytes{0x90, 0x3c, 0x04};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand(track, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  track.commands.back().annotation = SourceAnnotationId{40};
  addProbeCommand(track, Address{3}, probeRange(3, endBytes.size()), endBytes);
  track.commands.back().annotation = SourceAnnotationId{41};

  const SequenceProgramAsset sequence{
      .metadata = AssetMetadata{.id = AssetId{0}, .format = "Probe", .name = "Playback Sequence"},
      .program =
          SequenceProgram{
              .runtime = probeSequenceRuntime(),
              .timebase = config.timebase,
              .behavior = config.behavior,
              .tracks = {track},
          },
  };
  const SamplePoolAsset samples{
      .metadata = AssetMetadata{.id = AssetId{2}, .format = "Probe", .name = "Playback Samples"},
      .pool = SamplePool{.samples = {Sample{
                             .name = "Zero",
                             .codec = AudioCodec::SnesBrr,
                             .encodedData = SourceRange{.source = source, .offset = 0, .size = 9},
                             .sampleRate = 16000,
                         }}},
  };
  const SoundBankAsset instruments{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "Probe", .name = "Playback Instruments"},
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 0, .program = 0},
          .regions = {Region{.sample = SampleRef::resolved(samples.metadata.id, 0)}},
      }},
  };

  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(sequence);
  builder.assets.emplace_back(instruments);
  builder.assets.emplace_back(samples);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Playback",
      .members =
          {
              .sequence = sequence.metadata.id,
              .soundBanks = {instruments.metadata.id},
              .samplePools = {samples.metadata.id},
          },
  });
  const auto playback = prepareCollectionPlayback(builder.finish(), sources, CollectionId{0}, PlaybackRequest{});
  expect(playback.playable() && playback.diagnostics.empty(),
         "valid collection playback should prepare clean MIDI and SoundFont data");
  expect(playback.collection == CollectionId{0} && playback.sequence == sequence.metadata.id &&
             playback.title == "Playback",
         "prepared playback should retain stable collection and sequence identity");
  expect(playback.assetDependencies ==
             std::vector<AssetId>{sequence.metadata.id, instruments.metadata.id, samples.metadata.id},
         "prepared playback should identify the assets whose removal invalidates it");
  expect(playback.midi.size() >= 4 && std::string(playback.midi.begin(), playback.midi.begin() + 4) == "MThd",
         "prepared playback should contain a Standard MIDI File");
  expect(playback.soundFont.size() >= 12 &&
             std::string(playback.soundFont.begin() + 8, playback.soundFont.begin() + 12) == "sfbk",
         "prepared playback should contain an SF2 RIFF file");
  expect(playback.performance.sourceSpans ==
             std::vector<SourcePlaybackSpan>{
                 {.annotation = SourceAnnotationId{40}, .beginTick = 0, .endTick = 4},
                 {.annotation = SourceAnnotationId{41}, .beginTick = 4, .endTick = 5},
             },
         "prepared playback should retain the VM source timeline used by inspectors");

  test::SessionSnapshotBuilder sequenceOnlyBuilder;
  sequenceOnlyBuilder.assets.emplace_back(sequence);
  sequenceOnlyBuilder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Missing Synth",
      .members = {.sequence = sequence.metadata.id},
  });
  const auto missingSynth =
      prepareCollectionPlayback(sequenceOnlyBuilder.finish(), sources, CollectionId{0}, PlaybackRequest{});
  expect(!missingSynth.playable() &&
             std::ranges::any_of(missingSynth.diagnostics,
                                 [](const Diagnostic& diagnostic) {
                                   return diagnostic.message == "No decodable samples available for SoundFont2 export";
                                 }),
         "playback preparation should preserve a useful SoundFont failure diagnostic");

  const SoundBankAsset emptyInstruments{
      .metadata = AssetMetadata{.id = AssetId{3}, .format = "Probe", .name = "Empty Instruments"},
  };
  test::SessionSnapshotBuilder sampleOnlySynthBuilder;
  sampleOnlySynthBuilder.assets.emplace_back(sequence);
  sampleOnlySynthBuilder.assets.emplace_back(emptyInstruments);
  sampleOnlySynthBuilder.assets.emplace_back(samples);
  sampleOnlySynthBuilder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Sample-only Synth",
      .members =
          {
              .sequence = sequence.metadata.id,
              .soundBanks = {emptyInstruments.metadata.id},
              .samplePools = {samples.metadata.id},
          },
  });
  const auto sampleOnlySynth =
      prepareCollectionPlayback(sampleOnlySynthBuilder.finish(), sources, CollectionId{0}, PlaybackRequest{});
  expect(!sampleOnlySynth.playable() && sampleOnlySynth.soundFont.empty() &&
             std::ranges::any_of(sampleOnlySynth.diagnostics,
                                 [](const Diagnostic& diagnostic) {
                                   return diagnostic.message ==
                                          "No playable instruments available for SoundFont2 export";
                                 }),
         "playback preparation should reject a sample bank with no playable instruments");

  test::SessionSnapshotBuilder synthOnlyBuilder;
  synthOnlyBuilder.assets.emplace_back(instruments);
  synthOnlyBuilder.assets.emplace_back(samples);
  synthOnlyBuilder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Missing Sequence",
      .members =
          {
              .soundBanks = {instruments.metadata.id},
              .samplePools = {samples.metadata.id},
          },
  });
  const auto missingSequence =
      prepareCollectionPlayback(synthOnlyBuilder.finish(), sources, CollectionId{0}, PlaybackRequest{});
  expect(!missingSequence.playable() && std::ranges::any_of(missingSequence.diagnostics,
                                                            [](const Diagnostic& diagnostic) {
                                                              return diagnostic.message ==
                                                                     "Collection does not reference a sequence asset";
                                                            }),
         "playback preparation should preserve a useful MIDI failure diagnostic");
}

void synthPreparationKeepsSampleIdentityAndPhaseOrdering() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "phases.pcm"}, {0, 128, 232, 3});
  const auto sample = [&](std::string name) {
    return Sample{.name = std::move(name),
                  .codec = AudioCodec::PcmS16,
                  .encodedData = SourceRange{.source = source, .size = 4},
                  .sampleRate = 32000};
  };
  SoundBankAsset bank{.metadata = {.id = AssetId{1}}, .localSamples = {.samples = {sample("Local"), sample("Unused")}}};
  bank.localSamples.samples[1].encodedData.source = SourceId{999};
  const SamplePoolAsset pool{.metadata = {.id = AssetId{2}},
                             .pool = {.samples = {sample("External0"), sample("External1")}}};
  bank.instruments.push_back(
      Instrument{.regions = {
                     Region{.sample = SampleRef::resolved(pool.metadata.id, 0)},
                     Region{.sample = SampleRef::resolved(bank.metadata.id, 0), .invertSamplePhase = true},
                     Region{.sample = SampleRef::resolved(pool.metadata.id, 0), .invertSamplePhase = true},
                     Region{.sample = SampleRef::resolved(pool.metadata.id, 1)},
                     Region{.sample = SampleRef::resolved(pool.metadata.id, 0), .invertSamplePhase = true},
                 }});
  const std::array<const SoundBankAsset*, 1> banks{&bank};
  const std::array<const SamplePoolAsset*, 1> pools{&pool};
  const auto prepared = prepareSynthData(
      SynthExportInput{.soundBanks = banks, .samplePools = pools, .filterSamplesToReferencedInstruments = true},
      sources);
  expect(prepared.diagnostics.empty() && prepared.samples.size() == 4 && prepared.instruments.size() == 1,
         "filtered preparation should skip unused invalid samples and share repeated phase references");
  expect(prepared.samples[0].name == "Local [inverted]" && prepared.samples[1].name == "External0 [inverted]" &&
             prepared.samples[2].name == "External0" && prepared.samples[3].name == "External1",
         "samples should retain pool order with each inverted sample immediately before its retained original");
  expect(prepared.samples[0].decoded.pcm == std::vector<s16>{32767, -1000} &&
             prepared.samples[1].decoded.pcm == prepared.samples[0].decoded.pcm &&
             prepared.samples[2].decoded.pcm == std::vector<s16>{-32768, 1000},
         "inversion must saturate the minimum PCM value and leave a retained original unchanged");
  std::vector<u32> indexes;
  for (const auto& region : prepared.instruments[0].regions) {
    indexes.push_back(region.sampleIndex);
  }
  expect(indexes == std::vector<u32>{2, 0, 1, 3, 1},
         "regions must resolve both sample ownership and phase into the final sample table");
  bank.localSamples.samples[0].loop = {.enabled = true, .start = 1, .length = 1};
  bank.instruments[0].regions[1].sampleStartFrame = 1;
  const auto trimmed = prepareSynthData(
      SynthExportInput{.soundBanks = banks, .samplePools = pools, .filterSamplesToReferencedInstruments = true},
      sources);
  const auto& sustain = trimmed.samples[trimmed.instruments[0].regions[1].sampleIndex].decoded;
  expect(trimmed.diagnostics.empty() && sustain.pcm == std::vector<s16>{-1000} &&
             sustain.loop == Loop{.enabled = true, .start = 0, .length = 1},
         "sample-start trimming must preserve phase and rebase the loop");
}

}  // namespace

void runValueCollectionExportTests() {
  standaloneSynthExportsKeepNativeModulation();
  sampleReferenceValidationEnforcesOwnership();
  collectionSynthExportsCanExportOnlyUsedInstruments();
  collectionBindingAppliesToWholeExport();
  sequencePreparationValidatesRuntimeReplacement();
  collectionBindingProducesAnImmutableInstrumentView();
  synthOnlyExportRendersSequencesWithoutOriginalModulation();
  exportDiagnosticsPreserveSourceRanges();
  collectionPlaybackPreparesOneRenderedMidiAndSoundFontPair();
  synthPreparationKeepsSampleIdentityAndPhaseOrdering();
}
