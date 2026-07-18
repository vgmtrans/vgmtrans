/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/ParseCursor.h"
#include "value/scan/ScanTypes.h"
#include "value/model/SourceMap.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vgmtrans::core {

// These handles let scanners reserve asset IDs before the asset is parsed. That is
// useful when a sequence needs to refer to its instrument set, or an instrument
// set needs to refer to its sample collection.
struct ScanSequenceRef {
  AssetId id;
};

struct ScanInstrumentSetRef {
  AssetId id;
};

struct ScanSampleCollectionRef {
  AssetId id;
};

struct ScanMiscAssetRef {
  AssetId id;
};

class ScanResultBuilder;

// Builds one scanner-known collection. This is the common path when a format has
// already discovered the sequence, instruments, and samples together.
class ScanCollectionBuilder {
public:
  ScanCollectionBuilder(ScanResultBuilder& out, size_t index);

  ScanCollectionBuilder& sequence(ScanSequenceRef asset);
  ScanCollectionBuilder& instrumentSet(ScanInstrumentSetRef asset);
  ScanCollectionBuilder& samples(ScanSampleCollectionRef asset);
  ScanCollectionBuilder& misc(ScanMiscAssetRef asset);

private:
  ScanResultBuilder& out_;
  size_t index_ = 0;
};

class ScanSequenceAssetBuilder {
public:
  ScanSequenceAssetBuilder(ScanResultBuilder& out, ScanSequenceRef ref, std::string name, SourceRange range);

  ScanSequenceRef program(SequenceProgram program);

private:
  ScanResultBuilder& out_;
  ScanSequenceRef ref_;
  std::string name_;
  SourceRange range_;
};

class ScanInstrumentSetAssetBuilder {
public:
  ScanInstrumentSetAssetBuilder(ScanResultBuilder& out, ScanInstrumentSetRef ref, std::string name, SourceRange range);

  ScanInstrumentSetRef instruments(std::vector<Instrument> instruments);

private:
  ScanResultBuilder& out_;
  ScanInstrumentSetRef ref_;
  std::string name_;
  SourceRange range_;
};

class ScanSampleCollectionAssetBuilder {
public:
  ScanSampleCollectionAssetBuilder(ScanResultBuilder& out, ScanSampleCollectionRef ref, std::string name,
                                   SourceRange range);

  ScanSampleCollectionRef samples(SampleCollection samples);

private:
  ScanResultBuilder& out_;
  ScanSampleCollectionRef ref_;
  std::string name_;
  SourceRange range_;
};

class ScanMiscAssetBuilder {
public:
  ScanMiscAssetBuilder(ScanResultBuilder& out, ScanMiscAssetRef ref, std::string name, SourceRange range);

  ScanMiscAssetRef payload(std::vector<u8> payload);

private:
  ScanResultBuilder& out_;
  ScanMiscAssetRef ref_;
  std::string name_;
  SourceRange range_;
};

// Convenience wrapper for the normal scanner path. It still produces ordinary
// ScanResult values, but keeps format modules away from repetitive ID allocation,
// asset metadata setup, diagnostics, and simple collection-member facts.
class ScanResultBuilder {
public:
  ScanResultBuilder(const ScanInput& input, std::string format);
  ScanResultBuilder(const ScanInput& input, std::string format, std::string collectionResolver);

  [[nodiscard]] SourceId source() const noexcept { return input_.source.id; }
  [[nodiscard]] const SourceFile& sourceFile() const noexcept { return input_.source; }
  [[nodiscard]] ByteReader reader() const noexcept { return input_.reader; }
  [[nodiscard]] std::string_view format() const noexcept { return format_; }
  [[nodiscard]] ParseCursor cursor(SourceRange bounds);
  [[nodiscard]] SourceMapBuilder& sourceMap() noexcept { return sourceMap_; }
  [[nodiscard]] std::vector<Diagnostic>& diagnostics() noexcept { return result_.diagnostics; }

  [[nodiscard]] ScanSequenceRef reserveSequence();
  [[nodiscard]] ScanInstrumentSetRef reserveInstrumentSet();
  [[nodiscard]] ScanSampleCollectionRef reserveSampleCollection();
  [[nodiscard]] ScanMiscAssetRef reserveMisc();

  template <typename Factory>
  ScanSequenceRef sequence(Factory&& factory) {
    return sequence(reserveSequence(), std::forward<Factory>(factory));
  }

  template <typename Factory>
  ScanSequenceRef sequence(ScanSequenceRef ref, Factory&& factory) {
    auto asset = std::forward<Factory>(factory)(ref.id);
    addSequenceAsset(ref, std::move(asset));
    return ref;
  }

  [[nodiscard]] ScanSequenceAssetBuilder sequence(ScanSequenceRef ref, std::string name, SourceRange range);
  [[nodiscard]] ScanSequenceAssetBuilder sequence(std::string name, SourceRange range);

  template <typename Factory>
  ScanInstrumentSetRef instrumentSet(Factory&& factory) {
    return instrumentSet(reserveInstrumentSet(), std::forward<Factory>(factory));
  }

  template <typename Factory>
  ScanInstrumentSetRef instrumentSet(ScanInstrumentSetRef ref, Factory&& factory) {
    auto asset = std::forward<Factory>(factory)(ref.id);
    addInstrumentSetAsset(ref, std::move(asset));
    return ref;
  }

  [[nodiscard]] ScanInstrumentSetAssetBuilder instrumentSet(std::string name, SourceRange range);

  template <typename Factory>
  ScanSampleCollectionRef sampleCollection(Factory&& factory) {
    return sampleCollection(reserveSampleCollection(), std::forward<Factory>(factory));
  }

  template <typename Factory>
  ScanSampleCollectionRef sampleCollection(ScanSampleCollectionRef ref, Factory&& factory) {
    auto asset = std::forward<Factory>(factory)(ref.id);
    addSampleCollectionAsset(ref, std::move(asset));
    return ref;
  }

  [[nodiscard]] ScanSampleCollectionAssetBuilder sampleCollection(std::string name, SourceRange range);

  template <typename Factory>
  ScanMiscAssetRef misc(Factory&& factory) {
    return misc(reserveMisc(), std::forward<Factory>(factory));
  }

  template <typename Factory>
  ScanMiscAssetRef misc(ScanMiscAssetRef ref, Factory&& factory) {
    auto asset = std::forward<Factory>(factory)(ref.id);
    addMiscAsset(ref, std::move(asset));
    return ref;
  }

  [[nodiscard]] ScanMiscAssetBuilder misc(std::string name, SourceRange range);

  [[nodiscard]] ScanCollectionBuilder collection(std::string name);
  [[nodiscard]] ScanCollectionBuilder collection(std::string name, CollectionKey key);

  // Builds a region sample reference from a scanner handle and records that the
  // sample collection must be committed before finish().
  [[nodiscard]] SampleRef sampleRef(ScanSampleCollectionRef collection, u32 index);
  [[nodiscard]] SampleRef sampleRef(std::optional<ScanSampleCollectionRef> collection, u32 index);

  void fact(AssetId asset, MatchScope scope, MatchFactPayload payload);
  void sourceFact(AssetId asset, MatchFactPayload payload);
  void sessionFact(AssetId asset, MatchFactPayload payload);
  void collectionMember(AssetId asset, CollectionKey key, std::string collectionName, CollectionMemberRole role);

  void diagnostic(Diagnostic diagnostic);
  void warning(std::string message, SourceRange range);
  void error(std::string message, SourceRange range);
  void extractedSource(ExtractedSource source);

  [[nodiscard]] ScanResult finish();

private:
  friend class ScanCollectionBuilder;
  friend class ScanSequenceAssetBuilder;
  friend class ScanInstrumentSetAssetBuilder;
  friend class ScanSampleCollectionAssetBuilder;
  friend class ScanMiscAssetBuilder;

  [[nodiscard]] AssetMetadata metadata(AssetId id, std::string name, SourceRange range) const;
  [[nodiscard]] CollectionKey defaultCollectionKey(std::string_view name) const;
  [[nodiscard]] ExplicitCollection& explicitCollection(size_t index);

  void reserveHandle(AssetId id, CollectionMemberRole role);
  void markCommitted(AssetId id, CollectionMemberRole role);
  void markReferenced(AssetId id, CollectionMemberRole role);
  void validateReferencedHandles() const;

  void addSequenceAsset(ScanSequenceRef ref, SequenceProgramAsset asset);
  void addInstrumentSetAsset(ScanInstrumentSetRef ref, InstrumentSetAsset asset);
  void addSampleCollectionAsset(ScanSampleCollectionRef ref, SampleCollectionAsset asset);
  void addMiscAsset(ScanMiscAssetRef ref, MiscAsset asset);

  void prepareAsset(Asset& asset, AssetId expectedId) const;

  const ScanInput& input_;
  std::string format_;
  std::string collectionResolver_;
  ScanResult result_;
  SourceMapBuilder sourceMap_;

  struct HandleState {
    CollectionMemberRole role = CollectionMemberRole::Misc;
    bool committed = false;
    bool referenced = false;
  };
  std::unordered_map<u32, HandleState> handles_;
};

}  // namespace vgmtrans::core
