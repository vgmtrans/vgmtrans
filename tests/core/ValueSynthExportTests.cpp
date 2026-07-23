/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "SessionSnapshotBuilder.h"

#include "value/export/synth/ModulationScaling.h"
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

  const auto decoded = decodeSample(sample, sourceBytes);
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
  expect(!decodeSample(invalidRange, sourceBytes).has_value(), "BRR decoder should reject invalid source ranges");
}

void ndsImaAdpcmDecoderRejectsInvalidInitialIndex() {
  const Sample sample{
      .name = "adpcm",
      .codec = AudioCodec::NdsImaAdpcm,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 4, .size = 1},
      .sampleRate = 32768,
  };

  const std::vector<u8> validMaxIndex{0x00, 0x00, 0x58, 0x00, 0x00};
  const auto decoded = decodeSample(sample, validMaxIndex);
  expect(decoded.has_value() && decoded->pcm.size() == 3,
         "NDS IMA ADPCM decoder should accept initial predictor index 88");

  const std::vector<u8> invalidIndex{0x00, 0x00, 0x59, 0x00, 0x00};
  expect(!decodeSample(sample, invalidIndex).has_value(),
         "NDS IMA ADPCM decoder should reject initial predictor indexes outside the step table");
}

void envelopePredicateDetectsCanonicalData() {
  expect(!hasExplicitEnvelope(Envelope{}), "empty envelope should not report explicit envelope data");

  const Envelope timed{
      .attackSeconds = 0.25,
  };
  expect(hasExplicitEnvelope(timed), "envelope predicate should detect a specified stage time");

  const Envelope sustain{
      .sustainAmplitude = 0.5,
  };
  expect(hasExplicitEnvelope(sustain), "envelope predicate should detect a specified sustain amplitude");
}

void physicalModulationLowersToLegacySynthControls() {
  constexpr double stepHertz = 1000.0 / 16384.0;
  const auto lowered = lowerSynthModulation(InstrumentModulation{
      .vibrato =
          VibratoSpec{
              .maxDepthCents = 1200.0,
              .rateHertz = {stepHertz, 255.0 * stepHertz},
          },
      .tremolo =
          TremoloSpec{
              .maxDepthDb = 48.4,
              .rateHertz = {2.0 * stepHertz, 510.0 * stepHertz},
              .gainMode = TremoloGainMode::NoBoost,
          },
  });

  expect(lowered.generators.size() == 2 && lowered.generators[0].destination == SynthDestination::VibratoRate &&
             lowered.generators[0].amount == -8479 &&
             lowered.generators[1].destination == SynthDestination::TremoloRate &&
             lowered.generators[1].amount == -7279,
         "physical LFO rates should lower to the legacy synth generator values");
  expect(
      lowered.modulators.size() == 6 && lowered.modulators[0].source == SynthSource::ChannelPressure &&
          lowered.modulators[0].destination == SynthDestination::VibratoDepth && lowered.modulators[0].amount == 0 &&
          lowered.modulators[1].destination == SynthDestination::VibratoDepth && lowered.modulators[1].amount == 1200 &&
          lowered.modulators[2].destination == SynthDestination::VibratoRate && lowered.modulators[2].amount == 9669 &&
          lowered.modulators[3].destination == SynthDestination::TremoloRate && lowered.modulators[3].amount == 9669 &&
          lowered.modulators[4].destination == SynthDestination::TremoloDepth && lowered.modulators[4].amount == 484 &&
          lowered.modulators[5].destination == SynthDestination::VolumeAttenuation &&
          lowered.modulators[5].amount == 484,
      "physical vibrato and no-boost tremolo should preserve the legacy synth modulator records");
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

  expect(encodePcm16Wav(sample) == expected, "WAV exporter should write expected PCM16 RIFF bytes");
}

void soundFontExporterWritesSfbkRiffFile() {
  constexpr double lfoStepHertz = 1000.0 / 16384.0;
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
          .explicitAddress = InstrumentAddress{.bank = 1, .program = 5},
          .name = "Lead",
          .regions = {Region{
              .keyRange = KeyRange{.low = 24, .high = 96},
              .sample = SampleRef{.collection = sampleCollection.metadata.id, .index = 0},
              .unityKey = 58.75,
              .envelope =
                  Envelope{
                      .attackSeconds = 1.0,
                      .decaySeconds = 2.0,
                      .releaseSeconds = 0.25,
                      .sustainAmplitude = 0.5,
                  },
              .pan = 1.0,
          }},
          .modulation =
              InstrumentModulation{
                  .vibrato =
                      VibratoSpec{
                          .maxDepthCents = 120.0,
                          .rateHertz = {lfoStepHertz, 255.0 * lfoStepHertz},
                          .delaySeconds = ModulationRange{1.0, 2.0},
                      },
                  .tremolo =
                      TremoloSpec{
                          .maxDepthDb = 48.4,
                          .rateHertz = {2.0 * lfoStepHertz, 510.0 * lfoStepHertz},
                          .gainMode = TremoloGainMode::NoBoost,
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
  const auto result = buildSoundFont2(
      SynthExportInput{
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
  expect(soundFontInfoChunksHaveEvenDeclaredSizes(result.bytes),
         "SoundFont INFO chunks should declare even sizes for strict readers");
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
  expect(soundFontBagAt(result.bytes, "ibag", 1, 3, 7),
         "SoundFont region zone should start after instrument generators and modulators");
  expect(soundFontBagAt(result.bytes, "ibag", 2, 17, 7),
         "SoundFont terminal bag should include all generators and modulators");
  expect(chunkSize(result.bytes, "imod") == 80, "SoundFont imod chunk should include physical LFO modulators");
  expect(soundFontImodContains(result.bytes, 13, 6, 0),
         "SoundFont export should allow channel pressure to control physical vibrato depth");
  expect(soundFontImodContains(result.bytes, 129, 6, 36),
         "SoundFont export should scale physical vibrato depth from observed MIDI usage");
  expect(soundFontImodContains(result.bytes, 206, 23, 1209),
         "SoundFont export should write default vibrato-delay modulator");
  expect(soundFontImodContains(result.bytes, 203, 22, 914),
         "SoundFont export should scale default tremolo-rate modulator from observed MIDI usage");
  expect(chunkSize(result.bytes, "igen") == 72, "SoundFont igen chunk should include global and region generators");
  expect(chunkSize(result.bytes, "shdr") == 92, "SoundFont shdr chunk should include one sample and terminal record");
  expect(soundFontIgenContainsAmount(result.bytes, 24, -8479),
         "SoundFont export should write physical vibrato frequency");
  expect(soundFontIgenContainsAmount(result.bytes, 23, 0),
         "SoundFont export should write instrument vibrato delay generator");
  expect(soundFontIgenContainsAmount(result.bytes, 22, -7279),
         "SoundFont export should write physical tremolo frequency");
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

  const auto simulatedResult = buildSoundFont2(
      SynthExportInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = samples,
          .modulationConversion = ModulationConversionPolicy::SequenceEventSimulation,
      },
      sources);
  expect(simulatedResult.diagnostics.empty(),
         "SoundFont sequence-event simulation export should not report diagnostics for valid values");
  expect(!soundFontIgenContainsAmount(simulatedResult.bytes, 24, -8479) &&
             !soundFontIgenContainsAmount(simulatedResult.bytes, 23, 0) &&
             !soundFontIgenContainsAmount(simulatedResult.bytes, 22, -7279),
         "SoundFont sequence-event simulation export should suppress synth LFO generators");
  expect(!soundFontImodContains(simulatedResult.bytes, 206, 23, 1209),
         "SoundFont sequence-event simulation export should suppress synth vibrato-delay modulators");
}

void dlsExporterWritesDlsRiffFile() {
  constexpr double lfoStepHertz = 1000.0 / 16384.0;
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
                  .loop = Loop{.enabled = true, .start = 0, .length = 0},
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
          .identity = InstrumentIdentity{.domain = "probe.instrument", .key = 133},
          .name = "Lead",
          .regions = {Region{
              .keyRange = KeyRange{.low = 24, .high = 96},
              .sample = SampleRef{.collection = sampleCollection.metadata.id, .index = 0},
              .unityKey = 58.75,
              .envelope =
                  Envelope{
                      .attackSeconds = 1.0,
                      .decaySeconds = 2.0,
                      .releaseSeconds = 0.25,
                      .sustainAmplitude = 0.5,
                  },
              .pan = 1.0,
          }},
          .modulation =
              InstrumentModulation{
                  .vibrato =
                      VibratoSpec{
                          .maxDepthCents = 120.0,
                          .rateHertz = {lfoStepHertz, 255.0 * lfoStepHertz},
                          .delaySeconds = ModulationRange{1.0, 2.0},
                      },
                  .tremolo =
                      TremoloSpec{
                          .maxDepthDb = 48.4,
                          .rateHertz = {2.0 * lfoStepHertz, 510.0 * lfoStepHertz},
                          .gainMode = TremoloGainMode::NoBoost,
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
  const auto result = buildDls(
      SynthExportInput{
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
  const size_t instrumentHeader = asciiOffset(result.bytes, "insh");
  expect(readLe32(result.bytes, instrumentHeader + 12) == 0x100 && readLe32(result.bytes, instrumentHeader + 16) == 5,
         "DLS export should assign preset addressing from the neutral source identity");
  const size_t sampleMetadata = asciiOffset(result.bytes, "wsmp");
  expect(readLe32(result.bytes, sampleMetadata + 40) == 16,
         "DLS export should extend an enabled zero-length loop to the end of the sample");
  expect(chunkSize(result.bytes, "art2") == 188,
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
  expect(dlsArt2ContainsConnection(result.bytes, 0x0000, 0x0114, -8479 * 65536),
         "DLS export should write physical vibrato frequency");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0000, 0x0115, 0),
         "DLS export should write instrument vibrato delay generator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0000, 0x0104, -7279 * 65536),
         "DLS export should write physical tremolo frequency");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0009, 0x0081, 0x0003, 36 * 65536),
         "DLS export should scale physical vibrato depth from observed MIDI usage");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0008, 0x0104, 914 * 65536),
         "DLS export should scale default tremolo-rate modulator from observed MIDI usage");

  const auto simulatedResult = buildDls(
      SynthExportInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = samples,
          .modulationConversion = ModulationConversionPolicy::SequenceEventSimulation,
      },
      sources);
  expect(simulatedResult.diagnostics.empty(),
         "DLS sequence-event simulation export should not report diagnostics for valid values");
  expect(!dlsArt2ContainsConnection(simulatedResult.bytes, 0x0000, 0x0114, -8479 * 65536) &&
             !dlsArt2ContainsConnection(simulatedResult.bytes, 0x0000, 0x0115, 0) &&
             !dlsArt2ContainsConnection(simulatedResult.bytes, 0x0000, 0x0104, -7279 * 65536),
         "DLS sequence-event simulation export should suppress synth LFO generators");
}

void standaloneSynthExportsKeepNativeModulation() {
  constexpr double lfoStepHertz = 1000.0 / 16384.0;
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});
  SampleCollectionAsset samples{
      .metadata = AssetMetadata{.id = AssetId{2}, .format = "Probe", .name = "Samples"},
      .samples = SampleCollection{.samples = {Sample{
                                      .codec = AudioCodec::SnesBrr,
                                      .encodedData = SourceRange{.source = source, .offset = 0, .size = 9},
                                      .sampleRate = 16000,
                                  }}},
  };
  InstrumentSetAsset instruments{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "Probe", .name = "Instruments"},
      .instruments = {Instrument{
          .regions = {Region{.sample = SampleRef{.collection = samples.metadata.id, .index = 0}}},
          .modulation = InstrumentModulation{.vibrato = VibratoSpec{
                                                 .maxDepthCents = 100.0,
                                                 .rateHertz = {lfoStepHertz, lfoStepHertz},
                                             }},
      }},
  };

  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(instruments);
  builder.assets.emplace_back(samples);
  const SessionSnapshot snapshot = builder.finish();
  SequenceDialectRegistry dialects;
  const Artifact soundFont = exportInstrumentSet(snapshot, sources, instruments.metadata.id,
                                                 SynthExportFormat::SoundFont2, ExportRequest{}, dialects);
  const Artifact dls = exportInstrumentSet(snapshot, sources, instruments.metadata.id,
                                           SynthExportFormat::Dls, ExportRequest{}, dialects);

  expect(soundFontIgenContainsAmount(soundFont.bytes, 24, -8479),
         "standalone SoundFont export should retain native modulation when no MIDI replacement exists");
  expect(dlsArt2ContainsConnection(dls.bytes, 0x0000, 0x0114, -8479 * 65536),
         "standalone DLS export should retain native modulation when no MIDI replacement exists");
}

PreparedCollectionAssets prepareReplacementInstrumentSet(const CollectionPrepareContext& context) {
  const AssetId samples = context.collection.sampleCollections.front();
  return PreparedCollectionAssets{
      .replacementInstrumentSets = {InstrumentSetAsset{
          .metadata = AssetMetadata{.format = "Prepared Probe", .name = "Prepared Bank"},
          .instruments = {Instrument{
              .name = "Prepared Instrument",
              .regions = {Region{.sample = SampleRef{.collection = samples, .index = 0}}},
          }},
      }},
  };
}

ScanResult scanNoSources(const ScanInput&) {
  return {};
}

void collectionPreparationReplacesDurableInstrumentSets() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});
  const SampleCollectionAsset samples{
      .metadata = AssetMetadata{.id = AssetId{2}, .format = "Prepared Probe", .name = "Samples"},
      .samples = SampleCollection{.samples = {Sample{
                                      .name = "Zero",
                                      .codec = AudioCodec::SnesBrr,
                                      .encodedData = SourceRange{.source = source, .offset = 0, .size = 9},
                                      .sampleRate = 16000,
                                  }}},
  };
  const InstrumentSetAsset durable{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "Prepared Probe", .name = "Durable Bank"},
      .instruments = {Instrument{
          .name = "Durable Instrument",
          .regions = {Region{.sample = SampleRef{.collection = samples.metadata.id, .index = 0}}},
      }},
  };

  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(durable);
  builder.assets.emplace_back(samples);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .key = CollectionKey{.resolver = "Prepared Probe", .value = "one"},
      .name = "Prepared Probe",
      .instrumentSets = {durable.metadata.id},
      .sampleCollections = {samples.metadata.id},
  });

  FormatRegistry formats;
  formats.add(FormatModule{
      .name = "Prepared Probe",
      .scan = scanNoSources,
      .prepareCollection = prepareReplacementInstrumentSet,
  });
  formats.seal();
  SequenceDialectRegistry dialects;
  const auto artifacts = exportCollection(builder.finish(), sources, CollectionId{0},
                                          ExportRequest{.kinds = {ExportKind::Dls}}, dialects, &formats);

  expect(artifacts.size() == 1 && !artifacts.front().bytes.empty(),
         "collection preparation replacement fixture should export a DLS");
  const auto& dls = artifacts.front().bytes;
  expect(readLe32(dls, asciiOffset(dls, "colh") + 8) == 1,
         "prepared instrument sets should replace durable sets instead of being appended");
  expect(containsAscii(dls, "Prepared Instrument") && !containsAscii(dls, "Durable Instrument"),
         "collection export should use only the preparer's authoritative instrument view");
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

  test::SessionSnapshotBuilder builder;
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
  const auto sf2MissingSample = buildSoundFont2(
      SynthExportInput{
          .name = "Probe",
          .sampleCollections = missingSamples,
      },
      sources);
  expectDiagnosticRange(sf2MissingSample.diagnostics, "Sample source was not found", missingSampleRange);

  const auto dlsMissingSample = buildDls(
      SynthExportInput{
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
          .explicitAddress = InstrumentAddress{.bank = 0, .program = 0},
          .name = "Lead",
          .regions = {Region{
              .sample = SampleRef{.collection = validSampleCollection.metadata.id, .index = 9},
              .range = regionRange,
          }},
      }},
  };

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&badRegionSet};
  const std::array<const SampleCollectionAsset*, 1> validSamples{&validSampleCollection};
  const auto sf2BadRegion = buildSoundFont2(
      SynthExportInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = validSamples,
      },
      sources);
  expectDiagnosticRange(sf2BadRegion.diagnostics, "Region sample reference was not found", regionRange);

  const auto dlsBadRegion = buildDls(
      SynthExportInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = validSamples,
      },
      sources);
  expectDiagnosticRange(dlsBadRegion.diagnostics, "Region sample reference was not found", regionRange);
}

void collectionPlaybackPreparesOneRenderedMidiAndSoundFontPair() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "playback.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{.id = TrackId{0}, .sourceTrackNumber = 3, .startAddress = Address{0}};
  TrackProgramBuilder trackBuilder(track);
  const std::array<u8, 3> noteBytes{0x90, 0x3c, 0x04};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(trackBuilder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  track.commands.back().annotation = SourceAnnotationId{40};
  addProbeCommand<ProbeEndCommand>(trackBuilder, dialect, Address{3}, probeRange(3, endBytes.size()), endBytes);
  track.commands.back().annotation = SourceAnnotationId{41};

  const SequenceProgramAsset sequence{
      .metadata = AssetMetadata{.id = AssetId{0}, .format = "Probe", .name = "Playback Sequence"},
      .program = SequenceProgram{.dialect = dialect.id, .timebase = dialect.timebase, .tracks = {track}},
  };
  const SampleCollectionAsset samples{
      .metadata = AssetMetadata{.id = AssetId{2}, .format = "Probe", .name = "Playback Samples"},
      .samples = SampleCollection{.samples = {Sample{
                                      .name = "Zero",
                                      .codec = AudioCodec::SnesBrr,
                                      .encodedData = SourceRange{.source = source, .offset = 0, .size = 9},
                                      .sampleRate = 16000,
                                  }}},
  };
  const InstrumentSetAsset instruments{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "Probe", .name = "Playback Instruments"},
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 0, .program = 0},
          .regions = {Region{.sample = SampleRef{.collection = samples.metadata.id, .index = 0}}},
      }},
  };

  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(sequence);
  builder.assets.emplace_back(instruments);
  builder.assets.emplace_back(samples);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Playback",
      .sequence = sequence.metadata.id,
      .instrumentSets = {instruments.metadata.id},
      .sampleCollections = {samples.metadata.id},
  });
  SequenceDialectRegistry dialects;
  dialects.add(dialect);

  const auto playback =
      prepareCollectionPlayback(builder.finish(), sources, CollectionId{0}, PlaybackRequest{}, dialects);
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
      .sequence = sequence.metadata.id,
  });
  const auto missingSynth =
      prepareCollectionPlayback(sequenceOnlyBuilder.finish(), sources, CollectionId{0}, PlaybackRequest{}, dialects);
  expect(!missingSynth.playable() &&
             std::ranges::any_of(missingSynth.diagnostics,
                                 [](const Diagnostic& diagnostic) {
                                   return diagnostic.message == "No decodable samples available for SoundFont2 export";
                                 }),
         "playback preparation should preserve a useful SoundFont failure diagnostic");

  test::SessionSnapshotBuilder synthOnlyBuilder;
  synthOnlyBuilder.assets.emplace_back(instruments);
  synthOnlyBuilder.assets.emplace_back(samples);
  synthOnlyBuilder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Missing Sequence",
      .instrumentSets = {instruments.metadata.id},
      .sampleCollections = {samples.metadata.id},
  });
  const auto missingSequence =
      prepareCollectionPlayback(synthOnlyBuilder.finish(), sources, CollectionId{0}, PlaybackRequest{}, dialects);
  expect(!missingSequence.playable() &&
             std::ranges::any_of(missingSequence.diagnostics,
                                 [](const Diagnostic& diagnostic) {
                                   return diagnostic.message == "Collection does not reference a sequence asset";
                                 }),
         "playback preparation should preserve a useful MIDI failure diagnostic");
}

}  // namespace

void runValueSynthExportTests() {
  snesBrrDecoderProducesPcm();
  ndsImaAdpcmDecoderRejectsInvalidInitialIndex();
  envelopePredicateDetectsCanonicalData();
  physicalModulationLowersToLegacySynthControls();
  wavExporterWritesPcm16RiffFile();
  soundFontExporterWritesSfbkRiffFile();
  dlsExporterWritesDlsRiffFile();
  standaloneSynthExportsKeepNativeModulation();
  collectionPreparationReplacesDurableInstrumentSets();
  exportDiagnosticsPreserveSourceRanges();
  collectionPlaybackPreparesOneRenderedMidiAndSoundFontPair();
}
