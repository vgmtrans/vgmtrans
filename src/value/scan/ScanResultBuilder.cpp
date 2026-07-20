/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/scan/ScanResultBuilder.h"

#include <filesystem>
#include <stdexcept>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::string namedSourceCollectionKey(SourceId source, std::string_view name) {
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

[[nodiscard]] std::string roleName(CollectionMemberRole role) {
  switch (role) {
    case CollectionMemberRole::Sequence:
      return "sequence";
    case CollectionMemberRole::InstrumentSet:
      return "instrument-set";
    case CollectionMemberRole::SampleCollection:
      return "sample-collection";
    case CollectionMemberRole::Misc:
      return "misc";
  }
}

}  // namespace

ScanCollectionBuilder::ScanCollectionBuilder(ScanResultBuilder& out, size_t index) : out_(out), index_(index) {
}

ScanCollectionBuilder& ScanCollectionBuilder::sequence(ScanSequenceRef asset) {
  out_.markReferenced(asset.id, CollectionMemberRole::Sequence);
  out_.explicitCollection(index_).sequence = asset.id;
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::instrumentSet(ScanInstrumentSetRef asset) {
  out_.markReferenced(asset.id, CollectionMemberRole::InstrumentSet);
  out_.explicitCollection(index_).instrumentSets.push_back(asset.id);
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::samples(ScanSampleCollectionRef asset) {
  out_.markReferenced(asset.id, CollectionMemberRole::SampleCollection);
  out_.explicitCollection(index_).sampleCollections.push_back(asset.id);
  return *this;
}

ScanCollectionBuilder& ScanCollectionBuilder::misc(ScanMiscAssetRef asset) {
  out_.markReferenced(asset.id, CollectionMemberRole::Misc);
  out_.explicitCollection(index_).miscAssets.push_back(asset.id);
  return *this;
}

ScanSequenceAssetBuilder::ScanSequenceAssetBuilder(ScanResultBuilder& out, ScanSequenceRef ref, std::string name,
                                                   SourceRange range)
    : out_(out), ref_(ref), name_(std::move(name)), range_(range) {
}

ScanSequenceRef ScanSequenceAssetBuilder::program(SequenceProgram program) {
  out_.addSequenceAsset(ref_, SequenceProgramAsset{
                                  .metadata = out_.metadata(ref_.id, std::move(name_), range_),
                                  .program = std::move(program),
                              });
  return ref_;
}

ScanInstrumentSetAssetBuilder::ScanInstrumentSetAssetBuilder(ScanResultBuilder& out, ScanInstrumentSetRef ref,
                                                             std::string name, SourceRange range)
    : out_(out), ref_(ref), name_(std::move(name)), range_(range) {
}

ScanInstrumentSetRef ScanInstrumentSetAssetBuilder::instruments(std::vector<Instrument> instruments) {
  out_.addInstrumentSetAsset(ref_, InstrumentSetAsset{
                                       .metadata = out_.metadata(ref_.id, std::move(name_), range_),
                                       .instruments = std::move(instruments),
                                   });
  return ref_;
}

ScanSampleCollectionAssetBuilder::ScanSampleCollectionAssetBuilder(ScanResultBuilder& out, ScanSampleCollectionRef ref,
                                                                   std::string name, SourceRange range)
    : out_(out), ref_(ref), name_(std::move(name)), range_(range) {
}

ScanSampleCollectionRef ScanSampleCollectionAssetBuilder::samples(SampleCollection samples) {
  out_.addSampleCollectionAsset(ref_, SampleCollectionAsset{
                                          .metadata = out_.metadata(ref_.id, std::move(name_), range_),
                                          .samples = std::move(samples),
                                      });
  return ref_;
}

ScanMiscAssetBuilder::ScanMiscAssetBuilder(ScanResultBuilder& out, ScanMiscAssetRef ref, std::string name,
                                           SourceRange range)
    : out_(out), ref_(ref), name_(std::move(name)), range_(range) {
}

ScanMiscAssetRef ScanMiscAssetBuilder::payload(std::vector<u8> payload) {
  out_.addMiscAsset(ref_, MiscAsset{
                              .metadata = out_.metadata(ref_.id, std::move(name_), range_),
                              .payload = std::move(payload),
                          });
  return ref_;
}

ScanResultBuilder::ScanResultBuilder(const ScanInput& input, std::string format)
    : ScanResultBuilder(input, std::move(format), {}) {
}

ScanResultBuilder::ScanResultBuilder(const ScanInput& input, std::string format, std::string collectionResolver)
    : input_(input), format_(std::move(format)),
      collectionResolver_(collectionResolver.empty() ? format_ : std::move(collectionResolver)),
      sourceMap_([this]() { return input_.ids.nextSourceAnnotationId(); }) {
}

std::string ScanResultBuilder::sourceDisplayName() const {
  if (input_.source.title && !input_.source.title->empty()) {
    return *input_.source.title;
  }
  if (!input_.source.name.empty()) {
    return std::filesystem::path(input_.source.name).stem().string();
  }
  if (!input_.source.path.empty()) {
    return input_.source.path.stem().string();
  }
  return format_;
}

ParseCursor ScanResultBuilder::cursor(SourceRange bounds) {
  return ParseCursor(input_.reader, bounds, result_.diagnostics);
}

ScanSequenceRef ScanResultBuilder::reserveSequence() {
  const auto id = input_.ids.nextAssetId();
  reserveHandle(id, CollectionMemberRole::Sequence);
  return ScanSequenceRef{.id = id};
}

ScanInstrumentSetRef ScanResultBuilder::reserveInstrumentSet() {
  const auto id = input_.ids.nextAssetId();
  reserveHandle(id, CollectionMemberRole::InstrumentSet);
  return ScanInstrumentSetRef{.id = id};
}

ScanSampleCollectionRef ScanResultBuilder::reserveSampleCollection() {
  const auto id = input_.ids.nextAssetId();
  reserveHandle(id, CollectionMemberRole::SampleCollection);
  return ScanSampleCollectionRef{.id = id};
}

ScanMiscAssetRef ScanResultBuilder::reserveMisc() {
  const auto id = input_.ids.nextAssetId();
  reserveHandle(id, CollectionMemberRole::Misc);
  return ScanMiscAssetRef{.id = id};
}

ScanSequenceAssetBuilder ScanResultBuilder::sequence(std::string name, SourceRange range) {
  return sequence(reserveSequence(), std::move(name), range);
}

ScanSequenceAssetBuilder ScanResultBuilder::sequence(ScanSequenceRef ref, std::string name, SourceRange range) {
  return ScanSequenceAssetBuilder(*this, ref, std::move(name), range);
}

ScanInstrumentSetAssetBuilder ScanResultBuilder::instrumentSet(std::string name, SourceRange range) {
  return instrumentSet(reserveInstrumentSet(), std::move(name), range);
}

ScanInstrumentSetAssetBuilder ScanResultBuilder::instrumentSet(ScanInstrumentSetRef ref, std::string name,
                                                               SourceRange range) {
  return ScanInstrumentSetAssetBuilder(*this, ref, std::move(name), range);
}

ScanInstrumentSetRef ScanResultBuilder::instrumentSet(std::string name, InstrumentSetBuilder&& instruments) {
  const ScanInstrumentSetRef ref{.id = instruments.assetId()};
  auto values = std::move(instruments).finish();
  // finish() also accounts for ranges supplied late through value(), so the
  // asset range must be read afterward.
  const SourceRange range = instruments.range();
  return instrumentSet(ref, std::move(name), range).instruments(std::move(values));
}

ScanSampleCollectionAssetBuilder ScanResultBuilder::sampleCollection(std::string name, SourceRange range) {
  return sampleCollection(reserveSampleCollection(), std::move(name), range);
}

ScanSampleCollectionAssetBuilder ScanResultBuilder::sampleCollection(ScanSampleCollectionRef ref, std::string name,
                                                                     SourceRange range) {
  return ScanSampleCollectionAssetBuilder(*this, ref, std::move(name), range);
}

ScanSampleCollectionRef ScanResultBuilder::sampleCollection(std::string name, SampleCollectionBuilder&& samples) {
  const ScanSampleCollectionRef ref{.id = samples.assetId()};
  auto lookup = samples.refs();
  auto values = std::move(samples).finish();
  // A format can fill encodedData through value(); finish() incorporates that
  // range before the enclosing asset is committed.
  const SourceRange range = samples.range();
  sampleLookups_.insert_or_assign(ref.id.value, std::move(lookup));
  return sampleCollection(ref, std::move(name), range).samples(std::move(values));
}

InstrumentSetBuilder ScanResultBuilder::instruments() {
  return instruments(reserveInstrumentSet());
}

InstrumentSetBuilder ScanResultBuilder::instruments(ScanInstrumentSetRef ref) {
  reserveHandle(ref.id, CollectionMemberRole::InstrumentSet);
  return InstrumentSetBuilder{ref.id, &sourceMap_, &result_.diagnostics};
}

SampleCollectionBuilder ScanResultBuilder::samples() {
  return samples(reserveSampleCollection());
}

SampleCollectionBuilder ScanResultBuilder::samples(ScanSampleCollectionRef ref) {
  reserveHandle(ref.id, CollectionMemberRole::SampleCollection);
  return SampleCollectionBuilder{ref.id, &sourceMap_, &result_.diagnostics};
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
    key.value = namedSourceCollectionKey(input_.source.id, name);
  }
  const size_t index = result_.explicitCollections.size();
  result_.explicitCollections.push_back(ExplicitCollection{
      .key = std::move(key),
      .name = std::move(name),
  });
  return ScanCollectionBuilder(*this, index);
}

ScanCollectionBuilder ScanResultBuilder::sourceCollection(std::string name) {
  CollectionKey key{
      .value = "source:" + std::to_string(input_.source.id.value),
  };
  return collection(std::move(name), std::move(key));
}

SampleRef ScanResultBuilder::sampleRef(ScanSampleCollectionRef collection, u32 index) {
  markReferenced(collection.id, CollectionMemberRole::SampleCollection);
  return SampleRef{
      .collection = collection.id,
      .index = index,
  };
}

SampleRef ScanResultBuilder::sampleRef(std::optional<ScanSampleCollectionRef> collection, u32 index) {
  if (collection) {
    return sampleRef(*collection, index);
  }
  return SampleRef{.index = index};
}

std::optional<SampleRef> ScanResultBuilder::sampleByKey(ScanSampleCollectionRef collection, u64 sourceKey) {
  const auto lookup = sampleLookups_.find(collection.id.value);
  if (lookup == sampleLookups_.end()) {
    return std::nullopt;
  }
  const auto sample = lookup->second.find(sourceKey);
  if (sample) {
    markReferenced(collection.id, CollectionMemberRole::SampleCollection);
  }
  return sample;
}

std::optional<SampleRef> ScanResultBuilder::sampleByKeyOrWarning(std::optional<ScanSampleCollectionRef> collection,
                                                                 u64 sourceKey, std::string description,
                                                                 SourceRange range) {
  if (collection) {
    if (const auto sample = sampleByKey(*collection, sourceKey)) {
      return sample;
    }
  }
  warning(std::move(description) + " was not found", range);
  return std::nullopt;
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
  validateReferencedHandles();
  result_.sourceMap = sourceMap_.finish();
  return std::move(result_);
}

AssetMetadata ScanResultBuilder::metadata(AssetId id, std::string name, SourceRange range) const {
  return AssetMetadata{
      .id = id,
      .format = format_,
      .name = std::move(name),
      .range = range,
  };
}

CollectionKey ScanResultBuilder::defaultCollectionKey(std::string_view name) const {
  return CollectionKey{
      .resolver = collectionResolver_,
      .value = namedSourceCollectionKey(input_.source.id, name),
  };
}

ExplicitCollection& ScanResultBuilder::explicitCollection(size_t index) {
  return result_.explicitCollections.at(index);
}

void ScanResultBuilder::reserveHandle(AssetId id, CollectionMemberRole role) {
  auto [found, inserted] = handles_.try_emplace(id.value, HandleState{.role = role});
  if (!inserted && found->second.role != role) {
    throw std::logic_error("ScanResultBuilder asset handle reused with a different role");
  }
}

void ScanResultBuilder::markCommitted(AssetId id, CollectionMemberRole role) {
  auto [found, inserted] = handles_.try_emplace(id.value, HandleState{.role = role});
  if (!inserted && found->second.role != role) {
    throw std::logic_error("ScanResultBuilder asset handle reused with a different role");
  }
  found->second.committed = true;
}

void ScanResultBuilder::markReferenced(AssetId id, CollectionMemberRole role) {
  auto [found, inserted] = handles_.try_emplace(id.value, HandleState{.role = role});
  if (!inserted && found->second.role != role) {
    throw std::logic_error("ScanResultBuilder collection referenced an asset handle with the wrong role");
  }
  found->second.referenced = true;
}

void ScanResultBuilder::validateReferencedHandles() const {
  for (const auto& [id, state] : handles_) {
    if (state.referenced && !state.committed) {
      throw std::logic_error("ScanResultBuilder collection referenced " + roleName(state.role) + " asset id " +
                             std::to_string(id) + " before it was added");
    }
  }
}

void ScanResultBuilder::addSequenceAsset(ScanSequenceRef ref, SequenceProgramAsset asset) {
  Asset variant = std::move(asset);
  prepareAsset(variant, ref.id);
  markCommitted(ref.id, CollectionMemberRole::Sequence);
  result_.assets.push_back(std::move(variant));
}

void ScanResultBuilder::addInstrumentSetAsset(ScanInstrumentSetRef ref, InstrumentSetAsset asset) {
  Asset variant = std::move(asset);
  prepareAsset(variant, ref.id);
  markCommitted(ref.id, CollectionMemberRole::InstrumentSet);
  result_.assets.push_back(std::move(variant));
}

void ScanResultBuilder::addSampleCollectionAsset(ScanSampleCollectionRef ref, SampleCollectionAsset asset) {
  Asset variant = std::move(asset);
  prepareAsset(variant, ref.id);
  markCommitted(ref.id, CollectionMemberRole::SampleCollection);
  result_.assets.push_back(std::move(variant));
}

void ScanResultBuilder::addMiscAsset(ScanMiscAssetRef ref, MiscAsset asset) {
  Asset variant = std::move(asset);
  prepareAsset(variant, ref.id);
  markCommitted(ref.id, CollectionMemberRole::Misc);
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
