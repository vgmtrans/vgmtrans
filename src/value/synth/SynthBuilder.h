/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SourceMap.h"
#include "value/synth/SynthModel.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vgmtrans::core {

// A read-only copy of a sample builder's source-key lookup. Formats that
// discover instruments after committing their sample asset can retain this
// small value without retaining construction state.
class SampleRefLookup {
public:
  [[nodiscard]] std::optional<SampleRef> find(u64 sourceKey) const;
  [[nodiscard]] bool empty() const noexcept { return indexes_.empty(); }

private:
  friend class SampleCollectionBuilder;

  SampleRefLookup(AssetId collection, std::unordered_map<u64, u32> indexes);

  AssetId collection_;
  std::unordered_map<u64, u32> indexes_;
};

// Builds one sample collection while keeping sparse source keys, dense model
// indexes, and source annotations synchronized.
class SampleCollectionBuilder {
public:
  class Entry;

  explicit SampleCollectionBuilder(AssetId asset, SourceMapBuilder* sourceMap = nullptr,
                                   std::vector<Diagnostic>* diagnostics = nullptr);

  SampleCollectionBuilder(const SampleCollectionBuilder&) = delete;
  SampleCollectionBuilder& operator=(const SampleCollectionBuilder&) = delete;
  SampleCollectionBuilder(SampleCollectionBuilder&&) noexcept = default;
  SampleCollectionBuilder& operator=(SampleCollectionBuilder&&) noexcept = default;

  Entry add(u64 sourceKey, Sample sample);
  Entry alias(u64 aliasKey, u64 existingKey);
  [[nodiscard]] std::optional<SampleRef> find(u64 sourceKey) const;
  [[nodiscard]] SampleRefLookup refs() const;

  // Asset-level source structures, such as a sample directory table, use this
  // escape hatch while still receiving the correct asset owner automatically.
  AnnotationBuilder source(SourceRole role, std::string_view label, SourceRange range, std::string_view kind = {});

  SampleCollectionBuilder& include(SourceRange range);
  [[nodiscard]] SourceRange range() const noexcept;
  [[nodiscard]] AssetId assetId() const noexcept { return asset_; }
  [[nodiscard]] bool empty() const noexcept { return samples_.empty(); }
  [[nodiscard]] size_t size() const noexcept { return samples_.size(); }

  void warning(std::string message, SourceRange range = {});
  void error(std::string message, SourceRange range = {});

  [[nodiscard]] SampleCollection finish() &&;

  class Entry {
  public:
    Entry() = default;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] SampleRef ref() const;
    [[nodiscard]] Sample& value() const;
    AnnotationBuilder source(std::string_view label, SourceRange range, std::string_view kind = {}) const;

  private:
    friend class SampleCollectionBuilder;

    Entry(SampleCollectionBuilder& builder, u32 index);

    SampleCollectionBuilder* builder_ = nullptr;
    u32 index_ = 0;
  };

private:
  friend class Entry;

  struct EntryState {
    std::vector<SourceAnnotationId> sources;
  };

  [[nodiscard]] bool validIndex(u32 index) const noexcept;
  [[nodiscard]] AnnotationBuilder addEntrySource(u32 index, std::string_view label, SourceRange range,
                                                 std::string_view kind);
  void addFallbackSources();
  void recordRange(SourceRange range, bool explicitlyIncluded);
  void report(Severity severity, std::string code, std::string message, SourceRange range);

  AssetId asset_;
  SourceMapBuilder* sourceMap_ = nullptr;
  std::vector<Diagnostic>* diagnostics_ = nullptr;
  std::vector<Sample> samples_;
  std::vector<EntryState> states_;
  std::unordered_map<u64, u32> indexes_;
  std::optional<SourceRange> includedRange_;
  std::optional<SourceRange> observedRange_;
  bool finished_ = false;
};

// Builds one instrument set while assigning durable dense identities to
// instruments and regions and projecting their sample relationships.
class InstrumentSetBuilder {
public:
  class Entry;
  class RegionEntry;

  explicit InstrumentSetBuilder(AssetId asset, SourceMapBuilder* sourceMap = nullptr,
                                std::vector<Diagnostic>* diagnostics = nullptr);

  InstrumentSetBuilder(const InstrumentSetBuilder&) = delete;
  InstrumentSetBuilder& operator=(const InstrumentSetBuilder&) = delete;
  InstrumentSetBuilder(InstrumentSetBuilder&&) noexcept = default;
  InstrumentSetBuilder& operator=(InstrumentSetBuilder&&) noexcept = default;

  Entry append(Instrument instrument);
  Entry add(u64 groupingKey, Instrument instrument);
  Entry getOrAdd(u64 groupingKey, Instrument initialValue);
  [[nodiscard]] std::optional<Entry> find(u64 groupingKey);

  AnnotationBuilder source(SourceRole role, std::string_view label, SourceRange range, std::string_view kind = {});

  InstrumentSetBuilder& include(SourceRange range);
  [[nodiscard]] SourceRange range() const noexcept;
  [[nodiscard]] AssetId assetId() const noexcept { return asset_; }
  [[nodiscard]] bool empty() const noexcept { return instruments_.empty(); }
  [[nodiscard]] size_t size() const noexcept { return instruments_.size(); }

  void warning(std::string message, SourceRange range = {});
  void error(std::string message, SourceRange range = {});

  [[nodiscard]] std::vector<Instrument> finish() &&;

  class Entry {
  public:
    Entry() = default;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] Instrument& value() const;
    AnnotationBuilder source(std::string_view label, SourceRange range, std::string_view kind = {}) const;
    RegionEntry region(SampleRef sample, Region region) const;
    RegionEntry regionAt(u32 regionIndex) const;

  private:
    friend class InstrumentSetBuilder;

    Entry(InstrumentSetBuilder& builder, u32 index);

    InstrumentSetBuilder* builder_ = nullptr;
    u32 index_ = 0;
  };

  class RegionEntry {
  public:
    RegionEntry() = default;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] Region& value() const;
    AnnotationBuilder source(std::string_view label, SourceRange range, std::string_view kind = {}) const;

  private:
    friend class InstrumentSetBuilder;
    friend class Entry;

    RegionEntry(InstrumentSetBuilder& builder, u32 instrumentIndex, u32 regionIndex);

    InstrumentSetBuilder* builder_ = nullptr;
    u32 instrumentIndex_ = 0;
    u32 regionIndex_ = 0;
  };

private:
  friend class Entry;
  friend class RegionEntry;

  struct RegionState {
    bool rangeWasExplicit = false;
    std::optional<SourceRange> observedRange;
    std::vector<SourceAnnotationId> sources;
  };

  struct InstrumentState {
    bool rangeWasExplicit = false;
    std::optional<SourceRange> observedRange;
    std::vector<SourceAnnotationId> sources;
    std::optional<SourceAnnotationId> latestSource;
    std::vector<RegionState> regions;
  };

  [[nodiscard]] bool validInstrument(u32 index) const noexcept;
  [[nodiscard]] bool validRegion(u32 instrumentIndex, u32 regionIndex) const noexcept;
  [[nodiscard]] Entry appendAccepted(Instrument instrument);
  [[nodiscard]] RegionEntry appendRegion(u32 instrumentIndex, SampleRef sample, Region region);
  [[nodiscard]] AnnotationBuilder addInstrumentSource(u32 index, std::string_view label, SourceRange range,
                                                      std::string_view kind);
  [[nodiscard]] AnnotationBuilder addRegionSource(u32 instrumentIndex, u32 regionIndex, std::string_view label,
                                                  SourceRange range, std::string_view kind);
  void syncPrepopulatedRegions(u32 instrumentIndex);
  void addFallbackSources();
  void linkInstrumentSamples(u32 instrumentIndex, SourceAnnotationId annotation);
  void linkSample(SourceAnnotationId annotation, SampleRef sample, std::string_view label);
  void recordInstrumentRange(u32 index, SourceRange range);
  void recordRegionRange(u32 instrumentIndex, u32 regionIndex, SourceRange range);
  void recordRange(SourceRange range, bool explicitlyIncluded);
  void report(Severity severity, std::string code, std::string message, SourceRange range);

  AssetId asset_;
  SourceMapBuilder* sourceMap_ = nullptr;
  std::vector<Diagnostic>* diagnostics_ = nullptr;
  std::vector<Instrument> instruments_;
  std::vector<InstrumentState> states_;
  std::unordered_map<u64, u32> indexes_;
  std::optional<SourceRange> includedRange_;
  std::optional<SourceRange> observedRange_;
  bool finished_ = false;
};

}  // namespace vgmtrans::core
