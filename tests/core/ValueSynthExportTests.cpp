/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../TestSupport.h"
#include "DiagnosticTestSupport.h"
#include "SynthExportTestSupport.h"

#include "value/export/BinaryWriter.h"
#include "value/export/CollectionBinding.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/export/audio/WavExporter.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/export/synth/SynthExportData.h"
#include "value/synth/PsxSpu.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace vgmtrans::core;

namespace {

void regionResponsesAreSampledAtExport() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "response.pcm"}, {0, 127});
  SoundBankAsset bank{
      .metadata = {.id = AssetId{1}},
      .instruments = {Instrument{
          .regions =
              {
                  Region{.keyRange = {60, 63},
                         .velocityRange = {20, 23},
                         .sample = SampleRef::resolved(AssetId{1}, 0),
                         .response = {.keyDependent = true,
                                      .velocityDependent = true,
                                      .evaluate = [](Region& region, u8 key,
                                                     u8 velocity) { region.attenuationDb = key + velocity; }}},
                  Region{.sample = SampleRef::resolved(AssetId{1}, 0)},
              }}},
      .localSamples = {.samples = {Sample{.codec = AudioCodec::PcmS8,
                                          .encodedData = {.source = source, .size = 2},
                                          .sampleRate = 32000}}},
  };
  std::vector<Diagnostic> diagnostics;
  const u32 step = regionSamplingStep(bank, diagnostics, 5);
  const auto coarse = sampleRegionResponses(bank.instruments[0].regions, step);
  expect(step == 2 && diagnostics.size() == 1 && coarse.size() == 5 && coarse[0].keyRange == KeyRange{60, 61} &&
             coarse[0].velocityRange == VelocityRange{20, 21} && coarse[0].attenuationDb == 80 &&
             coarse[3].attenuationDb == 84 && coarse[4].keyRange == KeyRange{},
         "the shared budget should include static zones and sample dependent axes at cell midpoints");
  diagnostics.clear();
  expect(regionSamplingStep(bank, diagnostics, 1) == 128 && diagnostics.size() == 1,
         "even the coarsest sampling must report when native region count exceeds the budget");

  const std::array<const SoundBankAsset*, 1> banks{&bank};
  const SynthExportInput input{.name = "Response", .soundBanks = banks};
  auto prepared = prepareSynthData(input, sources);
  const auto sf2 = buildSoundFont2(input, sources);
  const auto dls = buildDls(input, sources);
  expect(prepared.diagnostics.empty() && prepared.instruments[0].regions.size() == 17 &&
             bank.instruments[0].regions.size() == 2 && bank.instruments[0].regions[0].response.evaluate,
         "direct export should sample native responses without changing source regions");
  bank.instruments[0].regions = sampleRegionResponses(bank.instruments[0].regions, 1);
  expect(sf2.bytes == buildSoundFont2(input, sources).bytes && dls.bytes == buildDls(input, sources).bytes,
         "both container paths should match explicit response materialization");
  bank.instruments[0].regions.clear();
  const auto moved = std::move(prepared);
  const auto& region = moved.instruments[0].regions[15];
  expect(region.sampleIndex == 0 && region.region.sample.owner() == bank.metadata.id &&
             region.region.attenuationDb == 86 && !region.region.response.evaluate,
         "prepared regions must own their sampled values and preserve sample bindings");
}

void riffChunksKeepLogicalSizesSeparateFromStoragePadding() {
  const RiffChunk odd{"odd ", {1, 2, 3}};
  const RiffChunk even{"even", {4, 5}};
  const auto bytes = makeRiff("TEST", std::array{makeListChunk("nest", std::array{odd, even})});
  // RIFF header plus LIST header/type puts the first child at byte 24.
  const size_t first = 24;
  const size_t second = first + chunkStorageSize(odd);
  expect(readLe32(bytes, first + 4) == 3 && bytes[first + 11] == 0 && second == asciiOffset(bytes, "even") &&
             readLe32(bytes, second + 4) == 2,
         "odd payloads need an uncounted alignment byte before the next chunk");
  expect(bytes.size() == second + chunkStorageSize(even) && readLe32(bytes, 4) == bytes.size() - 8 &&
             readLe32(bytes, 16) == bytes.size() - 20,
         "RIFF and LIST sizes must include their child chunks and padding");
  const auto large = makeRiff("TEST", std::array{RiffChunk{"data", std::vector<u8>(65537, 0x5a)}});
  expect(readLe32(large, 4) == 65550 && readLe32(large, 16) == 65537 && large.size() == 65558 &&
             large[20] == 0x5a && large[large.size() - 2] == 0x5a && large.back() == 0,
         "the final RIFF size must retain its high bytes and count an odd child's alignment byte");
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

void adsrApproximationLowersUnsupportedStages() {
  const Envelope exportableRelease = approximateEnvelopeAsAdsr(Envelope{
      .releaseSeconds = std::numeric_limits<double>::infinity(),
  });
  expect(exportableRelease.releaseSeconds == 150.0,
         "ADSR export should replace an endless release with a finite fallback");

  // Final Fantasy Tactics instrument 41: 00 0D 3E 0C 02 01 07 07.
  const Envelope nativeFftEnvelope =
      psxSpuEnvelope(composePsxAdsr1(0, 0x00, 0x0d, 0x02), composePsxAdsr2(1, 1, 0x3e, 1, 0x0c));
  const Envelope fftEnvelope = approximateEnvelopeAsAdsr(nativeFftEnvelope);
  expect(fftEnvelope.decaySeconds && std::abs(*fftEnvelope.decaySeconds - 17.167896) < 0.000001 &&
             !fftEnvelope.secondDecaySeconds && fftEnvelope.sustainAmplitude == 0.0,
         "ADSR export should combine FFT instrument 41's two rates by the attenuation distance each covers");

  // Dracula X, Picture of the Ghost Ship, track 0 at ARAM $342b:
  // FA 8F 02 DA. The audible first drop lasts 0.88 seconds; matching only the
  // eventual endpoint stretches it to almost four seconds.
  const Envelope nativeDraculaEnvelope = snesDspEnvelope(0x8f, 0x02, 0xda);
  const Envelope draculaEnvelope = approximateEnvelopeAsAdsr(nativeDraculaEnvelope);
  expect(nativeDraculaEnvelope.decaySeconds && nativeDraculaEnvelope.secondDecaySeconds &&
             std::abs(*nativeDraculaEnvelope.decaySeconds - 4.853094) < 0.000001 &&
             std::abs(*nativeDraculaEnvelope.secondDecaySeconds - 25.330971) < 0.000001 &&
             draculaEnvelope.decaySeconds && std::abs(*draculaEnvelope.decaySeconds - 7.473188) < 0.000001 &&
             !draculaEnvelope.secondDecaySeconds && draculaEnvelope.sustainAmplitude == 0.0,
         "a distinct SNES first decay should not be flattened by its quieter sustain-rate tail");

  // Contra III, Neo Kobe Steel Factory, track 2 at ARAM $39cf:
  // FA 70 00 01 decodes to ADSR1 $ab and ADSR2 $02. It reaches the -18 dB
  // knee in 0.33 seconds, so the much slower tail must not dominate the fit.
  const Envelope nativeContraEnvelope = snesDspEnvelope(0xab, 0x02, 0x00);
  const Envelope contraEnvelope = approximateEnvelopeAsAdsr(nativeContraEnvelope);
  expect(nativeContraEnvelope.decaySeconds && nativeContraEnvelope.secondDecaySeconds &&
             std::abs(*nativeContraEnvelope.decaySeconds - 1.819910) < 0.000001 &&
             std::abs(*nativeContraEnvelope.secondDecaySeconds - 25.330971) < 0.000001 && contraEnvelope.decaySeconds &&
             std::abs(*contraEnvelope.decaySeconds - 4.140138) < 0.000001 && !contraEnvelope.secondDecaySeconds &&
             contraEnvelope.sustainAmplitude == 0.0,
         "a 0.33-second SNES first decay should be fitted independently of its long quieter tail");

  // Star Fox, Continue, track 0, instrument 20 at ARAM $3d78: DF 34.
  // Its 58 ms first stage is brief, but the following stage halves perceived
  // loudness every 158 ms and must not be flattened into a 1.45-second decay.
  const Envelope nativeStarFoxEnvelope = snesDspEnvelope(0xdf, 0x34, 0x00);
  const Envelope starFoxEnvelope = approximateEnvelopeAsAdsr(nativeStarFoxEnvelope);
  expect(nativeStarFoxEnvelope.decaySeconds && nativeStarFoxEnvelope.secondDecaySeconds &&
             std::abs(*nativeStarFoxEnvelope.decaySeconds - 0.485319) < 0.000001 &&
             std::abs(*nativeStarFoxEnvelope.secondDecaySeconds - 1.582068) < 0.000001 &&
             starFoxEnvelope.decaySeconds && std::abs(*starFoxEnvelope.decaySeconds - 0.843394) < 0.000001 &&
             !starFoxEnvelope.secondDecaySeconds && starFoxEnvelope.sustainAmplitude == 0.0,
         "a rapid SNES second stage should retain its perceived decay rate after a brief first stage");

  // Super Mario World, Overworld, track 0, instrument 7 at ARAM $5f69: 9E 1F.
  // Its first stage takes 548 ms to reach the -18 dB knee. The fast tail must
  // not make the single-stage approximation rush through that audible fade.
  const Envelope nativeMarioEnvelope = snesDspEnvelope(0x9e, 0x1f, 0x00);
  const Envelope marioEnvelope = approximateEnvelopeAsAdsr(nativeMarioEnvelope);
  expect(nativeMarioEnvelope.decaySeconds && nativeMarioEnvelope.secondDecaySeconds &&
             std::abs(*nativeMarioEnvelope.decaySeconds - 3.033183) < 0.000001 &&
             std::abs(*nativeMarioEnvelope.secondDecaySeconds - 0.016492) < 0.000001 && marioEnvelope.decaySeconds &&
             std::abs(*marioEnvelope.decaySeconds - 2.337143) < 0.000001 && !marioEnvelope.secondDecaySeconds &&
             marioEnvelope.sustainAmplitude == 0.0,
         "a distinct SNES first stage should survive a very rapid second-stage tail");

  const Envelope barelyAudibleTail = approximateEnvelopeAsAdsr(Envelope{
      .decaySeconds = 0.2,
      .secondDecaySeconds = 100.0,
      .sustainAmplitude = 0.000316227766,
  });
  expect(barelyAudibleTail.decaySeconds && *barelyAudibleTail.decaySeconds < 0.3 &&
             !barelyAudibleTail.secondDecaySeconds && barelyAudibleTail.sustainAmplitude == 0.0,
         "a long second decay beginning 70 dB down should not dominate an audible ADSR approximation");

  const Envelope equalRates = approximateEnvelopeAsAdsr(Envelope{
      .decaySeconds = 3.0,
      .secondDecaySeconds = 3.0,
      .sustainAmplitude = 0.25,
  });
  expect(equalRates.decaySeconds && std::abs(*equalRates.decaySeconds - 3.0) < 0.000001 &&
             !equalRates.secondDecaySeconds && equalRates.sustainAmplitude == 0.0,
         "collapsing equal decay rates should preserve their common slope");

  const Envelope endlessSecondDecay = approximateEnvelopeAsAdsr(Envelope{
      .decaySeconds = 2.0,
      .secondDecaySeconds = std::numeric_limits<double>::infinity(),
      .sustainAmplitude = 0.5,
  });
  expect(endlessSecondDecay.decaySeconds == 2.0 && !endlessSecondDecay.secondDecaySeconds &&
             endlessSecondDecay.sustainAmplitude == 0.5,
         "an endless second decay should remain a true sustain");

  const Envelope secondDecayBelowSilence = approximateEnvelopeAsAdsr(Envelope{
      .decaySeconds = 2.0,
      .secondDecaySeconds = 1.0,
      .sustainAmplitude = 0.0,
  });
  expect(secondDecayBelowSilence.decaySeconds == 2.0 && !secondDecayBelowSilence.secondDecaySeconds &&
             secondDecayBelowSilence.sustainAmplitude == 0.0,
         "a second decay that begins at silence should not alter the first rate");
}

void physicalModulationLowersToLegacySynthControls() {
  constexpr double stepHertz = 1000.0 / 16384.0;
  const auto lowered = lowerSynthModulation(InstrumentModulation{
      .vibrato =
          VibratoSpec{
              .maxDepthCents = 1200.0,
              .rateHertz = {stepHertz, 255.0 * stepHertz},
              .waveform = LfoWaveform::SawtoothUp,
          },
      .tremolo =
          TremoloSpec{
              .maxDepthDb = 48.4,
              .rateHertz = {2.0 * stepHertz, 510.0 * stepHertz},
              .waveform = LfoWaveform::Square,
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

  const auto noise = lowerSynthModulation(InstrumentModulation{
      .vibrato =
          VibratoSpec{
              .maxDepthCents = 100.0,
              .rateHertz = {1.0, 1.0},
              .waveform = LfoWaveform::Noise,
          },
  });
  expect(noise.generators.empty() && noise.modulators.empty(),
         "noise modulation should remain in the model when the synth target cannot represent it");
}

void fixedPhysicalLfoValuesNeedNoZeroRangeModulators() {
  const auto lowered = lowerSynthModulation(InstrumentModulation{
      .vibrato =
          VibratoSpec{
              .maxDepthCents = 100.0,
              .rateHertz = {6.0, 6.0},
              .delaySeconds = ModulationRange{0.25, 0.25},
              .depthMode = ModulationDepthMode::Fixed,
          },
  });
  expect(std::ranges::any_of(lowered.generators,
                             [](const SynthGenerator& generator) {
                               return generator.destination == SynthDestination::VibratoDepth &&
                                      generator.amount == 100;
                             }),
         "a hardware-fixed LFO depth should lower to an unconditional synth generator");
  expect(std::ranges::none_of(lowered.modulators,
                              [](const SynthModulator& modulator) {
                                return modulator.destination == SynthDestination::VibratoDepth ||
                                       modulator.destination == SynthDestination::VibratoRate ||
                                       modulator.destination == SynthDestination::VibratoDelay;
                              }),
         "fixed physical depth, rate, and delay should live entirely in synth base generators");
}

void regionModulationExportsAtTheRegionScope() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "region-lfo.pcm"}, {0});
  const SamplePoolAsset samples{
      .metadata = AssetMetadata{.id = AssetId{2}, .format = "Probe", .name = "Samples"},
      .pool = SamplePool{.samples = {Sample{
                             .name = "Zero",
                             .codec = AudioCodec::PcmS8,
                             .encodedData = SourceRange{.source = source, .offset = 0, .size = 1},
                             .sampleRate = 16000,
                         }}},
  };
  const SoundBankAsset instruments{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "Probe", .name = "Instruments"},
      .instruments = {Instrument{
          .name = "Layered LFO",
          .regions =
              {
                  Region{
                      .keyRange = {.low = 0, .high = 63},
                      .sample = SampleRef::resolved(samples.metadata.id, 0),
                      .modulation =
                          InstrumentModulation{
                              .vibrato =
                                  VibratoSpec{
                                      .maxDepthCents = 7.0,
                                      .rateHertz = {.minimum = 0.17, .maximum = 0.17},
                                      .depthMode = ModulationDepthMode::Fixed,
                                  },
                              .tremolo =
                                  TremoloSpec{
                                      .maxDepthDb = 1.5,
                                      .rateHertz = {2.0, 4.0},
                                      .gainMode = TremoloGainMode::NoBoost,
                                      .depthMode = ModulationDepthMode::Fixed,
                                  },
                          },
                  },
                  Region{
                      .keyRange = {.low = 64, .high = 127},
                      .sample = SampleRef::resolved(samples.metadata.id, 0),
                      .modulation =
                          InstrumentModulation{
                              .vibrato =
                                  VibratoSpec{
                                      .maxDepthCents = 13.0,
                                      .rateHertz = {.minimum = 0.34, .maximum = 0.34},
                                      .depthMode = ModulationDepthMode::Fixed,
                                  },
                              .tremolo =
                                  TremoloSpec{
                                      .maxDepthDb = 2.5,
                                      .rateHertz = {2.0, 4.0},
                                      .gainMode = TremoloGainMode::NoBoost,
                                  },
                          },
                  },
              },
          .modulation =
              InstrumentModulation{
                  .tremolo =
                      TremoloSpec{
                          .maxDepthDb = 4.5,
                          .rateHertz = {2.0, 4.0},
                          .gainMode = TremoloGainMode::NoBoost,
                          .depthMode = ModulationDepthMode::Fixed,
                      },
              },
      }},
  };
  const std::array<const SoundBankAsset*, 1> soundBanks{&instruments};
  const std::array<const SamplePoolAsset*, 1> samplePools{&samples};
  const SynthExportInput input{
      .name = "Region LFO",
      .soundBanks = soundBanks,
      .samplePools = samplePools,
  };

  const auto soundFont = buildSoundFont2(input, sources);
  const auto dls = buildDls(input, sources);
  expect(soundFont.diagnostics.empty() && soundFontGeneratorContains(soundFont.bytes, "igen", 6, 7) &&
             soundFontGeneratorContains(soundFont.bytes, "igen", 6, 13),
         "SoundFont should preserve each region's vibrato depth");
  expect(dls.diagnostics.empty() && dlsArt2ContainsConnection(dls.bytes, 0x0009, 0x0003, 7 * 65536) &&
             dlsArt2ContainsConnection(dls.bytes, 0x0009, 0x0003, 13 * 65536),
         "DLS should preserve each region's vibrato depth");

  auto simulatedInput = input;
  simulatedInput.modulationConversion = ModulationConversionPolicy::SequenceEventSimulation;
  const auto simulatedSf = buildSoundFont2(simulatedInput, sources);
  const auto simulatedDls = buildDls(simulatedInput, sources);
  expect(simulatedSf.diagnostics.empty() && chunkSize(simulatedSf.bytes, "imod") == 10 &&
             !soundFontGeneratorContains(simulatedSf.bytes, "igen", 6, 7) &&
             !soundFontGeneratorContains(simulatedSf.bytes, "igen", 6, 13) &&
             soundFontGeneratorContains(simulatedSf.bytes, "igen", 48, 15) &&
             soundFontGeneratorContains(simulatedSf.bytes, "igen", 48, 45) &&
             !soundFontGeneratorContains(simulatedSf.bytes, "igen", 48, 25),
         "SF2 simulation should omit oscillators and controllers while retaining fixed no-boost attenuation at both "
         "scopes");
  expect(simulatedDls.diagnostics.empty() &&
             !dlsArt2Contains(simulatedDls.bytes,
                              [&](size_t offset) {
                                return readLe16(simulatedDls.bytes, offset) != 0 ||
                                       readLe16(simulatedDls.bytes, offset + 2) != 0;
                              }) &&
             dlsArt2ContainsConnection(simulatedDls.bytes, 0, 0x0001, 15 * 65536) &&
             dlsArt2ContainsConnection(simulatedDls.bytes, 0, 0x0001, 45 * 65536) &&
             !dlsArt2ContainsConnection(simulatedDls.bytes, 0, 0x0001, 25 * 65536),
         "DLS simulation should omit oscillators and controllers while retaining fixed no-boost attenuation at both "
         "scopes");
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

  SamplePoolAsset samplePool{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Probe Samples",
          },
      .pool =
          SamplePool{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = SourceRange{.source = sourceId, .offset = 0, .size = 9},
                  .sampleRate = 16000,
                  .loop = Loop{.enabled = true, .start = 0, .length = 16},
              }},
          },
  };
  SoundBankAsset soundBank{
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
              .sample = SampleRef::resolved(samplePool.metadata.id, 0),
              .unityKey = 58.75,
              .envelope =
                  Envelope{
                      .attackSeconds = 1.0,
                      .holdSeconds = std::numeric_limits<double>::infinity(),
                      .decaySeconds = 0.11,
                      .secondDecaySeconds = 5.5,
                      .releaseSeconds = 0.25,
                      .sustainAmplitude = 29.0 / 31.0,
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

  const std::array<const SoundBankAsset*, 1> soundBanks{&soundBank};
  const std::array<const SamplePoolAsset*, 1> samples{&samplePool};
  const MidiModulationUsage midiModulationUsage{
      .vibratoDepth = 38.0 / 127.0,
      .vibratoRate = 12.0 / 127.0,
      .tremoloDepth = 24.0 / 127.0,
      .tremoloRate = 12.0 / 127.0,
  };
  const auto result = buildSoundFont2(
      SynthExportInput{
          .name = "Probe",
          .soundBanks = soundBanks,
          .samplePools = samples,
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
  expect(soundFontGeneratorContains(result.bytes, "pgen", 16, 250),
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
  expect(soundFontGeneratorContains(result.bytes, "igen", 24, -8479),
         "SoundFont export should write physical vibrato frequency");
  expect(soundFontGeneratorContains(result.bytes, "igen", 23, 0),
         "SoundFont export should write instrument vibrato delay generator");
  expect(soundFontGeneratorContains(result.bytes, "igen", 22, -7279),
         "SoundFont export should write physical tremolo frequency");
  expect(soundFontGeneratorContains(result.bytes, "igen", 34, 0),
         "SoundFont export should write attackVolEnv from Region envelope");
  expect(soundFontGeneratorContains(result.bytes, "igen", 35, 32767),
         "SoundFont export should approximate an endless hold with its longest hold time");
  expect(soundFontGeneratorContains(result.bytes, "igen", 36, 2941),
         "SoundFont export should combine two decay rates over its 100 dB envelope range");
  expect(soundFontGeneratorContains(result.bytes, "igen", 37, 1000),
         "SoundFont export should end the approximated second decay at silence");
  expect(soundFontGeneratorContains(result.bytes, "igen", 38, -2400),
         "SoundFont export should write releaseVolEnv from Region envelope");

  const auto simulatedResult = buildSoundFont2(
      SynthExportInput{
          .name = "Probe",
          .soundBanks = soundBanks,
          .samplePools = samples,
          .modulationConversion = ModulationConversionPolicy::SequenceEventSimulation,
      },
      sources);
  expect(simulatedResult.diagnostics.empty(),
         "SoundFont sequence-event simulation export should not report diagnostics for valid values");
  expect(!soundFontGeneratorContains(simulatedResult.bytes, "igen", 24, -8479) &&
             !soundFontGeneratorContains(simulatedResult.bytes, "igen", 23, 0) &&
             !soundFontGeneratorContains(simulatedResult.bytes, "igen", 22, -7279),
         "SoundFont sequence-event simulation export should suppress synth LFO generators");
  expect(!soundFontImodContains(simulatedResult.bytes, 206, 23, 1209),
         "SoundFont sequence-event simulation export should suppress synth vibrato-delay modulators");

  Instrument variant = soundBank.instruments.front();
  variant.explicitAddress = InstrumentAddress{.bank = 1, .program = 6};
  variant.regions.front().envelope.attackSeconds = 2.0;
  soundBank.instruments.push_back(std::move(variant));
  const auto shared =
      buildSoundFont2(SynthExportInput{.name = "Probe", .soundBanks = soundBanks, .samplePools = samples}, sources);
  expect(chunkSize(shared.bytes, "phdr") == 3 * 38 && chunkSize(shared.bytes, "inst") == 2 * 22 &&
             soundFontGeneratorContains(shared.bytes, "pgen", 34, 1200),
         "SoundFont envelope variants should share one sample-mapped instrument through preset ADSR offsets");

  soundBank.instruments.back().modulation.vibrato->maxDepthCents = 240.0;
  for (const auto conversion :
       {ModulationConversionPolicy::SynthModulators, ModulationConversionPolicy::SequenceEventSimulation}) {
    const auto distinctModulation = buildSoundFont2(
        SynthExportInput{
            .name = "Probe", .soundBanks = soundBanks, .samplePools = samples, .modulationConversion = conversion},
        sources);
    const bool simulated = conversion == ModulationConversionPolicy::SequenceEventSimulation;
    expect(distinctModulation.diagnostics.empty() && chunkSize(distinctModulation.bytes, "phdr") == 3 * 38 &&
               chunkSize(distinctModulation.bytes, "inst") == (simulated ? 2 : 3) * 22 &&
               (!simulated || soundFontGeneratorContains(distinctModulation.bytes, "pgen", 34, 1200)),
           "only exported modulation should distinguish SF2 instruments; shared presets must retain their envelope "
           "offsets");
  }

  const MidiModulationUsage inactiveVibrato{.vibratoDepth = 0.0};
  const auto scaledShared =
      buildSoundFont2(SynthExportInput{.name = "Probe",
                                       .soundBanks = soundBanks,
                                       .samplePools = samples,
                                       .midiModulationUsage = &inactiveVibrato,
                                       .modulationScaling = ModulationScalingPolicy::ObservedSequenceRange},
                      sources);
  expect(chunkSize(scaledShared.bytes, "phdr") == 3 * 38 && chunkSize(scaledShared.bytes, "inst") == 2 * 22 &&
             soundFontGeneratorContains(scaledShared.bytes, "pgen", 34, 1200),
         "presets whose modulation becomes identical after scaling should share an SF2 instrument");

  soundBank.instruments.resize(1);
  auto& regions = soundBank.instruments.front().regions;
  const Region region = regions.front();
  regions.resize(5000, region);
  expectThrows<std::overflow_error>(
      [&] {
        static_cast<void>(buildSoundFont2(
            SynthExportInput{.name = "Too many zones", .soundBanks = soundBanks, .samplePools = samples}, sources));
      },
      "SoundFont table offsets must reject generator indexes that exceed 16 bits");
}

void soundFontSampleHeadersUseTheFirstReferencingRegion() {
  SourceStore sources;
  const auto source = sources.add(SourceFile{.name = "headers.pcm"}, {0, 32, 64, 96});
  SoundBankAsset bank{.metadata = {.id = AssetId{1}}};
  bank.localSamples.samples.resize(2, Sample{
      .codec = AudioCodec::PcmS8,
      .encodedData = {.source = source, .size = 4},
      .sampleRate = 32000,
      .loop = {.enabled = true, .start = 1, .length = 2},
      .pitch = {.cents = 125},
  });
  bank.instruments.push_back(Instrument{.regions = {
      Region{.sample = SampleRef::resolved(bank.metadata.id, 0),
             .unityKey = 65.25,
             .loop = Loop{.enabled = true, .start = 2, .length = 1}},
      Region{.sample = SampleRef::resolved(bank.metadata.id, 0), .unityKey = 72.0},
  }});
  const std::array<const SoundBankAsset*, 1> banks{&bank};
  const auto result = buildSoundFont2(SynthExportInput{.soundBanks = banks}, sources);
  expect(result.diagnostics.empty() && chunkSize(result.bytes, "shdr") == 3 * 46,
         "SoundFont should retain unreferenced samples when filtering is disabled");
  const size_t first = asciiOffset(result.bytes, "shdr") + 8;
  const size_t second = first + 46;
  expect(readLe32(result.bytes, first + 28) == 2 && readLe32(result.bytes, first + 32) == 3 &&
             result.bytes[first + 40] == 64 && result.bytes[first + 41] == 25,
         "the first referencing region must determine sample-header pitch and loop overrides");
  expect(readLe32(result.bytes, second + 28) == 51 && readLe32(result.bytes, second + 32) == 53 &&
             result.bytes[second + 40] == 60 && result.bytes[second + 41] == 0,
         "unreferenced headers must retain decoded loops, padding offsets, and default pitch");
}

void synthSampleIndexesRetainTheirRangeUntilContainerExport() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "wide-sample-table.pcm"}, {0, 127});
  constexpr u32 lastSample = 65536;
  SoundBankAsset bank{.metadata = AssetMetadata{.id = AssetId{1}}};
  bank.localSamples.samples.resize(lastSample + 1, Sample{
                                                       .codec = AudioCodec::PcmS8,
                                                       .encodedData = SourceRange{.source = source, .size = 1},
                                                       .sampleRate = 32000,
                                                   });
  bank.localSamples.samples.back().encodedData.offset = 1;
  bank.instruments.push_back(Instrument{
      .regions = {Region{.sample = SampleRef::resolved(bank.metadata.id, lastSample)}},
  });
  const std::array<const SoundBankAsset*, 1> banks{&bank};
  const SynthExportInput input{.soundBanks = banks};
  const auto prepared = prepareSynthData(input, sources);
  const u32 resolved = prepared.instruments.at(0).regions.at(0).sampleIndex;
  expect(prepared.diagnostics.empty() && prepared.samples.size() == lastSample + 1 && resolved == lastSample &&
             prepared.samples[resolved].decoded.pcm == std::vector<s16>{32512},
         "shared synth preparation must retain distinct samples beyond the SoundFont index range");

  const auto dls = buildDls(input, sources);
  expect(dls.diagnostics.empty() && readLe32(dls.bytes, asciiOffset(dls.bytes, "wlnk") + 16) == lastSample,
         "DLS wave links must retain the full 32-bit sample index");

  expectThrows<std::overflow_error>(
      [&] { static_cast<void>(buildSoundFont2(input, sources)); },
      "SoundFont must reject an unrepresentable sample link instead of changing its sample");

  bank.instruments.front().regions.front().sample = SampleRef::resolved(bank.metadata.id, lastSample - 1);
  bank.localSamples.samples.pop_back();
  const auto soundFont = buildSoundFont2(input, sources);
  expect(soundFont.diagnostics.empty() && soundFontGeneratorContains(soundFont.bytes, "igen", 53, -1),
         "the largest representable SoundFont sample index must still be written intact");
}

void dlsExporterWritesDlsRiffFile() {
  constexpr double lfoStepHertz = 1000.0 / 16384.0;
  SourceStore sources;
  const auto sourceId = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  SamplePoolAsset samplePool{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Probe Samples",
          },
      .pool =
          SamplePool{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = SourceRange{.source = sourceId, .offset = 0, .size = 9},
                  .sampleRate = 16000,
                  .loop = Loop{.enabled = true, .start = 0, .length = 0},
              }},
          },
  };
  SoundBankAsset soundBank{
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
              .sample = SampleRef::resolved(samplePool.metadata.id, 0),
              .unityKey = 58.75,
              .envelope =
                  Envelope{
                      .attackSeconds = 1.0,
                      .holdSeconds = std::numeric_limits<double>::infinity(),
                      .decaySeconds = 2.0,
                      .secondDecaySeconds = 1.0,
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

  const std::array<const SoundBankAsset*, 1> soundBanks{&soundBank};
  const std::array<const SamplePoolAsset*, 1> samples{&samplePool};
  const MidiModulationUsage midiModulationUsage{
      .vibratoDepth = 38.0 / 127.0,
      .vibratoRate = 12.0 / 127.0,
      .tremoloDepth = 24.0 / 127.0,
      .tremoloRate = 12.0 / 127.0,
  };
  const auto result = buildDls(
      SynthExportInput{
          .name = "Probe",
          .soundBanks = soundBanks,
          .samplePools = samples,
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
  expect(chunkSize(result.bytes, "fmt ") == 18 && readLe16(result.bytes, asciiOffset(result.bytes, "fmt ") + 24) == 0,
         "DLS must append an empty extension to the shared 16-byte PCM format record");
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
  expect(dlsArt2ContainsConnection(result.bytes, 0x020c, std::numeric_limits<s32>::max()),
         "DLS export should approximate an endless hold with its longest hold time");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0207, 36677699),
         "DLS export should perceptually fit two rapid decay rates over its 96 dB envelope range");
  expect(dlsArt2ContainsConnection(result.bytes, 0x020a, 0),
         "DLS export should end a combined two-stage decay at silence");
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
          .soundBanks = soundBanks,
          .samplePools = samples,
          .modulationConversion = ModulationConversionPolicy::SequenceEventSimulation,
      },
      sources);
  expect(simulatedResult.diagnostics.empty(),
         "DLS sequence-event simulation export should not report diagnostics for valid values");
  expect(!dlsArt2ContainsConnection(simulatedResult.bytes, 0x0000, 0x0114, -8479 * 65536) &&
             !dlsArt2ContainsConnection(simulatedResult.bytes, 0x0000, 0x0115, 0) &&
             !dlsArt2ContainsConnection(simulatedResult.bytes, 0x0000, 0x0104, -7279 * 65536),
         "DLS sequence-event simulation export should suppress synth LFO generators");

  samplePool.pool.samples.front().sampleRate = std::numeric_limits<u32>::max();
  expectThrows<std::overflow_error>(
      [&] { static_cast<void>(buildDls(SynthExportInput{.soundBanks = soundBanks, .samplePools = samples}, sources)); },
      "DLS must reject a PCM byte rate that would overflow its WAVE format record");
}

}  // namespace

void runValueSynthExportTests() {
  regionResponsesAreSampledAtExport();
  riffChunksKeepLogicalSizesSeparateFromStoragePadding();
  envelopePredicateDetectsCanonicalData();
  adsrApproximationLowersUnsupportedStages();
  physicalModulationLowersToLegacySynthControls();
  fixedPhysicalLfoValuesNeedNoZeroRangeModulators();
  regionModulationExportsAtTheRegionScope();
  wavExporterWritesPcm16RiffFile();
  soundFontExporterWritesSfbkRiffFile();
  soundFontSampleHeadersUseTheFirstReferencingRegion();
  synthSampleIndexesRetainTheirRangeUntilContainerExport();
  dlsExporterWritesDlsRiffFile();
}
