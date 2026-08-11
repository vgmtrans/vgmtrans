/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include "value/session/Session.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::sony_ps1;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void le16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void le32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
  bytes[offset + 2] = static_cast<u8>(value >> 16);
  bytes[offset + 3] = static_cast<u8>(value >> 24);
}

void be16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value >> 8);
  bytes[offset + 1] = static_cast<u8>(value);
}

void be24(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value >> 16);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
  bytes[offset + 2] = static_cast<u8>(value);
}

void be32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value >> 24);
  bytes[offset + 1] = static_cast<u8>(value >> 16);
  bytes[offset + 2] = static_cast<u8>(value >> 8);
  bytes[offset + 3] = static_cast<u8>(value);
}

std::vector<u8> sequenceFixture(std::initializer_list<u8> events, bool reversed = false) {
  std::vector<u8> bytes(15 + events.size());
  if (reversed) {
    bytes[0] = 'p';
    bytes[1] = 'Q';
    bytes[2] = 'E';
    bytes[3] = 'S';
  } else {
    bytes[0] = 'S';
    bytes[1] = 'E';
    bytes[2] = 'Q';
    bytes[3] = 'p';
  }
  be32(bytes, 4, 1);
  be16(bytes, 8, 96);
  be24(bytes, 10, 500000);
  bytes[13] = 4;
  bytes[14] = 2;
  std::ranges::copy(events, bytes.begin() + 15);
  return bytes;
}

constexpr u32 kFixtureVagSize = 0xc0;

void fillVag(std::vector<u8>& bytes, u32 offset) {
  for (u32 frame = 1; frame < kFixtureVagSize / kPsxAdpcmBlockBytes; ++frame) {
    const u32 block = offset + frame * kPsxAdpcmBlockBytes;
    bytes[block] = 0x11;
    bytes[block + 1] = frame + 1 == kFixtureVagSize / kPsxAdpcmBlockBytes ? 1 : 0;
    for (u32 byte = 2; byte < kPsxAdpcmBlockBytes; ++byte) {
      bytes[block + byte] = static_cast<u8>(frame * 17 + byte);
    }
  }
}

std::vector<u8> vabFixture(u32 version, bool body) {
  const u32 programSlots = version > 4 ? 128 : 64;
  const u32 sizeShift = version > 4 ? 3 : 2;
  const u32 programTable = 0x20;
  const u32 toneTable = programTable + programSlots * 0x10;
  const u32 sizeTable = toneTable + 16 * 0x20;
  const u32 sampleData = sizeTable + 0x200;
  std::vector<u8> bytes(sampleData + (body ? kFixtureVagSize : 0), 0);
  bytes[0] = 'p';
  bytes[1] = 'B';
  bytes[2] = 'A';
  bytes[3] = 'V';
  le32(bytes, 4, version);
  le32(bytes, 8, 7);
  le32(bytes, 0x0c, sampleData + kFixtureVagSize);
  le16(bytes, 0x10, 0xeeee);
  le16(bytes, 0x12, 1);
  le16(bytes, 0x14, 1);
  le16(bytes, 0x16, 1);
  bytes[0x18] = 127;
  bytes[0x19] = 64;

  bytes[programTable] = 1;
  bytes[programTable + 1] = 100;
  bytes[programTable + 4] = 64;
  bytes[toneTable + 2] = 80;
  bytes[toneTable + 3] = 64;
  bytes[toneTable + 4] = 60;
  bytes[toneTable + 5] = 64;
  bytes[toneTable + 6] = 12;
  bytes[toneTable + 7] = 100;
  le16(bytes, toneTable + 0x10, composePsxAdsr1(1, 0x70, 8, 8));
  le16(bytes, toneTable + 0x12, composePsxAdsr2(1, 1, 0x40, 1, 0x10));
  le16(bytes, toneTable + 0x14, 0);
  le16(bytes, toneTable + 0x16, 1);
  le16(bytes, sizeTable + 2, static_cast<u16>(kFixtureVagSize >> sizeShift));
  if (body) {
    fillVag(bytes, sampleData);
  }
  return bytes;
}

template <class Event>
std::vector<const Event*> eventsOfType(const PerformanceTrack& track) {
  std::vector<const Event*> events;
  for (const auto& value : track.events) {
    if (const auto* event = std::get_if<Event>(&value)) {
      events.push_back(event);
    }
  }
  return events;
}

}  // namespace

void sonyPs1SequenceSupportsBothLoopCountGenerations() {
  const auto modern = sequenceFixture({
      0x00, 0xc0, 0x02,                                                  // program 2
      0x00, 0x90, 0x3c, 0x64,                                            // note on
      0x0a, 0x3c, 0x00,                                                  // running-status note off
      0x00, 0xb0, 0x63, 0x14,                                            // loop start
      0x00, 0xb0, 0x06, 0x02,                                            // later SDKs: count through Data Entry
      0x00, 0x90, 0x3e, 0x64, 0x05, 0x3e, 0x00, 0x00, 0xb0, 0x63, 0x1e,  // loop end
      0x00, 0xff, 0x2f, 0x00,
  });
  const ByteReader modernReader(SourceId{81}, modern);
  const auto modernLayout = readSonyPs1SequenceLayout(modernReader, 0);
  expect(modernLayout.has_value() && modernLayout->events.size() == 9,
         "SonyPS1 should decode running status and the documented end marker");
  const auto loopEnd = std::ranges::find_if(
      modernLayout->events, [](const SonyPs1EventLayout& event) { return event.loopDestination.has_value(); });
  expect(loopEnd != modernLayout->events.end() && loopEnd->loopCount == 2,
         "later libsnd Data Entry loop counts should be attached to loop end");

  const SequenceProgram program = parseSonyPs1Sequence(modernReader, AssetId{81}, *modernLayout);
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, sonyPs1SequenceDialect());
  expect(performance.diagnostics.empty(), "modern SonyPS1 loop fixture should render without diagnostics");
  const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks.front());
  expect(std::ranges::count(notes, 62.0, [](const NotePerformanceEvent* note) { return note->key; }) == 2,
         "a loop count of two should play the enclosed note twice");
  expect(notes.front()->durationTicks == 10, "velocity-zero note-on should close the matching active voice");

  const auto legacy = sequenceFixture(
      {
          0x00,
          0xb0,
          0x63,
          0x14,  // loop start
          0x00,
          0xb0,
          0x62,
          0x7f,  // early SDKs: count through NRPN LSB
          0x00,
          0xb0,
          0x63,
          0x1e,
          0x00,
          0xff,
          0x2f,
          0x00,
      },
      true);
  const ByteReader legacyReader(SourceId{82}, legacy);
  const auto legacyLayout = readSonyPs1SequenceLayout(legacyReader, 0);
  expect(legacyLayout.has_value() && legacyLayout->events[2].loopCount == 127,
         "early libsnd CC98 loop counts and pQES memory signatures should remain supported");

  const auto rippedWithoutEnd = sequenceFixture({
      0x00, 0xb0, 0x63, 0x14,                                            // loop start
      0x00, 0xb0, 0x06, 0x7f,                                            // infinite loop
      0x00, 0x90, 0x3c, 0x64, 0x0a, 0x3c, 0x00, 0x00, 0xb0, 0x63, 0x1e,  // loop end; no FF 2F follows in many PSF rips
      0x00, 0x90, 0x00, 0x00,                                            // unrelated zero-filled RAM
      0x00, 0x00, 0x00, 0x00,
  });
  const ByteReader rippedReader(SourceId{86}, rippedWithoutEnd);
  const auto rippedLayout = readSonyPs1SequenceLayout(rippedReader, 0);
  expect(rippedLayout && rippedLayout->events.size() == 5 && rippedLayout->length == 34,
         "an infinite Sony loop should terminate layout discovery when a PSF rip omits End of Track");
  const PerformanceSequence rippedPerformance =
      SequenceVm(LoopPolicy::PlayOnce)
          .render(parseSonyPs1Sequence(rippedReader, AssetId{86}, *rippedLayout), sonyPs1SequenceDialect());
  expect(rippedPerformance.diagnostics.empty(), "a ripped sequence ending at its infinite loop should render cleanly");

  const auto rippedWithoutEndOrLoop = sequenceFixture({
      0x00, 0xc0, 0x02, 0x00, 0x90, 0x3c, 0x64, 0x0a, 0x3c, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  });
  const ByteReader noEndReader(SourceId{89}, rippedWithoutEndOrLoop);
  const auto noEndLayout = readSonyPs1SequenceLayout(noEndReader, 0);
  expect(noEndLayout && noEndLayout->events.size() == 3 && noEndLayout->length == 25,
         "ten zero-filled bytes should retain the legacy boundary for a PSF rip without End of Track");
  const PerformanceSequence noEndPerformance =
      SequenceVm(LoopPolicy::PlayOnce)
          .render(parseSonyPs1Sequence(noEndReader, AssetId{89}, *noEndLayout), sonyPs1SequenceDialect());
  expect(noEndPerformance.diagnostics.empty(), "a zero-terminated PSF sequence should render cleanly");
  const auto noEndNotes = eventsOfType<NotePerformanceEvent>(noEndPerformance.tracks.front());
  expect(noEndNotes.size() == 1 && noEndNotes.front()->durationTicks == 10,
         "a zero-terminated PSF sequence should retain its decoded musical events");
}

void sonyPs1SepAndVabLayoutsAreVersionAware() {
  const std::vector<u8> end{0x00, 0xff, 0x2f, 0x00};
  std::vector<u8> sep(19 + end.size() + 13 + end.size(), 0);
  sep[0] = 'S';
  sep[1] = 'E';
  sep[2] = 'Q';
  sep[3] = 'p';
  be16(sep, 4, 0);
  be16(sep, 6, 0);
  be16(sep, 8, 48);
  be24(sep, 10, 500000);
  sep[13] = 4;
  sep[14] = 2;
  be32(sep, 15, static_cast<u32>(end.size()));
  std::ranges::copy(end, sep.begin() + 19);
  const u32 second = 19 + end.size();
  be16(sep, second, 1);
  be16(sep, second + 2, 96);
  be24(sep, second + 4, 400000);
  sep[second + 7] = 3;
  sep[second + 8] = 2;
  be32(sep, second + 9, static_cast<u32>(end.size()));
  std::ranges::copy(end, sep.begin() + second + 13);
  const auto sepLayouts = findSonyPs1Sequences(ByteReader(SourceId{83}, sep));
  expect(sepLayouts.size() == 2 && sepLayouts[0].sepFirst && !sepLayouts[1].sepFirst && sepLayouts[1].sequenceId == 1 &&
             sepLayouts[1].ppqn == 96,
         "SEP should expose every packed sequence with its own short header");

  const auto oldBytes = vabFixture(4, true);
  const auto lateBytes = vabFixture(7, true);
  const auto old = readSonyPs1BankLayout(ByteReader(SourceId{84}, oldBytes), 0);
  const auto late = readSonyPs1BankLayout(ByteReader(SourceId{85}, lateBytes), 0);
  expect(old && old->programSlots == 64 && old->sampleSizeShift == 2 && old->expectedSampleBytes == kFixtureVagSize,
         "VAB versions through 4 should use the 64-program, four-byte-size layout");
  expect(
      late && late->programSlots == 128 && late->sampleSizeShift == 3 && late->expectedSampleBytes == kFixtureVagSize,
      "VAB versions above 4 should use the 128-program, eight-byte-size layout");

  auto separatedBytes = vabFixture(7, false);
  const u32 separatedBody = static_cast<u32>(separatedBytes.size()) + 0x100;
  separatedBytes.resize(separatedBody + kFixtureVagSize, 0);
  fillVag(separatedBytes, separatedBody);
  const auto separated = readSonyPs1BankLayout(ByteReader(SourceId{87}, separatedBytes), 0);
  expect(separated && separated->hasSampleBody && separated->sampleDataOffset == separatedBody,
         "a VAB header should locate a separately loaded sample body instead of decoding intervening RAM");

  auto forcedBytes = vabFixture(7, false);
  const u32 forcedBody = static_cast<u32>(forcedBytes.size()) + 4;
  forcedBytes.resize(forcedBody + kFixtureVagSize + 0x30, 0);
  fillVag(forcedBytes, forcedBody);
  for (u32 block = forcedBody + kFixtureVagSize; block < forcedBytes.size(); block += kPsxAdpcmBlockBytes) {
    forcedBytes[block + 1] = 7;
    std::fill_n(forcedBytes.begin() + block + 2, 14, 0x77);
  }
  const auto forced = findSonyPs1Banks(ByteReader(SourceId{88}, forcedBytes));
  expect(forced.size() == 1 && forced.front().hasSampleBody && forced.front().sampleDataOffset == forcedBody,
         "a sole VAB and sample collection should retain the legacy matcher's forced one-to-one pairing");
}

void sonyPs1ModuleBuildsCombinedAndSplitVabSynths() {
  Session combined;
  combined.registerFormat(sonyPs1Definition());
  combined.addSource(SourceFile{.name = "combined.VAB"}, vabFixture(7, true));
  combined.scanPendingSources();
  const SessionSnapshot combinedSnapshot = combined.snapshot();
  expect(combinedSnapshot.collections().size() == 1, "a standalone combined VAB should produce a synth collection");
  const Collection& combinedCollection = combinedSnapshot.collections().front();
  expect(
      combinedCollection.members.instrumentSets.size() == 1 && combinedCollection.members.sampleCollections.size() == 1,
      "combined VAB instruments and sample body should remain paired");
  const auto* instruments = combinedSnapshot.asset<InstrumentSetAsset>(combinedCollection.members.instrumentSets[0]);
  expect(instruments && instruments->instruments.size() == 1 && instruments->instruments[0].regions.size() == 1,
         "the VAB program and tone tables should build one playable region");
  const Region& region = instruments->instruments[0].regions[0];
  expect(std::abs(region.unityKey - 59.5) < 0.000001,
         "VAB shift should remain as fractional driver pitch instead of being rounded");
  expect(region.keyRange.low == 12 && region.keyRange.high == 100 &&
             region.envelope == psxSpuEnvelope(composePsxAdsr1(1, 0x70, 8, 8), composePsxAdsr2(1, 1, 0x40, 1, 0x10)),
         "VAB key ranges and native SPU ADSR registers should be retained");
  const SourceMap& combinedSources = combinedSnapshot.sourceMap();
  const auto programSources = combinedSources.ownedBy(ObjectRefs::instrument(instruments->metadata.id, 0));
  const auto toneSources = combinedSources.ownedBy(ObjectRefs::region(instruments->metadata.id, 0, 0));
  const SourceAnnotation* programSource =
      programSources.empty() ? nullptr : combinedSources.find(programSources.front());
  const SourceAnnotation* toneSource = toneSources.empty() ? nullptr : combinedSources.find(toneSources.front());
  expect(programSource != nullptr && programSource->localKind == "sony-ps1-program" &&
             programSource->fieldsAsChildren && toneSource != nullptr && toneSource->localKind == "sony-ps1-tone" &&
             toneSource->fieldsAsChildren,
         "VAB program and tone attributes should appear as selectable virtual children");

  Session split;
  split.registerFormat(sonyPs1Definition());
  split.addSource(SourceFile{.name = "BANK.VH", .path = "/fixture/BANK.VH"}, vabFixture(7, false));
  std::vector<u8> body(kFixtureVagSize, 0);
  fillVag(body, 0);
  split.addSource(SourceFile{.name = "BANK.VB", .path = "/fixture/BANK.VB"}, body);
  split.scanPendingSources();
  const SessionSnapshot splitSnapshot = split.snapshot();
  expect(splitSnapshot.collections().size() == 1 && splitSnapshot.collections()[0].members.instrumentSets.size() == 1 &&
             splitSnapshot.collections()[0].members.sampleCollections.size() == 1,
         "matching split VH and VB sources should resolve into one exportable synth collection");

  const auto bankBytes = vabFixture(7, true);
  const auto sequenceBytes = sequenceFixture({0x00, 0xff, 0x2f, 0x00});
  const u32 secondBank = static_cast<u32>(bankBytes.size() + 0x100);
  const u32 firstSequence = static_cast<u32>(secondBank + bankBytes.size() + 0x100);
  const u32 secondSequence = static_cast<u32>(firstSequence + sequenceBytes.size() + 0x100);
  std::vector<u8> pairedBytes(secondSequence + sequenceBytes.size(), 0);
  std::ranges::copy(bankBytes, pairedBytes.begin());
  std::ranges::copy(bankBytes, pairedBytes.begin() + secondBank);
  std::ranges::copy(sequenceBytes, pairedBytes.begin() + firstSequence);
  std::ranges::copy(sequenceBytes, pairedBytes.begin() + secondSequence);

  Session paired;
  paired.registerFormat(sonyPs1Definition());
  paired.addSource(SourceFile{.name = "paired.psf"}, std::move(pairedBytes));
  paired.scanPendingSources();
  const SessionSnapshot pairedSnapshot = paired.snapshot();
  const Collection* latestCollection = nullptr;
  const auto bankForSequence = [&](u32 sequenceOffset) {
    for (const auto& collection : pairedSnapshot.collections()) {
      const auto* sequence = pairedSnapshot.asset<SequenceProgramAsset>(*collection.members.sequence);
      if (sequence->metadata.range.offset == sequenceOffset) {
        if (sequenceOffset == secondSequence) {
          latestCollection = &collection;
        }
        expect(collection.members.instrumentSets.size() == 1 && collection.members.sampleCollections.size() == 1,
               "each same-source SonyPS1 sequence should resolve to one VAB pair");
        return pairedSnapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front())
            ->metadata.range.offset;
      }
    }
    return std::numeric_limits<u64>::max();
  };
  expect(bankForSequence(secondSequence) == secondBank && bankForSequence(firstSequence) == 0,
         "same-source SonyPS1 sequences and VABs should pair in descending offset order");
  const auto prepared = prepareSonyPs1Collection(CollectionPrepareContext{
      .sources = paired.sources(),
      .snapshot = pairedSnapshot,
      .collection = *latestCollection,
  });
  expect(prepared.replacementInstrumentSets && prepared.replacementInstrumentSets->size() == 1,
         "resolved SonyPS1 preparation should retain its selected VAB");
  const auto& instrument = prepared.replacementInstrumentSets->front().instruments.front();
  expect(instrument.explicitAddress && instrument.explicitAddress->bank == 0,
         "the selected VAB should be rebased from scan bank 1 to collection bank 0");
}
