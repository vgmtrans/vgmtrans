/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/PandoraBoxSnes/PandoraBoxSnes.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::pandora_box_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeLe16(std::vector<u8>& bytes, u32 offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void writeBytes(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
}

template <class Event>
std::vector<const Event*> events(const PerformanceTrack& track) {
  std::vector<const Event*> result;
  for (const PerformanceEvent& event : track.events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      result.push_back(typed);
    }
  }
  return result;
}

std::vector<const ModulationPerformanceEvent*> modulationEvents(const PerformanceTrack& track,
                                                                ModulationPerformanceTarget target) {
  auto result = events<ModulationPerformanceEvent>(track);
  std::erase_if(result, [=](const auto* event) { return event->target != target; });
  return result;
}

std::vector<u8> scannerFixture(Version version) {
  std::vector<u8> bytes(kAramSize);
  if (version == Version::Traverse) {
    writeBytes(bytes, 0xf000, {0x8d, 0x10, 0x7d, 0xf0, 0x45, 0x6d, 0xf7, 0x08, 0xfc, 0xc4,
                               0x00, 0xf7, 0x08, 0xfc, 0xc4, 0x01, 0xbc, 0xf0, 0x31});
    writeLe16(bytes, 0x01c6, 0x3000);
  } else {
    writeBytes(bytes, 0xf000, {0x8d, 0x10, 0xfc, 0xf7, 0x20, 0xdc, 0x37, 0x20, 0x68, 0xff, 0xf0, 0x30});
    writeLe16(bytes, 0x20, 0x3000);
  }
  writeBytes(bytes, 0xf020, {0xe8, version == Version::Traverse ? u8{0xfc} : u8{0xfb}, 0x8d, 0x5d, 0x61});
  writeBytes(bytes, 0xf030, {0x8d, 0x0c, 0x60, 0x97, 0xd9, 0xfd, 0xf7, 0xd9,
                             0xec, 0x83, 0x01, 0xf0, 0x07, 0x76, 0x3f, 0x01});

  bytes[0x0183] = 2;
  writeBytes(bytes, 0x0140, {0x44, 0x55});
  bytes[0x3006] = 120;
  bytes[0x3007] = 96;
  bytes[0x300c] = 0x40;
  bytes[0x3020] = 0xff;
  for (u32 track = 0; track < kTrackCount; ++track) {
    writeLe16(bytes, 0x3010 + track * 2, 0xffff);
  }
  writeLe16(bytes, 0x3010, 0x0100);
  writeBytes(bytes, 0x3040, {0x44, 0x55});
  writeBytes(bytes, 0x3100, {0x61, 0x01, 4, 0xf5});

  const u32 directory = version == Version::Traverse ? 0xfc00 : 0xfb00;
  writeLe16(bytes, directory + 4, 0x4000);
  writeLe16(bytes, directory + 6, 0x4000);
  writeBytes(bytes, 0x4000, {1, 0, 0, 0, 0, 0, 0, 0, 0});
  return bytes;
}

PerformanceSequence render(std::initializer_list<u8> track, Version version = Version::Standard) {
  std::vector<u8> bytes = scannerFixture(version);
  writeBytes(bytes, 0x3100, track);
  const ByteReader reader(SourceId{501}, bytes);
  const auto layout = findLayout(reader);
  expect(layout.has_value(), "fixture driver signatures should produce a layout");
  SequenceParse parsed = decodeSequence(reader, *layout, AssetId{501});
  return SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
}

void layoutAndSynthUseAuditedDriverTables() {
  for (const Version version : {Version::Standard, Version::Traverse}) {
    const std::vector<u8> bytes = scannerFixture(version);
    const auto layout = findLayout(ByteReader(SourceId{502}, bytes));
    expect(layout && layout->version == version && layout->sequenceHeaderAddress == 0x3000 &&
               localInstrumentAddress(*layout, 0) == 0x3040 && layout->globalInstrumentTableAddress == 0x0140 &&
               layout->globalInstrumentCount == 2 && layout->spcDirAddress ==
                   (version == Version::Traverse ? 0xfc00 : 0xfb00) &&
               layout->tracks[0] == 0x3100 &&
               programSrcn(ByteReader(SourceId{502}, bytes), *layout, 1) == 1,
           "driver signatures should recover each version's live header, relative tracks, DIR, and SRCN tables");
    std::vector<u8> fallbackBytes = bytes;
    fallbackBytes[0x3041] = 0x99;
    expect(programSrcn(ByteReader(SourceId{503}, fallbackBytes), *layout, 1) ==
               (version == Version::Traverse ? 0x3f : 0),
           "unmapped local programs should use each driver's audited fallback SRCN");
  }

  const std::vector<u8> bytes = scannerFixture(Version::Standard);
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "PandoraBoxSnes fixture.aram"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const Collection* collection = snapshot.collections().empty() ? nullptr : &snapshot.collections().front();
  const SoundBankAsset* bank = collection == nullptr || collection->members.soundBanks.empty()
                                   ? nullptr
                                   : snapshot.asset<SoundBankAsset>(collection->members.soundBanks.front());
  const auto found = bank == nullptr ? std::vector<Instrument>::const_iterator{}
                                     : std::ranges::find(bank->instruments, u64{1}, [](const Instrument& instrument) {
                                         return instrument.identity ? instrument.identity->key : ~u64{0};
                                       });
  const Instrument* instrument = bank != nullptr && found != bank->instruments.end() ? &*found : nullptr;
  expect(snapshot.diagnostics().empty() && snapshot.collections().size() == 1 && instrument != nullptr &&
             instrument->regions.size() == 1 && instrument->regions.front().unityKey == 45.0 &&
             instrument->regions.front().envelope == snesDspEnvelope(0xff, 0xe0, 0),
         "local programs should bind BRR samples with the reset-time $FF/$E0 DSP envelope");
}

void fineTuningMatchesDriverPitch() {
  const auto tuningFor = [](u16 pitch, int key) {
    return (45.0 + 12.0 * std::log2(pitch / 4096.0) - key) * 100.0;
  };
  const PerformanceSequence performance = render({0xe1, 60, 0x01, 4, 0xe1, 0xf6, 0x0a, 4, 0xf5});
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto tuning = events<TuningPerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 2 && tuning.size() == 2 && notes[0]->key == 36.0 &&
             notes[1]->key == 45.0 && std::abs(tuning[0]->cents - tuningFor(0x0983 + 60, 36)) < 0.000001 &&
             std::abs(tuning[1]->cents - tuningFor(0x1000 - 10, 45)) < 0.000001,
         "notes should retain nominal keys while pitch-table and signed $E1 offsets become exact tuning");

  const PerformanceSequence tied = render({0x11, 4, 0xe1, 60, 0x11, 4, 0x02, 4, 0xf5});
  const auto tiedNotes = events<NotePerformanceEvent>(tied.tracks.front());
  const auto tiedTuning = events<TuningPerformanceEvent>(tied.tracks.front());
  expect(tiedNotes.size() == 3 && tiedNotes[1]->extendsPrevious && !tiedNotes[1]->restartsEnvelope &&
             tiedTuning.size() == 2 &&
             std::abs(tiedTuning.back()->cents - tuningFor(0x0a14 + 60, 37)) < 0.000001,
         "$E1 should remain pending across a same-note tie until the driver recalculates pitch");
}

void versionedVolumeAndDynamicAdsrMatchTheDrivers() {
  expect(decodedVolume(Version::Standard, 0x0f) == 0x3c && decodedVolume(Version::Standard, 0x40) == 0x40 &&
             decodedVolume(Version::Traverse, 0xff) == 0x3c && decodedVolume(Version::Traverse, 0x40) == 0x40,
         "only Standard $00-$0F and Traverse $F0-$FF values should index the shared volume table");
  const DynamicAdsr minimum = dynamicAdsr(0, 0, 0, 0);
  const DynamicAdsr maximum = dynamicAdsr(127, 127, 127, 127);
  expect(minimum.adsr1 == 0x80 && minimum.adsr2 == 0xe0 && maximum.adsr1 == 0xff && maximum.adsr2 == 0x1f,
         "five dynamic ADSR operands should use the SPC700 integer scaling and ignore the last operand");

  const PerformanceSequence standard = render({0xf6, 0x0f, 0xf5});
  const PerformanceSequence traverse = render({0xf6, 0xff, 0xf5}, Version::Traverse);
  const PerformanceSequence standardDirect = render({0xf6, 0x40, 0xe6, 0xf5});
  const PerformanceSequence traverseDirect = render({0xf6, 0x40, 0xe7, 0xf5}, Version::Traverse);
  const auto standardLevels = events<LevelPerformanceEvent>(standard.tracks.front());
  const auto traverseLevels = events<LevelPerformanceEvent>(traverse.tracks.front());
  const auto standardDirectLevels = events<LevelPerformanceEvent>(standardDirect.tracks.front());
  const auto traverseDirectLevels = events<LevelPerformanceEvent>(traverseDirect.tracks.front());
  expect(!standardLevels.empty() && !traverseLevels.empty() &&
             std::abs(standardLevels.back()->linearGain - 0x3c / 255.0) < 0.000001 &&
             std::abs(traverseLevels.back()->linearGain - 0x3c / 255.0) < 0.000001 &&
             standardDirectLevels.back()->linearGain == 0x41 / 255.0 &&
             traverseDirectLevels.back()->linearGain == 0x3f / 255.0,
         "runtime volume events should preserve each driver's indexed representation and 8-bit mixer scale");
}

void modulationEnvelopeReverbAndPanRemainPhysical() {
  const PerformanceSequence performance = render({
      0xe3, 0x20,
      0xeb,
      0xe8, 2, 3, 4, 0, 8,
      0x01, 8,
      0xf3, 127, 127, 127, 127, 99,
      0xea,
      0xf5,
  });
  const PerformanceTrack& track = performance.tracks.front();
  const auto balance = events<StereoBalancePerformanceEvent>(track);
  const auto reverb = events<ReverbPerformanceEvent>(track);
  const auto envelope = events<EnvelopePerformanceEvent>(track);
  const auto depth = modulationEvents(track, ModulationPerformanceTarget::VibratoDepth);
  const auto rate = modulationEvents(track, ModulationPerformanceTarget::VibratoRate);
  expect(performance.diagnostics.empty() && !balance.empty() && balance.back()->leftGain == 0.75 &&
             balance.back()->rightGain == 0.25,
         "$E3 should retain the driver's exact constant-sum left/right DSP gains");
  expect(reverb.size() >= 3 && reverb[reverb.size() - 2]->send == 0x30 / 128.0 && reverb.back()->send == 0.0,
         "$EA/$EB should disable and enable the current voice's header-configured echo send");
  expect(envelope.size() == 1 && envelope.front()->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks &&
             envelope.front()->update.values == snesDspEnvelope(0xff, 0x1f, 0),
         "$F3 should replace the active and persistent DSP ADSR state");
  expect(!depth.empty() && depth.back()->pitchDepthSemitones && *depth.back()->pitchDepthSemitones > 0.0 &&
             depth.back()->context.shape && depth.back()->context.shape->waveform == LfoWaveform::Triangle &&
             depth.back()->context.shape->samples.size() == 16 && depth.back()->context.pitchRangeSemitones &&
             std::abs(depth.back()->context.pitchRangeSemitones->minimum -
                      12.0 * std::log2(2415.0 / 2435.0)) < 0.000001 &&
             std::abs(depth.back()->context.pitchRangeSemitones->maximum -
                      12.0 * std::log2(2447.0 / 2435.0)) < 0.000001 &&
             depth.back()->context.delayTicks == 2 &&
             depth.back()->context.delayUpdateMode == LfoDelayUpdateMode::FutureNotesOnly && !rate.empty() &&
             rate.back()->context.cyclesPerTick == 1.0 / (2.0 * 3.0 * 8.0),
         "$E8 should emit the audited delayed triangle pitch LFO, not a generic event or tremolo");
  expect(modulationEvents(track, ModulationPerformanceTarget::TremoloDepth).empty(),
         "the Pandora Box drivers have no tremolo modulation path");

  const PerformanceSequence disabled = render({0xe8, 0, 2, 4, 0, 8, 0x01, 4, 0xe9, 0, 0xf5});
  const auto disabledDepth = modulationEvents(disabled.tracks.front(), ModulationPerformanceTarget::VibratoDepth);
  expect(!disabledDepth.empty() && disabledDepth.back()->pitchDepthSemitones == 0.0 &&
             disabledDepth.back()->context.zeroDepthBehavior == LfoZeroDepthBehavior::HoldOutputUntilNextNote,
         "$E9 should stop the pitch accumulator without snapping its current DSP pitch back to center");
}

void slursAndNestedRepeatBreaksFollowDriverFlow() {
  const PerformanceSequence slur = render({0x11, 4, 0x12, 4, 0xf5});
  const auto notes = events<NotePerformanceEvent>(slur.tracks.front());
  expect(slur.diagnostics.empty() && notes.size() == 2 && !notes.back()->restartsEnvelope &&
             !notes.back()->restartsLfoPhase,
         "a slurred key change should continue the active DSP voice without KON or LFO restart");
  const PerformanceSequence shortGate = render({0x49, 0x01, 1, 0xf5});
  const auto shortNotes = events<NotePerformanceEvent>(shortGate.tracks.front());
  expect(shortGate.diagnostics.empty() && shortNotes.size() == 1 && shortNotes.front()->durationTicks == 1,
         "a sub-tick quantized gate should still sound until the driver's next sequence update");

  const PerformanceSequence repeat = render({0xec, 2, 0x01, 4, 0xee, 0x02, 4, 0xed, 0xf5});
  expect(repeat.diagnostics.empty() && events<NotePerformanceEvent>(repeat.tracks.front()).size() == 3,
         "repeat break should branch only on the final pass of the paired repeat frame");
  const PerformanceSequence single = render({0xec, 1, 0x01, 4, 0xee, 0x02, 4, 0xed, 0xf5});
  expect(single.diagnostics.empty() && events<NotePerformanceEvent>(single.tracks.front()).size() == 1,
         "a count-one repeat break should skip the remainder of its first and only pass");
  const PerformanceSequence infinite = render({0xec, 0xff, 0x01, 4, 0xed, 0xf5});
  expect(infinite.diagnostics.empty() && events<NotePerformanceEvent>(infinite.tracks.front()).size() == 1,
         "$FF repeats should be declared loops and stop PlayOnce rendering after one pass");
}

}  // namespace

void runPandoraBoxSnesModuleTests() {
  layoutAndSynthUseAuditedDriverTables();
  fineTuningMatchesDriverPitch();
  versionedVolumeAndDynamicAdsrMatchTheDrivers();
  modulationEnvelopeReverbAndPanRemainPhysical();
  slursAndNestedRepeatBreaksFollowDriverFlow();
}
