/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS2/SonyPS2.h"

#include "value/scan/AssetResolution.h"
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

using BankEntry = AssetWithData<SoundBankAsset, SoundBankData>;
using BodyEntry = AssetWithData<SamplePoolAsset, SampleBodyData>;

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

[[nodiscard]] bool compatible(const SoundBankAsset& bank, const SoundBankData& bankData, const SampleBodyData& body) {
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

struct BoundSample {
  SampleRef reference;
  bool loops = false;
};

class BodyBinder {
public:
  BodyBinder(BankPreparationContext& context, SoundBankAsset& bank, const SamplePoolAsset& body,
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
        .loop = loop,
    });
    return true;
  }

  BankPreparationContext& context_;
  SoundBankAsset& bank_;
  const SamplePoolAsset& body_;
  const SoundBankData& bankData_;
  const SampleBodyData& bodyData_;
  BodyAddressing addressing_;
  std::unordered_map<u32, u32> localSamples_;
};

void bindBody(BankPreparationContext& context, const SoundBankData& data, const SampleInput<SampleBodyData>& input) {
  auto* bank = &context.bank;
  const auto* body = &input.asset;
  const auto* bankData = &data;
  const auto* bodyData = &input.data;
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
        return;
      }
    }
  }
}

}  // namespace

DependencySelection BankRequest::operator()(const DependencyContext& context) const {
  const auto banks = context.candidates<SoundBankAsset, SoundBankData>();
  if (!member.empty()) {
    const auto matches = bestMatches(banks, [&](const BankEntry& bank) {
      return context.source() != nullptr && bank.source != nullptr && context.source()->parent == bank.source->parent &&
                     selectedMember(*bank.source, member)
                 ? 0
                 : -1;
    });
    return selectOne(matches);
  }
  auto matches = bestMatches(banks, [&](const BankEntry& bank) { return affinity(context.source(), bank.source); });
  if (matches.size() > 1 && affinity(context.source(), matches.front()->source) < 4) {
    matches.clear();
  }
  auto result = selectAll(matches);
  if (matches.size() > 1) {
    result.ambiguous(dependencyTargets(matches), "SonyPS2 SQ matches multiple HD banks with equal source affinity");
  }
  return result;
}

DependencySelection selectSonyPs2Samples(const DependencyContext& context) {
  const auto& bank = *context.catalog().asset<SoundBankAsset>(context.metadata().id);
  const auto& data = context.data<SoundBankData>();
  const auto bodies = context.candidates<SamplePoolAsset, SampleBodyData>();
  return selectOne(bestMatches(bodies, [&](const BodyEntry& body) {
    if (!compatible(bank, data, *body.data)) {
      return kNoAffinity;
    }
    if (context.manual()) {
      return 0;
    }
    if (!data.sampleBodyMember.empty()) {
      return context.source() != nullptr && body.source != nullptr && context.source()->parent == body.source->parent &&
                     selectedMember(*body.source, data.sampleBodyMember)
                 ? 0
                 : -1;
    }
    return affinity(context.source(), body.source);
  }));
}

void prepareSonyPs2Bank(BankPreparationContext& context, const SoundBankData& data) {
  for (auto& instrument : context.bank.instruments) {
    if (!instrument.identity || instrument.identity->domain != kInstrumentDomain) {
      continue;
    }
    const u32 program = instrument.identity->key & 0xff;
    instrument.explicitAddress = InstrumentAddress{.bank = context.bankIndex, .program = program};
    instrument.identity = instrumentIdentity(static_cast<u16>(context.bankIndex), static_cast<u8>(program));
  }
  const auto bodies = context.samples<SampleBodyData>();
  if (bodies.size() != 1) {
    context.fail("SonyPS2 HD has no unambiguous compatible BD sample body");
    return;
  }
  bindBody(context, data, bodies.front());
}

void prepareSonyPs2Sequence(SequencePreparationContext& context) {
  std::vector<ProgramRuntimeInfo> programs;
  u32 bankNumber = 0;
  for (const auto& bank : context.banks<SoundBankData>(kFormatName)) {
    for (auto program : bank.data.runtimePrograms) {
      program.bank = static_cast<u8>(std::min<u32>(bankNumber, 255));
      programs.push_back(program);
    }
    ++bankNumber;
  }
  static_cast<void>(context.replaceSequenceRuntime(sequenceRuntime(RuntimeConfig{.programs = std::move(programs)})));
}

}  // namespace vgmtrans::formats::sony_ps2
