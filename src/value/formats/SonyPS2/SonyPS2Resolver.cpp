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

constexpr int kNoAffinity = -2;

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
  if (left != nullptr && right != nullptr && left->parent != right->parent) {
    return kNoAffinity;
  }
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

[[nodiscard]] bool compatible(const SoundBankAsset& bank, const SoundBankData& bankData,
                              const SampleBodyData& body) {
  if (!body.source) {
    return false;
  }
  const BodyAddressing addressing = bodyAddressing(bankData, body);
  return std::ranges::all_of(bank.instruments, [&](const Instrument& instrument) {
    return std::ranges::all_of(instrument.regions, [&](const Region& region) {
      if (!region.sample.needsBinding()) {
        return true;
      }
      const u32 index = region.sample.index();
      if (index >= bankData.vags.size() || !bankData.vags[index]) {
        return false;
      }
      const u32 logicalOffset = bankData.vags[index]->bodyOffset;
      return (logicalOffset & (kPsxAdpcmBlockBytes - 1)) == 0 && addressing.physicalOffset(logicalOffset) < body.bytes;
    });
  });
}

[[nodiscard]] std::vector<const BodyEntry*> chooseBodies(const BankEntry& bank, const std::vector<BodyEntry>& bodies) {
  std::vector<const BodyEntry*> selected;
  int best = -1;
  for (const auto& body : bodies) {
    if (!compatible(*bank.asset, *bank.data, *body.data)) {
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
  if (best < 4 && selected.size() != 1) {
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

struct BoundSample {
  SampleRef reference;
  bool loops = false;
};

class BodyBinder {
public:
  BodyBinder(CollectionBindingContext& context, SoundBankAsset& bank, const SamplePoolAsset& body,
             const SoundBankData& bankData, const SampleBodyData& bodyData, BodyAddressing addressing)
      : context_(context), bank_(bank), body_(body), bankData_(bankData), bodyData_(bodyData), addressing_(addressing) {
  }

  [[nodiscard]] bool bind(Region& region) {
    if (!region.sample.needsBinding()) {
      return true;
    }
    const u32 vagIndex = region.sample.index();
    if (vagIndex >= bankData_.vags.size() || !bankData_.vags[vagIndex]) {
      context_.fail("SonyPS2 region refers outside the sparse Vagi table", region.range);
      return false;
    }
    const VagInfo& vag = *bankData_.vags[vagIndex];
    const auto sample = resolve(addressing_.physicalOffset(vag.bodyOffset), vag.bodyOffset, region.range);
    if (!sample) {
      return false;
    }
    region.sample = sample->reference;
    if (vag.loops != sample->loops) {
      context_.warning("SonyPS2 Vagi loop attribute disagrees with the ADPCM end flags", region.range);
    }
    return true;
  }

private:
  [[nodiscard]] std::optional<BoundSample> resolve(u32 bodyOffset, u32 logicalOffset, SourceRange range) {
    const auto entry = std::ranges::find(bodyData_.entries, bodyOffset, &SampleBodyData::Entry::bodyOffset);
    if (entry != bodyData_.entries.end()) {
      const auto& sample = body_.pool.samples[entry->sampleIndex];
      return BoundSample{
          .reference = SampleRef::resolved(body_.metadata.id, entry->sampleIndex),
          .loops = sample.loop.enabled,
      };
    }

    auto [sample, inserted] = localSamples_.try_emplace(bodyOffset, 0);
    if (inserted && !addLocalSample(sample->second, bodyOffset, logicalOffset, range)) {
      localSamples_.erase(sample);
      return std::nullopt;
    }
    const auto& local = bank_.localSamples.samples[sample->second];
    return BoundSample{
        .reference = SampleRef::resolved(bank_.metadata.id, sample->second),
        .loops = local.loop.enabled,
    };
  }

  [[nodiscard]] bool addLocalSample(u32& index, u32 bodyOffset, u32 logicalOffset, SourceRange range) {
    const ByteReader reader = bodyData_.source.reader();
    const u32 boundary = sampleBoundary(bankData_, addressing_, bodyOffset, bodyData_.bytes);
    const auto stream = inspectPsxAdpcmStream(reader, bodyOffset, boundary);
    const bool completeEndpoint =
        stream && stream->encodedData.size >= kPsxAdpcmBlockBytes &&
        (reader.u8At(bodyOffset + stream->encodedData.size - kPsxAdpcmBlockBytes + 1) & 1) != 0;
    const u32 partialBlock = boundary & ~(kPsxAdpcmBlockBytes - 1);
    const bool truncatedEndpoint = stream && boundary == bodyData_.bytes && boundary - partialBlock >= 2 &&
                                   partialBlock >= bodyOffset && (reader.u8At(partialBlock + 1) & 1) != 0;
    if (!completeEndpoint && !truncatedEndpoint) {
      context_.fail(
          fmt::format("SonyPS2 Vagi entry at {:#x} has no ADPCM endpoint before the next BD waveform", logicalOffset),
          range);
      return false;
    }
    Loop loop = stream->loop;
    if (truncatedEndpoint) {
      loop.enabled = (reader.u8At(partialBlock + 1) & 2) != 0;
      context_.warning("SonyPS2 BD ends inside its final ADPCM block; the incomplete block was omitted",
                       stream->encodedData);
    }
    index = static_cast<u32>(bank_.localSamples.samples.size());
    bank_.localSamples.samples.push_back(Sample{
        .name = fmt::format("VAG at {:#x}", bodyOffset),
        .codec = AudioCodec::PsxAdpcm,
        .encodedData = stream->encodedData,
        .sampleRate = kPs2SpuSampleRate,
        .channels = 1,
        .bitsPerSample = 16,
        .loop = loop,
    });
    return true;
  }

  CollectionBindingContext& context_;
  SoundBankAsset& bank_;
  const SamplePoolAsset& body_;
  const SoundBankData& bankData_;
  const SampleBodyData& bodyData_;
  BodyAddressing addressing_;
  std::unordered_map<u32, u32> localSamples_;
};

[[nodiscard]] bool bindBody(CollectionBindingContext& context, const SampleBinding& binding) {
  auto* bank = context.soundBank(binding.bank);
  const auto* body = context.samplePool(binding.body);
  if (bank == nullptr || body == nullptr) {
    context.fail("SonyPS2 collection contains an invalid HD/BD binding");
    return false;
  }
  const auto* bankData = bank->privateData.get<SoundBankData>();
  const auto* bodyData = body->privateData.get<SampleBodyData>();
  if (bankData == nullptr || bodyData == nullptr) {
    context.fail("SonyPS2 HD/BD binding metadata is missing", bank->metadata.range);
    return false;
  }
  const BodyAddressing addressing = bodyAddressing(*bankData, *bodyData);
  if (addressing.omittedLeadingBlock) {
    // Some PSF2 rips discarded the bank's initial silent block but kept the
    // original Vagi addresses. Translate those logical addresses rather than
    // manufacturing a padded source.
    context.warning("SonyPS2 BD omits its initial silent ADPCM block; Vagi offsets were shifted by 16 bytes",
                    body->metadata.range);
  }
  const u32 sizeDifference = bankData->expectedBodyBytes > bodyData->bytes
                                 ? bankData->expectedBodyBytes - bodyData->bytes
                                 : bodyData->bytes - bankData->expectedBodyBytes;
  if (sizeDifference > 32) {
    // Shipped banks can retain an unrelated allocation size. sceHSyn_Load
    // receives the uploaded body base and resolves samples through Vagi offsets.
    context.warning("SonyPS2 HD bodySize differs from the selected BD; VAG offsets were used for binding",
                    bank->metadata.range);
  }

  BodyBinder binder(context, *bank, *body, *bankData, *bodyData, addressing);
  for (auto& instrument : bank->instruments) {
    for (auto& region : instrument.regions) {
      if (!binder.bind(region)) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool assignProgramAddresses(CollectionBindingContext& context,
                                          std::vector<ProgramRuntimeInfo>& runtimePrograms) {
  u16 bankNumber = 0;
  for (auto& bank : context.soundBanks) {
    if (bank.metadata.format != kFormatName) {
      continue;
    }
    const auto* data = bank.privateData.get<SoundBankData>();
    if (data == nullptr) {
      context.fail("SonyPS2 HD is missing retained Vagi binding data", bank.metadata.range);
      return false;
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
  return true;
}

void applyBindings(CollectionBindingContext& context, const std::vector<SampleBinding>& bindings) {
  std::vector<ProgramRuntimeInfo> runtimePrograms;
  if (!assignProgramAddresses(context, runtimePrograms)) {
    return;
  }

  for (const auto& binding : bindings) {
    if (!bindBody(context, binding)) {
      return;
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
  std::vector<SampleBinding> bindings;
  for (const auto& bank : context.soundBanks) {
    if (bank.metadata.format != kFormatName) {
      continue;
    }
    const auto* bankData = bank.privateData.get<SoundBankData>();
    if (bankData == nullptr) {
      context.fail("SonyPS2 HD is missing retained Vagi binding data", bank.metadata.range);
      return;
    }
    const SamplePoolAsset* selected = nullptr;
    for (const auto* body : context.samplePools) {
      const auto* bodyData = body->privateData.get<SampleBodyData>();
      if (body->metadata.format != kFormatName || bodyData == nullptr || !compatible(bank, *bankData, *bodyData)) {
        continue;
      }
      if (selected != nullptr) {
        context.fail("SonyPS2 HD matches multiple compatible BD sample bodies", bank.metadata.range);
        return;
      }
      selected = body;
    }
    if (selected == nullptr) {
      context.fail("SonyPS2 HD has no compatible BD sample body", bank.metadata.range);
      return;
    }
    bindings.push_back(SampleBinding{.bank = bank.metadata.id, .body = selected->metadata.id});
  }
  applyBindings(context, bindings);
}

}  // namespace vgmtrans::formats::sony_ps2
