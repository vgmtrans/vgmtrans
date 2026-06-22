/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"
#include "value/export/synth/SynthExportData.h"

namespace {

void snesBrrDecoderProducesPcm() {
  const std::vector<u8> sourceBytes{0x01, 0, 0, 0, 0, 0, 0, 0, 0};
  const Sample sample{
      .name = "zero",
      .codec = AudioCodec::SnesBrr,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 0, .size = sourceBytes.size()},
      .sampleRate = 32000,
  };

  const auto registry = SampleDecoderRegistry::withDefaultDecoders();
  const auto copy = registry;
  const auto decoded = registry.decode(sample, sourceBytes);
  expect(decoded.has_value(), "BRR decoder should decode a valid sample");
  expect(decoded->sampleRate == 32000, "decoded sample should preserve sample rate");
  expect(decoded->pcm.size() == 16, "one BRR block should decode to 16 samples");
  expect(std::ranges::all_of(decoded->pcm, [](s16 sample) { return sample == 0; }),
         "zero BRR block should decode to silence");

  const Sample invalidRange = Sample{
      .name = "invalid",
      .codec = AudioCodec::SnesBrr,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 8, .size = 9},
  };
  expect(!copy.decode(invalidRange, sourceBytes).has_value(), "BRR decoder should reject invalid source ranges");

  bool threw = false;
  try {
    SampleDecoderRegistry custom;
    custom.add(SampleDecoder{.codec = AudioCodec::SnesBrr});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "sample decoder registry should reject incomplete decoder values");
}

void ndsImaAdpcmDecoderRejectsInvalidInitialIndex() {
  const Sample sample{
      .name = "adpcm",
      .codec = AudioCodec::NdsImaAdpcm,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 4, .size = 1},
      .sampleRate = 32768,
  };

  const auto registry = SampleDecoderRegistry::withDefaultDecoders();
  const std::vector<u8> validMaxIndex{0x00, 0x00, 0x58, 0x00, 0x00};
  const auto decoded = registry.decode(sample, validMaxIndex);
  expect(decoded.has_value() && decoded->pcm.size() == 3,
         "NDS IMA ADPCM decoder should accept initial predictor index 88");

  const std::vector<u8> invalidIndex{0x00, 0x00, 0x59, 0x00, 0x00};
  expect(!registry.decode(sample, invalidIndex).has_value(),
         "NDS IMA ADPCM decoder should reject initial predictor indexes outside the step table");
}

void envelopePredicatesDetectPreciseOnlyData() {
  expect(!hasCoarseEnvelope(Envelope{}), "empty envelope should not report coarse envelope data");
  expect(!hasPreciseEnvelope(Envelope{}), "empty envelope should not report precise envelope data");
  expect(!hasExplicitEnvelope(Envelope{}), "empty envelope should not report explicit envelope data");

  const Envelope coarse{
      .attack = 1,
  };
  expect(hasCoarseEnvelope(coarse), "coarse envelope predicate should detect integer envelope fields");
  expect(hasAnyEnvelopeData(coarse), "any-envelope predicate should detect coarse envelope data");

  const Envelope precise{
      .attackSeconds = 0.25,
  };
  expect(!hasCoarseEnvelope(precise), "precise-only envelope should not report coarse envelope data");
  expect(hasPreciseEnvelope(precise), "precise envelope predicate should detect seconds fields");
  expect(hasExplicitEnvelope(precise), "explicit envelope predicate should detect precise-only envelope data");

  const Envelope preciseSustain{
      .sustainAmplitude = 0.5,
  };
  expect(hasPreciseEnvelope(preciseSustain), "precise envelope predicate should detect sustain amplitude");
  expect(hasExplicitEnvelope(preciseSustain), "explicit envelope predicate should detect precise-only sustain data");
}

void synthEffectiveLoopExpandsEnabledZeroLengthLoop() {
  const DecodedSynthSample sample{
      .collectionId = AssetId{1},
      .decoded =
          DecodedSample{
              .channels = 1,
              .pcm = std::vector<s16>(16),
              .loop = Loop{.enabled = true, .start = 4, .length = 0},
          },
  };

  const Loop sampleLoop = effectiveRegionLoop(Region{}, sample);
  expect(sampleLoop.enabled, "enabled zero-length sample loop should remain enabled");
  expect(sampleLoop.start == 4, "effective sample loop should preserve loop start");
  expect(sampleLoop.length == 12, "effective sample loop should extend to sample end");

  const Region region{
      .loop = Loop{.enabled = true, .start = 2, .length = 0},
  };
  const Loop regionLoop = effectiveRegionLoop(region, sample);
  expect(regionLoop.enabled, "enabled zero-length region loop should remain enabled");
  expect(regionLoop.start == 2, "effective region loop should preserve loop start");
  expect(regionLoop.length == 14, "effective region loop should extend to sample end");
}

void wavExporterWritesPcm16RiffFile() {
  const DecodedSample sample{
      .sampleRate = 8000,
      .channels = 1,
      .pcm = {-32768, 0, 32767},
  };

  const std::vector<u8> expected{
      'R',  'I',  'F',  'F',  0x2a, 0x00, 0x00, 0x00, 'W',  'A',  'V',  'E',  'f',  'm',  't',  ' ',  0x10,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x40, 0x1f, 0x00, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x02, 0x00,
      0x10, 0x00, 'd',  'a',  't',  'a',  0x06, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0xff, 0x7f,
  };

  expect(WavExporter().exportPcm16(sample) == expected, "WAV exporter should write expected PCM16 RIFF bytes");
}

void soundFontExporterWritesSfbkRiffFile() {
  SourceStore sources;
  const auto sourceId = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  SampleCollectionAsset sampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Probe Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = SourceRange{.source = sourceId, .offset = 0, .size = 9},
                  .sampleRate = 16000,
                  .loop = Loop{.enabled = true, .start = 0, .length = 16},
              }},
          },
  };
  InstrumentSetAsset instrumentSet{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Probe Instruments",
          },
      .instruments = {Instrument{
          .bank = 1,
          .program = 5,
          .name = "Lead",
          .regions = {Region{
              .keyRange = KeyRange{.low = 24, .high = 96},
              .sample = SampleRef{.collection = sampleCollection.metadata.id, .index = 0},
              .tuning = Tuning{.cents = 125},
              .envelope =
                  Envelope{
                      .attack = 1'000'000,
                      .decay = 2'000'000,
                      .sustain = 500,
                      .release = 250'000,
                  },
              .pan = 1.0,
          }},
          .generators =
              {
                  SynthGenerator{.destination = SynthDestination::VibratoDepth, .amount = 120},
                  SynthGenerator{.destination = SynthDestination::VibratoRate, .amount = 240},
              },
          .modulators =
              {
                  SynthModulator{
                      .source = SynthSource::NoteOnVelocity,
                      .destination = SynthDestination::VibratoDepth,
                      .amount = 300,
                  },
                  SynthModulator{
                      .source = SynthSource::ChannelPressure,
                      .destination = SynthDestination::VibratoRate,
                      .amount = 0,
                  },
                  SynthModulator{
                      .destination = SynthDestination::TremoloRate,
                      .amount = 180,
                  },
              },
      }},
  };

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&instrumentSet};
  const std::array<const SampleCollectionAsset*, 1> samples{&sampleCollection};
  const MidiModulationUsage midiModulationUsage{
      .vibratoDepth = ObservedValueRange{.observed = true, .min = 4, .max = 38},
      .vibratoRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
      .tremoloDepth = ObservedValueRange{.observed = true, .min = 2, .max = 24},
      .tremoloRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
  };
  const auto result = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = samples,
          .midiModulationUsage = &midiModulationUsage,
          .modulationScaling = ModulationScalingPolicy::ObservedSequenceRange,
      },
      sources);

  expect(result.diagnostics.empty(), "SoundFont export should not report diagnostics for valid values");
  expect(result.bytes.size() > 44, "SoundFont export should produce RIFF bytes");
  expect(std::vector<u8>(result.bytes.begin(), result.bytes.begin() + 4) == std::vector<u8>{'R', 'I', 'F', 'F'},
         "SoundFont export should start with RIFF");
  expect(readLe32(result.bytes, 4) == result.bytes.size() - 8, "SoundFont RIFF size should match file size");
  expect(std::vector<u8>(result.bytes.begin() + 8, result.bytes.begin() + 12) == std::vector<u8>{'s', 'f', 'b', 'k'},
         "SoundFont RIFF type should be sfbk");
  expect(containsAscii(result.bytes, "INFO"), "SoundFont export should include INFO list");
  expect(containsAscii(result.bytes, "sdta"), "SoundFont export should include sample data list");
  expect(containsAscii(result.bytes, "pdta"), "SoundFont export should include preset data list");
  expect(containsAscii(result.bytes, "smpl"), "SoundFont export should include smpl chunk");
  expect(containsAscii(result.bytes, "phdr"), "SoundFont export should include phdr chunk");
  expect(containsAscii(result.bytes, "inst"), "SoundFont export should include inst chunk");
  expect(containsAscii(result.bytes, "shdr"), "SoundFont export should include shdr chunk");
  expect(containsAscii(result.bytes, "Lead"), "SoundFont export should include instrument name");
  expect(containsAscii(result.bytes, "Zero"), "SoundFont export should include sample name");
  expect(chunkSize(result.bytes, "smpl") == 124, "SoundFont smpl chunk should include PCM and SF2 padding samples");
  expect(chunkSize(result.bytes, "pgen") == 12,
         "SoundFont pgen chunk should include reverb, instrument, and terminal generators");
  expect(soundFontBagAt(result.bytes, "pbag", 1, 2, 0),
         "SoundFont terminal preset bag should include both preset generators");
  expect(soundFontPgenContainsAmount(result.bytes, 16, 250),
         "SoundFont export should write default preset reverb send");
  expect(chunkSize(result.bytes, "ibag") == 12, "SoundFont ibag chunk should include a global generator zone");
  expect(soundFontBagAt(result.bytes, "ibag", 0, 0, 0), "SoundFont global zone should start at generator index 0");
  expect(soundFontBagAt(result.bytes, "ibag", 1, 2, 3),
         "SoundFont region zone should start after instrument generators and modulators");
  expect(soundFontBagAt(result.bytes, "ibag", 2, 16, 3),
         "SoundFont terminal bag should include all generators and modulators");
  expect(chunkSize(result.bytes, "imod") == 40, "SoundFont imod chunk should include modulators plus terminal");
  expect(soundFontImodContains(result.bytes, 2, 6, 300),
         "SoundFont export should write explicit velocity-to-vibrato modulator");
  expect(soundFontImodContains(result.bytes, 13, 24, 0),
         "SoundFont export should write explicit channel-pressure-to-vibrato-rate modulator");
  expect(soundFontImodContains(result.bytes, 203, 22, 17),
         "SoundFont export should scale default tremolo-rate modulator from observed MIDI usage");
  expect(chunkSize(result.bytes, "igen") == 68, "SoundFont igen chunk should include global and region generators");
  expect(chunkSize(result.bytes, "shdr") == 92, "SoundFont shdr chunk should include one sample and terminal record");
  expect(soundFontIgenContainsAmount(result.bytes, 6, 120),
         "SoundFont export should write instrument vibrato depth generator");
  expect(soundFontIgenContainsAmount(result.bytes, 24, 240),
         "SoundFont export should write instrument vibrato rate generator");
  expect(soundFontIgenContainsAmount(result.bytes, 34, 0),
         "SoundFont export should write attackVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 35, -32768),
         "SoundFont export should write holdVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 36, 1200),
         "SoundFont export should write decayVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 37, 60),
         "SoundFont export should write sustainVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 38, -2400),
         "SoundFont export should write releaseVolEnv from Region envelope");
}

void dlsExporterWritesDlsRiffFile() {
  SourceStore sources;
  const auto sourceId = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  SampleCollectionAsset sampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Probe Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = SourceRange{.source = sourceId, .offset = 0, .size = 9},
                  .sampleRate = 16000,
                  .loop = Loop{.enabled = true, .start = 0, .length = 16},
              }},
          },
  };
  InstrumentSetAsset instrumentSet{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Probe Instruments",
          },
      .instruments = {Instrument{
          .bank = 1,
          .program = 5,
          .name = "Lead",
          .regions = {Region{
              .keyRange = KeyRange{.low = 24, .high = 96},
              .sample = SampleRef{.collection = sampleCollection.metadata.id, .index = 0},
              .tuning = Tuning{.cents = 125},
              .envelope =
                  Envelope{
                      .attack = 1'000'000,
                      .decay = 2'000'000,
                      .sustain = 500,
                      .release = 250'000,
                  },
              .pan = 1.0,
          }},
          .generators =
              {
                  SynthGenerator{.destination = SynthDestination::VibratoDepth, .amount = 120},
                  SynthGenerator{.destination = SynthDestination::VibratoRate, .amount = 240},
              },
          .modulators =
              {
                  SynthModulator{
                      .source = SynthSource::NoteOnVelocity,
                      .destination = SynthDestination::VibratoDepth,
                      .amount = 300,
                  },
                  SynthModulator{
                      .source = SynthSource::ChannelPressure,
                      .destination = SynthDestination::VibratoRate,
                      .amount = 0,
                  },
                  SynthModulator{
                      .destination = SynthDestination::TremoloRate,
                      .amount = 180,
                  },
              },
      }},
  };

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&instrumentSet};
  const std::array<const SampleCollectionAsset*, 1> samples{&sampleCollection};
  const MidiModulationUsage midiModulationUsage{
      .vibratoDepth = ObservedValueRange{.observed = true, .min = 4, .max = 38},
      .vibratoRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
      .tremoloDepth = ObservedValueRange{.observed = true, .min = 2, .max = 24},
      .tremoloRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
  };
  const auto result = DlsExporter().exportDls(
      DlsInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = samples,
          .midiModulationUsage = &midiModulationUsage,
          .modulationScaling = ModulationScalingPolicy::ObservedSequenceRange,
      },
      sources);

  expect(result.diagnostics.empty(), "DLS export should not report diagnostics for valid values");
  expect(result.bytes.size() > 44, "DLS export should produce RIFF bytes");
  expect(std::vector<u8>(result.bytes.begin(), result.bytes.begin() + 4) == std::vector<u8>{'R', 'I', 'F', 'F'},
         "DLS export should start with RIFF");
  expect(readLe32(result.bytes, 4) == result.bytes.size() - 8, "DLS RIFF size should match file size");
  expect(std::vector<u8>(result.bytes.begin() + 8, result.bytes.begin() + 12) == std::vector<u8>{'D', 'L', 'S', ' '},
         "DLS RIFF type should be DLS");
  expect(containsAscii(result.bytes, "colh"), "DLS export should include collection header");
  expect(containsAscii(result.bytes, "lins"), "DLS export should include instrument list");
  expect(containsAscii(result.bytes, "ptbl"), "DLS export should include pool table");
  expect(containsAscii(result.bytes, "wvpl"), "DLS export should include wave pool");
  expect(containsAscii(result.bytes, "wave"), "DLS export should include wave list");
  expect(containsAscii(result.bytes, "rgnh"), "DLS export should include region header");
  expect(containsAscii(result.bytes, "wsmp"), "DLS export should include sample metadata");
  expect(containsAscii(result.bytes, "wlnk"), "DLS export should include wave link");
  expect(containsAscii(result.bytes, "art2"), "DLS export should include region articulation");
  expect(containsAscii(result.bytes, "Lead"), "DLS export should include instrument name");
  expect(containsAscii(result.bytes, "Zero"), "DLS export should include sample name");
  expect(chunkSize(result.bytes, "colh") == 4, "DLS colh chunk should store one u32 count");
  expect(chunkSize(result.bytes, "ptbl") == 12, "DLS ptbl chunk should include one pool cue");
  expect(chunkSize(result.bytes, "data") == 32, "DLS data chunk should include decoded PCM bytes");
  expect(chunkSize(result.bytes, "art2") == 140,
         "DLS art2 chunk should include pan, envelope, generator, and modulator connections");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0206, 0),
         "DLS export should write EG1 attack time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x020c, std::numeric_limits<s32>::min()),
         "DLS export should write EG1 hold time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0207, 78643200),
         "DLS export should write EG1 decay time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x020a, 61425937),
         "DLS export should write EG1 sustain level from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0209, -157286400),
         "DLS export should write EG1 release time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0009, 0x0003, 7864320),
         "DLS export should write instrument vibrato depth generator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0000, 0x0114, 15728640),
         "DLS export should write instrument vibrato rate generator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0009, 0x0002, 0x0003, 19660800),
         "DLS export should write explicit velocity-to-vibrato modulator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0008, 0x0114, 0),
         "DLS export should write explicit channel-pressure-to-vibrato-rate modulator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0008, 0x0104, 1114112),
         "DLS export should scale default tremolo-rate modulator from observed MIDI usage");
}

void exportDiagnosticsPreserveSourceRanges() {
  SourceStore sources;
  const auto validSource = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  const SourceRange missingSampleRange{.source = SourceId{99}, .offset = 0x12, .size = 9};
  SampleCollectionAsset missingSampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Missing Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Missing",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = missingSampleRange,
              }},
          },
  };

  SessionSnapshotBuilder builder;
  builder.assets.push_back(missingSampleCollection);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Probe",
      .sampleCollections = {missingSampleCollection.metadata.id},
  });
  const SessionSnapshot project = builder.finish();

  SequenceDialectRegistry dialects;
  const auto wavArtifacts =
      exportCollection(project, sources, CollectionId{0}, ExportRequest{.kinds = {ExportKind::Wav}}, dialects);
  expect(wavArtifacts.size() == 1, "WAV export should return one artifact for one sample");
  expectDiagnosticRange(wavArtifacts[0].diagnostics, "Sample source was not found", missingSampleRange);

  const std::array<const SampleCollectionAsset*, 1> missingSamples{&missingSampleCollection};
  const auto sf2MissingSample = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = "Probe",
          .sampleCollections = missingSamples,
      },
      sources);
  expectDiagnosticRange(sf2MissingSample.diagnostics, "Sample source was not found", missingSampleRange);

  const auto dlsMissingSample = DlsExporter().exportDls(
      DlsInput{
          .name = "Probe",
          .sampleCollections = missingSamples,
      },
      sources);
  expectDiagnosticRange(dlsMissingSample.diagnostics, "Sample source was not found", missingSampleRange);

  const SourceRange sampleRange{.source = validSource, .offset = 0, .size = 9};
  const SourceRange regionRange{.source = validSource, .offset = 0x40, .size = 6};
  SampleCollectionAsset validSampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{3},
              .format = "Probe",
              .name = "Valid Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = sampleRange,
                  .sampleRate = 16000,
              }},
          },
  };
  InstrumentSetAsset badRegionSet{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Bad Region Set",
          },
      .instruments = {Instrument{
          .bank = 0,
          .program = 0,
          .name = "Lead",
          .regions = {Region{
              .sample = SampleRef{.collection = validSampleCollection.metadata.id, .index = 9},
              .range = regionRange,
          }},
      }},
  };

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&badRegionSet};
  const std::array<const SampleCollectionAsset*, 1> validSamples{&validSampleCollection};
  const auto sf2BadRegion = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = validSamples,
      },
      sources);
  expectDiagnosticRange(sf2BadRegion.diagnostics, "Region sample reference was not found", regionRange);

  const auto dlsBadRegion = DlsExporter().exportDls(
      DlsInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = validSamples,
      },
      sources);
  expectDiagnosticRange(dlsBadRegion.diagnostics, "Region sample reference was not found", regionRange);
}

}  // namespace

void runValueSynthExportTests() {
  snesBrrDecoderProducesPcm();
  ndsImaAdpcmDecoderRejectsInvalidInitialIndex();
  envelopePredicatesDetectPreciseOnlyData();
  synthEffectiveLoopExpandsEnabledZeroLengthLoop();
  wavExporterWritesPcm16RiffFile();
  soundFontExporterWritesSfbkRiffFile();
  dlsExporterWritesDlsRiffFile();
  exportDiagnosticsPreserveSourceRanges();
}
