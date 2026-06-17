/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ScanResultBuilder.h"

#include <stdexcept>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::string sourceCollectionKey(SourceId source, std::string_view name) {
  return "source:" + std::to_string(source.value) + ":collection:" + std::string(name);
}

void ensureAssetId(AssetMetadata& metadata, AssetId expectedId) {
  if (!metadata.id.valid()) {
    metadata.id = expectedId;
    return;
  }
  if (metadata.id != expectedId) {
    throw std::logic_error("ScanResultBuilder asset factory returned the wrong asset id");
  }
}

}  // namespace

ScanCollectionBuilder::ScanCollectionBuilder(ScanResultBuilder& out, CollectionKey key, std::string name)
    : out_(out), key_(std::move(key)), name_(std::move(name)) {
}

ScanCollectionBuilder& ScanCollectionBuilder::sequence(ScanSequenceRef asset) {
  out_.collectionMember(asset.id, key_, name_, CollectionMemberRole::Sequence);
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::instrumentSet(ScanInstrumentSetRef asset) {
  out_.collectionMember(asset.id, key_, name_, CollectionMemberRole::InstrumentSet);
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::samples(ScanSampleCollectionRef asset) {
  out_.collectionMember(asset.id, key_, name_, CollectionMemberRole::SampleCollection);
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::misc(ScanMiscAssetRef asset) {
  out_.collectionMember(asset.id, key_, name_, CollectionMemberRole::Misc);
  return *this;
}

ScanSequenceAssetBuilder::ScanSequenceAssetBuilder(ScanResultBuilder& out, ScanSequenceRef ref, std::string name,
                                                   SourceRange range)
    : out_(out), ref_(ref), name_(std::move(name)), range_(range) {
}

ScanSequenceRef ScanSequenceAssetBuilder::program(SequenceProgram program, ItemTree items) {
  out_.addSequenceAsset(ref_, SequenceProgramAsset{
                                  .metadata = out_.metadata(ref_.id, std::move(name_), range_, std::move(items)),
                                  .program = std::move(program),
                              });
  return ref_;
}

ScanInstrumentSetAssetBuilder::ScanInstrumentSetAssetBuilder(ScanResultBuilder& out, ScanInstrumentSetRef ref,
                                                             std::string name, SourceRange range)
    : out_(out), ref_(ref), name_(std::move(name)), range_(range) {
}

ScanInstrumentSetRef ScanInstrumentSetAssetBuilder::instruments(std::vector<Instrument> instruments, ItemTree items) {
  out_.addInstrumentSetAsset(ref_, InstrumentSetAsset{
                                       .metadata = out_.metadata(ref_.id, std::move(name_), range_, std::move(items)),
                                       .instruments = std::move(instruments),
                                   });
  return ref_;
}

ScanSampleCollectionAssetBuilder::ScanSampleCollectionAssetBuilder(ScanResultBuilder& out, ScanSampleCollectionRef ref,
                                                                   std::string name, SourceRange range)
    : out_(out), ref_(ref), name_(std::move(name)), range_(range) {
}

ScanSampleCollectionRef ScanSampleCollectionAssetBuilder::samples(SampleCollection samples, ItemTree items) {
  out_.addSampleCollectionAsset(ref_,
                                SampleCollectionAsset{
                                    .metadata = out_.metadata(ref_.id, std::move(name_), range_, std::move(items)),
                                    .samples = std::move(samples),
                                });
  return ref_;
}

ScanMiscAssetBuilder::ScanMiscAssetBuilder(ScanResultBuilder& out, ScanMiscAssetRef ref, std::string name,
                                           SourceRange range)
    : out_(out), ref_(ref), name_(std::move(name)), range_(range) {
}

ScanMiscAssetRef ScanMiscAssetBuilder::payload(std::vector<u8> payload, ItemTree items) {
  out_.addMiscAsset(ref_, MiscAsset{
                              .metadata = out_.metadata(ref_.id, std::move(name_), range_, std::move(items)),
                              .payload = std::move(payload),
                          });
  return ref_;
}

ScanResultBuilder::ScanResultBuilder(const ScanInput& input, std::string format)
    : ScanResultBuilder(input, std::move(format), {}) {
}

ScanResultBuilder::ScanResultBuilder(const ScanInput& input, std::string format, std::string collectionResolver)
    : input_(input), format_(std::move(format)),
      collectionResolver_(collectionResolver.empty() ? format_ : std::move(collectionResolver)) {
}

ScanSequenceRef ScanResultBuilder::reserveSequence() {
  return ScanSequenceRef{.id = input_.ids.nextAssetId()};
}

ScanInstrumentSetRef ScanResultBuilder::reserveInstrumentSet() {
  return ScanInstrumentSetRef{.id = input_.ids.nextAssetId()};
}

ScanSampleCollectionRef ScanResultBuilder::reserveSampleCollection() {
  return ScanSampleCollectionRef{.id = input_.ids.nextAssetId()};
}

ScanMiscAssetRef ScanResultBuilder::reserveMisc() {
  return ScanMiscAssetRef{.id = input_.ids.nextAssetId()};
}

ScanSequenceAssetBuilder ScanResultBuilder::sequence(std::string name, SourceRange range) {
  return ScanSequenceAssetBuilder(*this, reserveSequence(), std::move(name), range);
}

ScanInstrumentSetAssetBuilder ScanResultBuilder::instrumentSet(std::string name, SourceRange range) {
  return ScanInstrumentSetAssetBuilder(*this, reserveInstrumentSet(), std::move(name), range);
}

ScanSampleCollectionAssetBuilder ScanResultBuilder::sampleCollection(std::string name, SourceRange range) {
  return ScanSampleCollectionAssetBuilder(*this, reserveSampleCollection(), std::move(name), range);
}

ScanMiscAssetBuilder ScanResultBuilder::misc(std::string name, SourceRange range) {
  return ScanMiscAssetBuilder(*this, reserveMisc(), std::move(name), range);
}

ScanCollectionBuilder ScanResultBuilder::collection(std::string name) {
  return collection(name, defaultCollectionKey(name));
}

ScanCollectionBuilder ScanResultBuilder::collection(std::string name, CollectionKey key) {
  if (key.resolver.empty()) {
    key.resolver = collectionResolver_;
  }
  if (key.value.empty()) {
    key.value = sourceCollectionKey(input_.source.id, name);
  }
  return ScanCollectionBuilder(*this, std::move(key), std::move(name));
}

void ScanResultBuilder::fact(AssetId asset, MatchScope scope, MatchFactPayload payload) {
  result_.matchFacts.push_back(MatchFact{
      .asset = asset,
      .format = format_,
      .scope = std::move(scope),
      .payload = std::move(payload),
  });
}

void ScanResultBuilder::sourceFact(AssetId asset, MatchFactPayload payload) {
  fact(asset, MatchScope{.kind = MatchScopeKind::Source, .source = input_.source.id}, std::move(payload));
}

void ScanResultBuilder::sessionFact(AssetId asset, MatchFactPayload payload) {
  fact(asset, MatchScope{.kind = MatchScopeKind::Session}, std::move(payload));
}

void ScanResultBuilder::collectionMember(AssetId asset, CollectionKey key, std::string collectionName,
                                         CollectionMemberRole role) {
  sourceFact(asset, CollectionMemberFact{
                        .key = std::move(key),
                        .collectionName = std::move(collectionName),
                        .role = role,
                    });
}

void ScanResultBuilder::diagnostic(Diagnostic diagnostic) {
  result_.diagnostics.push_back(std::move(diagnostic));
}

void ScanResultBuilder::warning(std::string message, SourceRange range) {
  diagnostic(Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range});
}

void ScanResultBuilder::error(std::string message, SourceRange range) {
  diagnostic(Diagnostic{.severity = Severity::Error, .message = std::move(message), .range = range});
}

void ScanResultBuilder::extractedSource(ExtractedSource source) {
  result_.extractedSources.push_back(std::move(source));
}

ScanResult ScanResultBuilder::finish() {
  return std::move(result_);
}

AssetMetadata ScanResultBuilder::metadata(AssetId id, std::string name, SourceRange range, ItemTree items) const {
  return AssetMetadata{
      .id = id,
      .format = format_,
      .name = std::move(name),
      .range = range,
      .items = std::move(items),
  };
}

CollectionKey ScanResultBuilder::defaultCollectionKey(std::string_view name) const {
  return CollectionKey{
      .resolver = collectionResolver_,
      .value = sourceCollectionKey(input_.source.id, name),
  };
}

void ScanResultBuilder::addSequenceAsset(ScanSequenceRef ref, SequenceProgramAsset asset) {
  Asset variant = std::move(asset);
  prepareAsset(variant, ref.id);
  result_.assets.push_back(std::move(variant));
}

void ScanResultBuilder::addInstrumentSetAsset(ScanInstrumentSetRef ref, InstrumentSetAsset asset) {
  Asset variant = std::move(asset);
  prepareAsset(variant, ref.id);
  result_.assets.push_back(std::move(variant));
}

void ScanResultBuilder::addSampleCollectionAsset(ScanSampleCollectionRef ref, SampleCollectionAsset asset) {
  Asset variant = std::move(asset);
  prepareAsset(variant, ref.id);
  result_.assets.push_back(std::move(variant));
}

void ScanResultBuilder::addMiscAsset(ScanMiscAssetRef ref, MiscAsset asset) {
  Asset variant = std::move(asset);
  prepareAsset(variant, ref.id);
  result_.assets.push_back(std::move(variant));
}

void ScanResultBuilder::prepareAsset(Asset& asset, AssetId expectedId) const {
  auto& meta = vgmtrans::core::metadata(asset);
  ensureAssetId(meta, expectedId);
  if (meta.format.empty()) {
    meta.format = format_;
  }
}

}  // namespace vgmtrans::core
