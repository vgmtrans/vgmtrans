/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS2/SonyPS2.h"

#include "value/scan/CollectionDiscovery.h"
#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vgmtrans::formats::sony_ps2 {

using namespace core;

namespace {

using SequenceEntry = AssetWithData<SequenceProgramAsset, SequenceData>;
using BankEntry = AssetWithData<SoundBankAsset, SoundBankData>;
using BodyEntry = AssetWithData<SamplePoolAsset, SampleBodyData>;

struct SampleBinding {
  AssetId bank;
  AssetId body;
};

struct BodyAddressing {
  bool omittedLeadingBlock = false;

  [[nodiscard]] u32 physicalOffset(u32 logicalOffset) const {
    return omittedLeadingBlock && logicalOffset >= kPsxAdpcmBlockBytes ? logicalOffset - kPsxAdpcmBlockBytes
                                                                       : logicalOffset;
  }
};

[[nodiscard]] std::filesystem::path path(const SourceFile* source) {
  if (source == nullptr) {
    return {};
  }
  if (const auto member = source->attribute("container-member")) {
    return *member;
  }
  if (source->derived() && !source->name.empty()) {
    return source->name;
  }
  return source->path.empty() ? std::filesystem::path(source->name) : source->path;
}

[[nodiscard]] int affinity(const SourceFile* left, const SourceFile* right) {
  const auto a = path(left);
  const auto b = path(right);
  if (a.empty() || b.empty()) {
    return 0;
  }
  if (a.parent_path() == b.parent_path() && a.stem() == b.stem()) {
    return 8;
  }
  if (left->parent && right->parent && left->parent == right->parent && a.stem() == b.stem()) {
    return 6;
  }
  if (a.parent_path() == b.parent_path()) {
    return 4;
  }
  if (left->parent && right->parent && left->parent == right->parent) {
    return 2;
  }
  return left->id == right->id ? 1 : 0;
}

[[nodiscard]] BodyAddressing bodyAddressing(const SoundBankData& bank, const SampleBodyData& body) {
  if (!body.source || body.bytes < kPsxAdpcmBlockBytes || bank.expectedBodyBytes <= body.bytes ||
      bank.expectedBodyBytes - body.bytes != kPsxAdpcmBlockBytes) {
    return {};
  }
  const ByteReader reader = body.source.reader();
  const bool startsWithSilence =
      std::ranges::all_of(reader.slice(0, kPsxAdpcmBlockBytes), [](u8 byte) { return byte == 0; });
  return {.omittedLeadingBlock = !startsWithSilence};
}

[[nodiscard]] bool compatible(const BankEntry& bank, const SampleBodyData& body) {
  if (!body.source) {
    return false;
  }
  const BodyAddressing addressing = bodyAddressing(*bank.data, body);
  return std::ranges::all_of(bank.asset->instruments, [&](const Instrument& instrument) {
    return std::ranges::all_of(instrument.regions, [&](const Region& region) {
      if (!region.sample.needsBinding()) {
        return true;
      }
      const u32 index = region.sample.index();
      if (index >= bank.data->vags.size() || !bank.data->vags[index]) {
        return false;
      }
      const u32 logicalOffset = bank.data->vags[index]->bodyOffset;
      return (logicalOffset & (kPsxAdpcmBlockBytes - 1)) == 0 && addressing.physicalOffset(logicalOffset) < body.bytes;
    });
  });
}

[[nodiscard]] std::vector<const BodyEntry*> chooseBodies(const BankEntry& bank, const std::vector<BodyEntry>& bodies) {
  std::vector<const BodyEntry*> selected;
  int best = -1;
  for (const auto& body : bodies) {
    if (!compatible(bank, *body.data)) {
      continue;
    }
    const int score = affinity(bank.source, body.source);
    if (score > best) {
      selected.clear();
      best = score;
    }
    if (score == best) {
      selected.push_back(&body);
    }
  }
  return selected;
}

[[nodiscard]] u32 sampleBoundary(const SoundBankData& bank, BodyAddressing addressing, u32 bodyOffset, u32 bodyBytes) {
  u32 boundary = bodyBytes;
  for (const auto& vag : bank.vags) {
    if (vag) {
      const u32 candidate = addressing.physicalOffset(vag->bodyOffset);
      if (candidate > bodyOffset) {
        boundary = std::min(boundary, candidate);
      }
    }
  }
  return boundary;
}

[[nodiscard]] std::vector<const BankEntry*> chooseBanks(const SequenceEntry& sequence,
                                                        const std::vector<BankEntry>& banks) {
  std::vector<const BankEntry*> selected;
  int best = -1;
  for (const auto& bank : banks) {
    const int score = affinity(sequence.source, bank.source);
    if (score > best) {
      selected.clear();
      best = score;
    }
    if (score == best) {
      selected.push_back(&bank);
    }
  }
  if (best < 4 && banks.size() != 1) {
    selected.clear();
  }
  return selected;
}

void attachBank(CollectionAssembly& collection, const BankEntry& bank, const std::vector<BodyEntry>& bodies,
                std::vector<SampleBinding>& bindings) {
  collection.soundBank(bank.id());
  const auto matches = chooseBodies(bank, bodies);
  if (matches.size() == 1) {
    collection.samplePool(matches.front()->id());
    bindings.push_back(SampleBinding{.bank = bank.id(), .body = matches.front()->id()});
  } else if (matches.empty()) {
    collection.incomplete(CollectionIssue{
        .severity = Severity::Warning,
        .code = "missing-sample-body",
        .message = "SonyPS2 HD has no compatible BD sample body",
        .asset = bank.id(),
    });
  } else {
    collection.ambiguous("SonyPS2 HD matches multiple compatible BD sample bodies", bank.id());
  }
}

void applyBindings(CollectionBindingContext& context, const std::vector<SampleBinding>& bindings) {
  std::vector<ProgramRuntimeInfo> runtimePrograms;
  u16 bankNumber = 0;
  for (auto& bank : context.soundBanks) {
    if (bank.metadata.format != kFormatName) {
      continue;
    }
    const auto* data = bank.privateData.get<SoundBankData>();
    if (data == nullptr) {
      context.fail("SonyPS2 HD is missing retained Vagi binding data", bank.metadata.range);
      return;
    }
    for (auto& instrument : bank.instruments) {
      if (!instrument.identity || instrument.identity->domain != kInstrumentDomain) {
        continue;
      }
      const u32 program = instrument.identity->key & 0xff;
      instrument.explicitAddress = InstrumentAddress{.bank = bankNumber, .program = program};
      instrument.identity = instrumentIdentity(bankNumber, static_cast<u8>(program));
    }
    for (auto program : data->runtimePrograms) {
      program.bank = static_cast<u8>(std::min<u16>(bankNumber, 255));
      runtimePrograms.push_back(program);
    }
    ++bankNumber;
  }

  for (const auto& binding : bindings) {
    auto* bank = context.soundBank(binding.bank);
    const auto* body = context.samplePool(binding.body);
    if (bank == nullptr || body == nullptr) {
      context.fail("SonyPS2 collection contains an invalid HD/BD binding");
      return;
    }
    const auto* bankData = bank->privateData.get<SoundBankData>();
    const auto* bodyData = body->privateData.get<SampleBodyData>();
    if (bankData == nullptr || bodyData == nullptr) {
      context.fail("SonyPS2 HD/BD binding metadata is missing", bank->metadata.range);
      return;
    }
    const BodyAddressing addressing = bodyAddressing(*bankData, *bodyData);
    if (addressing.omittedLeadingBlock) {
      // Some PSF2 rips discarded the bank's initial silent block but kept
      // the original Vagi addresses. Keep source ranges physical and translate
      // those logical addresses instead of manufacturing a padded source.
      context.warning("SonyPS2 BD omits its initial silent ADPCM block; Vagi offsets were shifted by 16 bytes",
                      body->metadata.range);
    }
    const u32 sizeDifference = bankData->expectedBodyBytes > bodyData->bytes
                                   ? bankData->expectedBodyBytes - bodyData->bytes
                                   : bodyData->bytes - bankData->expectedBodyBytes;
    if (sizeDifference > 32) {
      // The manual describes bodySize as the complete BD length, but shipped
      // banks can retain an unrelated allocation size. sceHSyn_Load receives
      // only the uploaded body base and resolves samples through Vagi offsets.
      context.warning("SonyPS2 HD bodySize differs from the selected BD; VAG offsets were used for binding",
                      bank->metadata.range);
    }
    std::unordered_map<u32, u32> localSamples;
    for (auto& instrument : bank->instruments) {
      for (auto& region : instrument.regions) {
        if (!region.sample.needsBinding()) {
          continue;
        }
        const u32 vagIndex = region.sample.index();
        if (vagIndex >= bankData->vags.size() || !bankData->vags[vagIndex]) {
          context.fail("SonyPS2 region refers outside the sparse Vagi table", region.range);
          return;
        }
        const u32 logicalOffset = bankData->vags[vagIndex]->bodyOffset;
        const u32 bodyOffset = addressing.physicalOffset(logicalOffset);
        const auto entry = std::ranges::find(bodyData->entries, bodyOffset, &SampleBodyData::Entry::bodyOffset);
        bool loops = false;
        if (entry != bodyData->entries.end()) {
          region.sample = SampleRef::resolved(body->metadata.id, entry->sampleIndex);
          loops = body->pool.samples[entry->sampleIndex].loop.enabled;
        } else {
          auto [sample, inserted] = localSamples.try_emplace(bodyOffset, 0);
          if (inserted) {
            const ByteReader reader = bodyData->source.reader();
            const u32 boundary = sampleBoundary(*bankData, addressing, bodyOffset, bodyData->bytes);
            const auto stream = inspectPsxAdpcmStream(reader, bodyOffset, boundary);
            const bool completeEndpoint = stream && stream->encodedData.size >= kPsxAdpcmBlockBytes &&
                                          (reader.u8At(bodyOffset + stream->encodedData.size -
                                                       kPsxAdpcmBlockBytes + 1) &
                                           1) != 0;
            const u32 partialBlock = boundary & ~(kPsxAdpcmBlockBytes - 1);
            const bool truncatedEndpoint = stream && boundary == bodyData->bytes && boundary - partialBlock >= 2 &&
                                           partialBlock >= bodyOffset && (reader.u8At(partialBlock + 1) & 1) != 0;
            if (!completeEndpoint && !truncatedEndpoint) {
              context.fail(fmt::format("SonyPS2 Vagi entry at {:#x} has no ADPCM endpoint before the next BD "
                                       "waveform",
                                       logicalOffset),
                           region.range);
              return;
            }
            Loop loop = stream->loop;
            if (truncatedEndpoint) {
              loop.enabled = (reader.u8At(partialBlock + 1) & 2) != 0;
              context.warning("SonyPS2 BD ends inside its final ADPCM block; the incomplete block was omitted",
                              stream->encodedData);
            }
            sample->second = static_cast<u32>(bank->localSamples.samples.size());
            bank->localSamples.samples.push_back(Sample{
                .name = fmt::format("VAG at {:#x}", bodyOffset),
                .codec = AudioCodec::PsxAdpcm,
                .encodedData = stream->encodedData,
                .sampleRate = kPs2SpuSampleRate,
                .channels = 1,
                .bitsPerSample = 16,
                .loop = loop,
            });
          }
          region.sample = SampleRef::resolved(bank->metadata.id, sample->second);
          loops = bank->localSamples.samples[sample->second].loop.enabled;
        }
        if (bankData->vags[vagIndex]->loops != loops) {
          context.warning("SonyPS2 Vagi loop attribute disagrees with the ADPCM end flags", region.range);
        }
      }
    }
  }

  if (context.sequence != nullptr && context.sequence->metadata.format == kFormatName) {
    static_cast<void>(
        context.replaceSequenceRuntime(sequenceRuntime(RuntimeConfig{.programs = std::move(runtimePrograms)})));
  }
}

[[nodiscard]] CollectionBinder binder(std::vector<SampleBinding> bindings) {
  return [bindings = std::move(bindings)](CollectionBindingContext& context) { applyBindings(context, bindings); };
}

}  // namespace

std::vector<DesiredCollection> resolveCollections(const CollectionDiscoveryContext& context) {
  const auto sequences = context.assetsWithData<SequenceProgramAsset, SequenceData>();
  const auto banks = context.assetsWithData<SoundBankAsset, SoundBankData>();
  const auto bodies = context.assetsWithData<SamplePoolAsset, SampleBodyData>();
  std::vector<DesiredCollection> collections;
  std::unordered_set<u32> pairedBanks;
  for (const auto& sequence : sequences) {
    CollectionAssembly collection(
        "source:" + std::to_string(sequence.source == nullptr ? 0 : sequence.source->id.value) +
            ":sequence:" + std::to_string(sequence.asset->metadata.id.value),
        sequence.asset->metadata.name);
    collection.sequence(sequence.id());
    std::vector<SampleBinding> bindings;
    const auto matches = chooseBanks(sequence, banks);
    for (const auto* bank : matches) {
      attachBank(collection, *bank, bodies, bindings);
      pairedBanks.insert(bank->id().value);
    }
    if (matches.empty()) {
      collection.requireSoundBank();
    } else if (matches.size() > 1) {
      collection.ambiguous("SonyPS2 SQ matches multiple HD banks with equal source affinity", sequence.id());
    }
    collection.bind(binder(std::move(bindings)));
    collections.push_back(std::move(collection).finish());
  }
  for (const auto& bank : banks) {
    if (pairedBanks.contains(bank.id().value)) {
      continue;
    }
    CollectionAssembly collection("source:" + std::to_string(bank.source == nullptr ? 0 : bank.source->id.value) +
                                      ":bank:" + std::to_string(bank.asset->metadata.id.value),
                                  bank.asset->metadata.name);
    std::vector<SampleBinding> bindings;
    attachBank(collection, bank, bodies, bindings);
    collection.bind(binder(std::move(bindings)));
    collections.push_back(std::move(collection).finish());
  }
  return collections;
}

void bindCollection(CollectionBindingContext& context) {
  applyBindings(context, {});
}

}  // namespace vgmtrans::formats::sony_ps2
