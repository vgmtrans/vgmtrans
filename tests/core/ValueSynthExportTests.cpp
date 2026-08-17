/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "SessionSnapshotBuilder.h"

#include "value/export/CollectionBinding.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/export/synth/SynthExportData.h"
#include "value/synth/PsxSpu.h"
#include "value/synth/SnesDsp.h"

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

void pcm16DecoderHonorsExplicitByteOrder() {
  const Sample littleEndian{
      .codec = AudioCodec::PcmS16,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 0, .size = 4},
  };
  Sample bigEndian = littleEndian;
  bigEndian.bigEndian = true;

  const std::vector<u8> bytes{0x12, 0x34, 0xfe, 0xdc};
  const auto little = decodeSample(littleEndian, bytes);
  const auto big = decodeSample(bigEndian, bytes);
  expect(little && little->pcm == std::vector<s16>({0x3412, -8962}),
         "PCM16 should retain the default little-endian decoding");
  expect(big && big->pcm == std::vector<s16>({0x1234, -292}),
         "PCM16 should honor source-declared big-endian byte order");
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
                      .sample = SampleRef{.externalPool = samples.metadata.id, .index = 0},
                      .modulation =
                          InstrumentModulation{
                              .vibrato =
                                  VibratoSpec{
                                      .maxDepthCents = 7.0,
                                      .rateHertz = {.minimum = 0.17, .maximum = 0.17},
                                      .depthMode = ModulationDepthMode::Fixed,
                                  },
                          },
                  },
                  Region{
                      .keyRange = {.low = 64, .high = 127},
                      .sample = SampleRef{.externalPool = samples.metadata.id, .index = 0},
                      .modulation =
                          InstrumentModulation{
                              .vibrato =
                                  VibratoSpec{
                                      .maxDepthCents = 13.0,
                                      .rateHertz = {.minimum = 0.34, .maximum = 0.34},
                                      .depthMode = ModulationDepthMode::Fixed,
                                  },
                          },
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
  expect(soundFont.diagnostics.empty() && soundFontIgenContainsAmount(soundFont.bytes, 6, 7) &&
             soundFontIgenContainsAmount(soundFont.bytes, 6, 13),
         "SoundFont should preserve each region's vibrato depth");
  expect(dls.diagnostics.empty() && dlsArt2ContainsConnection(dls.bytes, 0x0009, 0x0003, 7 * 65536) &&
             dlsArt2ContainsConnection(dls.bytes, 0x0009, 0x0003, 13 * 65536),
         "DLS should preserve each region's vibrato depth");
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
              .sample = SampleRef{.externalPool = samplePool.metadata.id, .index = 0},
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
      .vibratoDepth = ObservedValueRange{.observed = true, .min = 4, .max = 38},
      .vibratoRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
      .tremoloDepth = ObservedValueRange{.observed = true, .min = 2, .max = 24},
      .tremoloRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
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
  expect(soundFontIgenContainsAmount(result.bytes, 35, 32767),
         "SoundFont export should approximate an endless hold with its longest hold time");
  expect(soundFontIgenContainsAmount(result.bytes, 36, 2941),
         "SoundFont export should combine two decay rates over its 100 dB envelope range");
  expect(soundFontIgenContainsAmount(result.bytes, 37, 1000),
         "SoundFont export should end the approximated second decay at silence");
  expect(soundFontIgenContainsAmount(result.bytes, 38, -2400),
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
  expect(!soundFontIgenContainsAmount(simulatedResult.bytes, 24, -8479) &&
             !soundFontIgenContainsAmount(simulatedResult.bytes, 23, 0) &&
             !soundFontIgenContainsAmount(simulatedResult.bytes, 22, -7279),
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
             soundFontPgenContainsAmount(shared.bytes, 34, 1200),
         "SoundFont envelope variants should share one sample-mapped instrument through preset ADSR offsets");
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
              .sample = SampleRef{.externalPool = samplePool.metadata.id, .index = 0},
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
      .vibratoDepth = ObservedValueRange{.observed = true, .min = 4, .max = 38},
      .vibratoRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
      .tremoloDepth = ObservedValueRange{.observed = true, .min = 2, .max = 24},
      .tremoloRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
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
}

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
          .regions = {Region{.sample = SampleRef{.externalPool = samples.metadata.id, .index = 0}}},
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

  expect(soundFontIgenContainsAmount(soundFont.bytes, 24, -8479),
         "standalone SoundFont export should retain native modulation when no MIDI replacement exists");
  expect(dlsArt2ContainsConnection(dls.bytes, 0x0000, 0x0114, -8479 * 65536),
         "standalone DLS export should retain native modulation when no MIDI replacement exists");
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
  addProbeCommand<ProbeNoteCommand>(track, config, Address{0}, probeRange(0, defaultNote.size()), defaultNote);
  addProbeCommand<ProbeProgramCommand>(track, config, Address{3}, probeRange(3, selectLead.size()), selectLead);
  addProbeCommand<ProbeNoteCommand>(track, config, Address{5}, probeRange(5, leadNote.size()), leadNote);
  addProbeCommand<ProbeEndCommand>(track, config, Address{8}, probeRange(8, end.size()), end);

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
        .regions = {Region{.sample = SampleRef{.externalPool = samplePoolId, .index = sampleIndex}}},
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

  test::SessionSnapshotBuilder ambiguousBuilder;
  ambiguousBuilder.assets = {sequence, instruments, samples};
  ambiguousBuilder.collections = {
      snapshot.collections().front(),
      Collection{
          .id = CollectionId{1},
          .name = "Other Usage",
          .members =
              {
                  .sequence = sequence.metadata.id,
                  .soundBanks = {instruments.metadata.id},
                  .samplePools = {samples.metadata.id},
              },
      },
  };
  const SessionSnapshot ambiguousSnapshot = ambiguousBuilder.finish();
  const auto ambiguousSoundFont =
      exportSoundBank(ambiguousSnapshot, sources, instruments.metadata.id, SynthExportFormat::SoundFont2, onlyUsed);
  const auto ambiguousDls =
      exportSoundBank(ambiguousSnapshot, sources, instruments.metadata.id, SynthExportFormat::Dls, onlyUsed);
  expect(chunkSize(ambiguousSoundFont.bytes, "phdr") == 4 * 38 &&
             chunkSize(ambiguousSoundFont.bytes, "shdr") == 4 * 46 &&
             readLe32(ambiguousDls.bytes, asciiOffset(ambiguousDls.bytes, "colh") + 8) == 3 &&
             chunkSize(ambiguousDls.bytes, "ptbl") == 20,
         "instrument-set export should retain all data when multiple collections could supply sequence context");

  const InstrumentIdentity semanticIdentity{.domain = "probe.instrument", .key = 2};
  auto semanticInstruments = instruments;
  semanticInstruments.instruments[2].identity = semanticIdentity;
  PerformanceSequence semanticPerformance{
      .tracks = {PerformanceTrack{
          .events =
              {
                  InstrumentPerformanceEvent{.sourceInstrument = semanticIdentity},
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
                  InstrumentPerformanceEvent{.bank = 1, .program = 1},
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
                  InstrumentPerformanceEvent{.bank = 1 << 7, .program = 1},
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

  const auto baseline =
      exportCollection(missingRuntimeSnapshot, sources, CollectionId{0}, ExportRequest{.kinds = {ExportKind::Dls}});
  expect(baseline.size() == 1 && !baseline[0].bytes.empty(),
         "ordinary full-bank synth export should survive a sequence rendering failure");
  diagnosticWithMessage(baseline[0].diagnostics, "Sequence program has no runtime executor");

  for (const auto kind : {ExportKind::SoundFont2, ExportKind::Dls}) {
    const auto failed = exportCollection(missingRuntimeSnapshot, sources, CollectionId{0},
                                         ExportRequest{
                                             .kinds = {kind},
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

void bindInstrumentSet(CollectionBindingContext& context) {
  const AssetId samples = context.samplePools.front()->metadata.id;
  auto& instruments = context.soundBanks.front();
  instruments.instruments = {Instrument{
      .name = "Prepared Instrument",
      .regions = {Region{.sample = SampleRef{.externalPool = samples, .index = 0}}},
  }};
}

struct PreparedProbeProgramState {
  PreparedProbeProgramState(const SequenceProgram&, bool fail) : shouldFail(fail) {}

  void finalizePerformance(PerformanceSequence& performance) const {
    if (shouldFail) {
      throw std::runtime_error("test finalizer failure");
    }
    for (auto& track : performance.tracks) {
      track.hasPhysicalModulation = true;
      track.events.emplace_back(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoDepth,
          .pitchDepthSemitones = 1.0,
      });
      track.events.emplace_back(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoRate,
          .frequencyHz = 6.0,
      });
    }
  }

  bool shouldFail = false;
};

struct ForeignRuntimeTrackState {};

struct ForeignRuntimePlayback {
  ForeignRuntimePlayback(ForeignRuntimeTrackState&, PerformanceEmitter&, VmApi&) {}
};

struct ForeignRuntimeCursor {
  using TrackState = ForeignRuntimeTrackState;
  using Playback = ForeignRuntimePlayback;
};

void bindPerformanceRuntime(CollectionBindingContext& context) {
  const auto* sequence = context.sequence;
  const bool fail = sequence != nullptr && sequence->metadata.name == "Failing Sequence";
  if (!context.replaceSequenceRuntime(makeCompiledRuntime<ProbeCompilerCursor, PreparedProbeProgramState>(fail))) {
    return;
  }
  context.warning("Collection binding warning");
}

void collectionBindingAppliesToWholeExport() {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "performance-finalizer.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});
  const SequenceProgramConfig config = probeSequenceConfig();
  TrackProgram track{.startAddress = Address{0}};
  const std::array<u8, 3> noteBytes{0x90, 0x3c, 0x04};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(track, config, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeEndCommand>(track, config, Address{3}, probeRange(3, endBytes.size()), endBytes);

  const SequenceProgramAsset sequence{
      .metadata = AssetMetadata{.id = AssetId{0}, .format = "Performance Finalizer", .name = "Sequence"},
      .program =
          SequenceProgram{
              .runtime = makeCompiledRuntime<ProbeCompilerCursor, PreparedProbeProgramState>(false),
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
          .regions = {Region{.sample = SampleRef{.externalPool = AssetId{2}, .index = 0}}},
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
  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(sequence);
  builder.assets.emplace_back(instruments);
  builder.assets.emplace_back(samples);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Performance Finalizer",
      .key = CollectionKey{.resolver = "Performance Finalizer", .value = "one"},
      .binder = bindPerformanceRuntime,
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
  failingCollection.key.value = "failure";
  failingCollection.members.sequence = failingSequence.metadata.id;
  builder.collections.push_back(std::move(failingCollection));
  auto mismatchedCollection = builder.collections.front();
  mismatchedCollection.id = CollectionId{2};
  mismatchedCollection.key.value = "runtime-mismatch";
  mismatchedCollection.binder = [](CollectionBindingContext& context) {
    if (!context.replaceSequenceRuntime(makeCompiledRuntime<ForeignRuntimeCursor>())) {
      return;
    }
  };
  builder.collections.push_back(std::move(mismatchedCollection));

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

  const auto mismatched = bindCollection(snapshot, CollectionId{2});
  expect(!mismatched.collection, "an incompatible runtime should fail at collection binding before VM execution");
  diagnosticWithMessage(mismatched.diagnostics, "Collection binding produced an incompatible sequence runtime family");
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
          .regions = {Region{.sample = SampleRef{.externalPool = samples.metadata.id, .index = 0}}},
      }},
  };

  test::SessionSnapshotBuilder builder;
  builder.assets.emplace_back(durable);
  builder.assets.emplace_back(samples);
  builder.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Prepared Probe",
      .key = CollectionKey{.resolver = "Prepared Probe", .value = "one"},
      .members =
          {
              .soundBanks = {durable.metadata.id},
              .samplePools = {samples.metadata.id},
          },
  });

  const auto snapshotWithBinder = [&](CollectionBinder binder) {
    auto copy = builder;
    copy.collections.front().binder = std::move(binder);
    return copy.finish();
  };
  const SessionSnapshot snapshot = snapshotWithBinder(bindInstrumentSet);
  const auto binding = bindCollection(snapshot, CollectionId{0});
  expect(binding.collection && binding.collection->soundBanks().size() == 1 &&
             binding.collection->soundBanks().front().metadata.id == durable.metadata.id &&
             binding.collection->soundBanks().front().instruments.front().name == "Prepared Instrument" &&
             snapshot.asset<SoundBankAsset>(durable.metadata.id)->instruments.front().name == "Durable Instrument",
         "collection binding should preserve selected asset identity without mutating durable assets");
  const auto artifacts =
      exportCollection(snapshot, sources, CollectionId{0}, ExportRequest{.kinds = {ExportKind::Dls}});

  expect(artifacts.size() == 1 && !artifacts.front().bytes.empty(), "collection binding fixture should export a DLS");
  const auto& dls = artifacts.front().bytes;
  expect(readLe32(dls, asciiOffset(dls, "colh") + 8) == 1,
         "bound instrument data should replace durable data instead of being appended");
  expect(containsAscii(dls, "Prepared Instrument") && !containsAscii(dls, "Durable Instrument"),
         "collection export should use only the binder's authoritative instrument view");

  const auto failed = bindCollection(snapshotWithBinder([](CollectionBindingContext& context) {
                                       context.soundBanks.front().instruments.front().name = "Partially Bound";
                                       context.fail("expected binding failure");
                                     }),
                                     CollectionId{0});
  expect(!failed.collection &&
             snapshot.asset<SoundBankAsset>(durable.metadata.id)->instruments.front().name == "Durable Instrument",
         "an explicit binding failure should publish neither a partial collection nor durable mutations");
  diagnosticWithMessage(failed.diagnostics, "expected binding failure");

  const auto threw = bindCollection(snapshotWithBinder([](CollectionBindingContext& context) {
                                      context.soundBanks.front().instruments.front().name = "Partially Bound";
                                      throw std::runtime_error("expected binding exception");
                                    }),
                                    CollectionId{0});
  expect(!threw.collection,
         "an exception should abort collection binding instead of publishing the callback's partial changes");
  diagnosticWithMessage(threw.diagnostics, "Prepared Probe collection binding failed: expected binding exception");

  const auto changedIdentity = bindCollection(snapshotWithBinder([](CollectionBindingContext& context) {
                                                context.soundBanks.front().metadata.id = AssetId{99};
                                                context.soundBanks.front().metadata.format = "Changed";
                                              }),
                                              CollectionId{0});
  expect(!changedIdentity.collection,
         "collection binding should reject changes to selected instrument identity or order");
  diagnosticWithMessage(changedIdentity.diagnostics,
                        "Collection binding changed sound bank identity, format, or order");
}

u32 synthOnlySequenceExecutions = 0;

Effects countSynthOnlySequenceExecution(const SourceCommand& command, std::any& programState, std::any& trackState,
                                        PerformanceEmitter& out, VmApi& vm) {
  ++synthOnlySequenceExecutions;
  static const ExecuteCommand execute = probeSequenceRuntime().execute;
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
  addProbeCommand<ProbeNoteCommand>(track, config, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeEndCommand>(track, config, Address{3}, probeRange(3, endBytes.size()), endBytes);

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
          .regions = {Region{.sample = SampleRef{.externalPool = samples.metadata.id, .index = 0}}},
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
              .sample = SampleRef{.externalPool = validSamplePool.metadata.id, .index = 9},
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
  TrackProgram track{.sourceTrackNumber = 3, .startAddress = Address{0}};
  const std::array<u8, 3> noteBytes{0x90, 0x3c, 0x04};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(track, config, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  track.commands.back().annotation = SourceAnnotationId{40};
  addProbeCommand<ProbeEndCommand>(track, config, Address{3}, probeRange(3, endBytes.size()), endBytes);
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
          .regions = {Region{.sample = SampleRef{.externalPool = samples.metadata.id, .index = 0}}},
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

}  // namespace

void runValueSynthExportTests() {
  snesBrrDecoderProducesPcm();
  ndsImaAdpcmDecoderRejectsInvalidInitialIndex();
  pcm16DecoderHonorsExplicitByteOrder();
  envelopePredicateDetectsCanonicalData();
  adsrApproximationLowersUnsupportedStages();
  physicalModulationLowersToLegacySynthControls();
  fixedPhysicalLfoValuesNeedNoZeroRangeModulators();
  regionModulationExportsAtTheRegionScope();
  wavExporterWritesPcm16RiffFile();
  soundFontExporterWritesSfbkRiffFile();
  dlsExporterWritesDlsRiffFile();
  standaloneSynthExportsKeepNativeModulation();
  collectionSynthExportsCanExportOnlyUsedInstruments();
  collectionBindingAppliesToWholeExport();
  collectionBindingProducesAnImmutableInstrumentView();
  synthOnlyExportRendersSequencesWithoutOriginalModulation();
  exportDiagnosticsPreserveSourceRanges();
  collectionPlaybackPreparesOneRenderedMidiAndSoundFontPair();
}
