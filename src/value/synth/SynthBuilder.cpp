/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/SynthBuilder.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

namespace {

void mergeRange(std::optional<SourceRange>& destination, SourceRange range) {
  if (!range.valid()) {
    return;
  }
  if (!destination) {
    destination = range;
    return;
  }
  // A SourceRange can describe only one source. Exact records from any other
  // source remain in SourceMap instead of being folded into a false span.
  if (destination->source != range.source) {
    return;
  }
  const u64 begin = std::min(destination->offset, range.offset);
  const u64 end = std::max(destination->endOffset(), range.endOffset());
  destination->offset = begin;
  destination->size = end - begin;
}

std::optional<SourceRange> diagnosticRange(SourceRange range) {
  return range.valid() ? std::optional<SourceRange>{range} : std::nullopt;
}

SourceTarget sampleTarget(SampleRef sample) {
  return SourceTarget{ObjectRefs::sample(sample.owner(), sample.index())};
}

void annotateLoop(AnnotationBuilder& annotation, const Loop& loop) {
  annotation.derived("loop_enabled", loop.enabled, SourceValueDisplay::Boolean);
  if (loop.enabled) {
    annotation.derived("loop_start", loop.start).derived("loop_length", loop.length);
  }
}

void annotateEnvelope(AnnotationBuilder& annotation, const Envelope& envelope) {
  if (envelope.attackSeconds) {
    annotation.derived(std::isinf(*envelope.attackSeconds) ? "attack_infinite" : "attack_seconds",
                       std::isinf(*envelope.attackSeconds) ? SourceValue{true} : SourceValue{*envelope.attackSeconds});
  }
  if (envelope.holdSeconds) {
    annotation.derived(std::isinf(*envelope.holdSeconds) ? "hold_infinite" : "hold_seconds",
                       std::isinf(*envelope.holdSeconds) ? SourceValue{true} : SourceValue{*envelope.holdSeconds});
  }
  if (envelope.decaySeconds) {
    annotation.derived(std::isinf(*envelope.decaySeconds) ? "decay_infinite" : "decay_seconds",
                       std::isinf(*envelope.decaySeconds) ? SourceValue{true} : SourceValue{*envelope.decaySeconds});
  }
  if (envelope.secondDecaySeconds) {
    annotation.derived(
        std::isinf(*envelope.secondDecaySeconds) ? "second_decay_infinite" : "second_decay_seconds",
        std::isinf(*envelope.secondDecaySeconds) ? SourceValue{true} : SourceValue{*envelope.secondDecaySeconds});
  }
  if (envelope.sustainAmplitude) {
    annotation.derived("sustain_level", *envelope.sustainAmplitude, SourceValueDisplay::Percent);
  }
  if (envelope.releaseSeconds) {
    annotation.derived(
        std::isinf(*envelope.releaseSeconds) ? "release_infinite" : "release_seconds",
        std::isinf(*envelope.releaseSeconds) ? SourceValue{true} : SourceValue{*envelope.releaseSeconds});
  }
}

[[nodiscard]] std::string_view audioCodecName(AudioCodec codec) {
  switch (codec) {
    case AudioCodec::Unknown:
      return "Unknown";
    case AudioCodec::PcmS8:
      return "PCM 8-bit";
    case AudioCodec::PcmS16:
      return "PCM 16-bit";
    case AudioCodec::SnesBrr:
      return "SNES BRR";
    case AudioCodec::SnesDspNoise:
      return "SNES DSP noise";
    case AudioCodec::NdsImaAdpcm:
      return "NDS IMA ADPCM";
    case AudioCodec::NdsPsg:
      return "NDS PSG";
    case AudioCodec::GbaBdpcm:
      return "GBA BDPCM";
    case AudioCodec::GbaPsg:
      return "GBA PSG";
    case AudioCodec::GbaPsgWave:
      return "GBA PSG programmable wave";
    case AudioCodec::PsxAdpcm:
      return "PSX ADPCM";
    case AudioCodec::KonamiK053260Adpcm:
      return "Konami K053260 ADPCM";
    case AudioCodec::KonamiK054539Adpcm:
      return "Konami K054539 ADPCM";
    case AudioCodec::OkiAdpcm:
      return "OKI ADPCM";
  }
  return "Unknown";
}

}  // namespace

void annotateSynthValue(AnnotationBuilder annotation, const Sample& sample) {
  annotation.derived("codec", audioCodecName(sample.codec), SourceValueDisplay::Enum)
      .derived("encoded_bytes", sample.encodedData.size)
      .derived("effective_sample_rate", sample.sampleRate)
      .derived("channels", sample.channels)
      .derived("bits_per_sample", sample.bitsPerSample);
  if (sample.reverse) {
    annotation.derived("reverse", true, SourceValueDisplay::Boolean);
  }
  annotateLoop(annotation, sample.loop);
  if (sample.pitch.cents != 0) {
    annotation.derived("pitch_cents", sample.pitch.cents, SourceValueDisplay::Cents);
  }
  if (sample.attenuationDb != 0.0) {
    annotation.derived("attenuation_db", sample.attenuationDb, SourceValueDisplay::Decibels);
  }
}

void annotateSynthValue(AnnotationBuilder annotation, const Instrument& instrument) {
  const InstrumentAddress address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
  annotation.derived("bank", address.bank)
      .derived("program", address.program)
      .derived("region_count", instrument.regions.size())
      .derived("reverb", instrument.reverb, SourceValueDisplay::Percent);
  if (instrument.pitchBendRangeCents) {
    annotation.derived("pitch_bend_range_cents", *instrument.pitchBendRangeCents);
  }
  if (instrument.synthVoice) {
    std::visit(
        [&](const auto& voice) {
          using Voice = std::decay_t<decltype(voice)>;
          if constexpr (std::is_same_v<Voice, Ym2151Voice>) {
            annotation.derived("synth", "YM2151", SourceValueDisplay::Enum)
                .derived("algorithm", voice.algorithm)
                .derived("feedback", voice.feedback)
                .derived("operator_mask", voice.operatorMask, SourceValueDisplay::Hex);
          }
        },
        *instrument.synthVoice);
  }
}

void annotateSynthValue(AnnotationBuilder annotation, const Region& region) {
  annotation.derived("key_low", region.keyRange.low, SourceValueDisplay::MidiNote)
      .derived("key_high", region.keyRange.high, SourceValueDisplay::MidiNote)
      .derived("velocity_low", region.velocityRange.low)
      .derived("velocity_high", region.velocityRange.high)
      .derived("pan", region.pan, SourceValueDisplay::Percent)
      .derived("attenuation_db", region.attenuationDb, SourceValueDisplay::Decibels);
  annotation.derived("unity_key", region.unityKey, SourceValueDisplay::MidiNote);
  if (region.loop) {
    annotateLoop(annotation, *region.loop);
  }
  annotateEnvelope(annotation, region.envelope);
}

SampleRefLookup::SampleRefLookup(AssetId owner, std::unordered_map<u64, u32> indexes)
    : owner_(owner), indexes_(std::move(indexes)) {
}

std::optional<SampleRef> SampleRefLookup::find(u64 sourceKey) const {
  const auto found = indexes_.find(sourceKey);
  if (found == indexes_.end()) {
    return std::nullopt;
  }
  return SampleRef::resolved(owner_, found->second);
}

SamplePoolBuilder::SamplePoolBuilder(AssetId asset, SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics)
    : asset_(asset), sourceMap_(sourceMap), diagnostics_(diagnostics) {
}

SamplePoolBuilder::Entry SamplePoolBuilder::add(u64 sourceKey, Sample sample) {
  if (finished_) {
    throw std::logic_error("Cannot add a sample after SamplePoolBuilder::finish()");
  }
  if (indexes_.contains(sourceKey)) {
    report(Severity::Error, "synth.sample-key.duplicate", "Duplicate sample source key " + std::to_string(sourceKey),
           sample.encodedData);
    return {};
  }

  const u32 index = static_cast<u32>(samples_.size());
  indexes_.emplace(sourceKey, index);
  samples_.push_back(std::move(sample));
  states_.emplace_back();
  recordRange(samples_.back().encodedData, false);
  return Entry{*this, index};
}

SamplePoolBuilder::Entry SamplePoolBuilder::alias(u64 aliasKey, u64 existingKey) {
  if (finished_) {
    throw std::logic_error("Cannot add a sample alias after SamplePoolBuilder::finish()");
  }
  if (indexes_.contains(aliasKey)) {
    report(Severity::Error, "synth.sample-key.duplicate", "Duplicate sample source key " + std::to_string(aliasKey),
           {});
    return {};
  }
  const auto found = indexes_.find(existingKey);
  if (found == indexes_.end()) {
    report(Severity::Error, "synth.sample-alias.missing-target",
           "Sample alias " + std::to_string(aliasKey) + " refers to missing source key " + std::to_string(existingKey),
           {});
    return {};
  }
  indexes_.emplace(aliasKey, found->second);
  return Entry{*this, found->second};
}

std::optional<SampleRef> SamplePoolBuilder::find(u64 sourceKey) const {
  const auto found = indexes_.find(sourceKey);
  if (found == indexes_.end()) {
    return std::nullopt;
  }
  return SampleRef::resolved(asset_, found->second);
}

AnnotationBuilder SamplePoolBuilder::source(SourceRole role, std::string_view label, SourceRange range,
                                            std::string_view kind) {
  recordRange(range, false);
  if (sourceMap_ == nullptr || !range.valid()) {
    return {};
  }
  auto annotation = sourceMap_->annotation(role, label, range).owner(ObjectRefs::asset(asset_));
  if (!kind.empty()) {
    annotation.kind(kind);
  }
  return annotation;
}

AnnotationBuilder SamplePoolBuilder::source(SourceRole role, std::string_view label, const SourceRecord& record,
                                            std::string_view kind) {
  return source(role, label, record.range, kind).fields(record.fields);
}

SamplePoolBuilder& SamplePoolBuilder::include(SourceRange range) {
  recordRange(range, true);
  return *this;
}

SourceRange SamplePoolBuilder::range() const noexcept {
  if (includedRange_) {
    return *includedRange_;
  }
  return observedRange_.value_or(SourceRange{});
}

void SamplePoolBuilder::warning(std::string message, SourceRange range) {
  report(Severity::Warning, {}, std::move(message), range);
}

void SamplePoolBuilder::error(std::string message, SourceRange range) {
  report(Severity::Error, {}, std::move(message), range);
}

BuiltSamplePool SamplePoolBuilder::finish() && {
  if (finished_) {
    throw std::logic_error("SamplePoolBuilder was finished more than once");
  }
  addFallbackSources();
  annotateValues();
  const SourceRange finalRange = range();
  finished_ = true;
  return BuiltSamplePool{
      .value = SamplePool{.samples = std::move(samples_)},
      .refs = SampleRefLookup{asset_, std::move(indexes_)},
      .range = finalRange,
  };
}

SamplePoolBuilder::Entry::Entry(SamplePoolBuilder& builder, u32 index) : builder_(&builder), index_(index) {
}

SamplePoolBuilder::Entry::operator bool() const noexcept {
  return builder_ != nullptr && builder_->validIndex(index_);
}

SampleRef SamplePoolBuilder::Entry::ref() const {
  if (!*this) {
    throw std::logic_error("Invalid SamplePoolBuilder entry");
  }
  return SampleRef::resolved(builder_->asset_, index_);
}

const Sample& SamplePoolBuilder::Entry::value() const {
  if (!*this) {
    throw std::logic_error("Invalid SamplePoolBuilder entry");
  }
  return builder_->samples_[index_];
}

AnnotationBuilder SamplePoolBuilder::Entry::source(std::string_view label, SourceRange range,
                                                   std::string_view kind) const {
  return *this ? builder_->addEntrySource(index_, label, range, kind) : AnnotationBuilder{};
}

AnnotationBuilder SamplePoolBuilder::Entry::source(std::string_view label, const SourceRecord& record,
                                                   std::string_view kind) const {
  return source(label, record.range, kind).fields(record.fields);
}

bool SamplePoolBuilder::validIndex(u32 index) const noexcept {
  return !finished_ && index < samples_.size();
}

AnnotationBuilder SamplePoolBuilder::addEntrySource(u32 index, std::string_view label, SourceRange range,
                                                    std::string_view kind) {
  recordRange(range, false);
  if (sourceMap_ == nullptr || !range.valid()) {
    return {};
  }
  auto annotation = sourceMap_->annotation(SourceRole::Sample, label, range).owner(ObjectRefs::sample(asset_, index));
  if (!kind.empty()) {
    annotation.kind(kind);
  }
  states_[index].sources.push_back(annotation.id());
  return annotation;
}

void SamplePoolBuilder::addFallbackSources() {
  if (sourceMap_ == nullptr) {
    return;
  }
  for (u32 index = 0; index < samples_.size(); ++index) {
    if (!states_[index].sources.empty() || !samples_[index].encodedData.valid()) {
      continue;
    }
    const std::string label = samples_[index].name.empty() ? "Sample " + std::to_string(index) : samples_[index].name;
    auto annotation = sourceMap_->annotation(SourceRole::Sample, label, samples_[index].encodedData)
                          .owner(ObjectRefs::sample(asset_, index));
    states_[index].sources.push_back(annotation.id());
  }
}

void SamplePoolBuilder::annotateValues() {
  if (sourceMap_ == nullptr) {
    return;
  }
  for (u32 index = 0; index < samples_.size(); ++index) {
    for (const SourceAnnotationId source : states_[index].sources) {
      annotateSynthValue(AnnotationBuilder{*sourceMap_, source}, samples_[index]);
    }
  }
}

void SamplePoolBuilder::recordRange(SourceRange range, bool explicitlyIncluded) {
  mergeRange(explicitlyIncluded ? includedRange_ : observedRange_, range);
}

void SamplePoolBuilder::report(Severity severity, std::string code, std::string message, SourceRange range) {
  if (diagnostics_ == nullptr) {
    return;
  }
  diagnostics_->push_back(Diagnostic{
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .range = diagnosticRange(range),
      .object = ObjectRefs::asset(asset_),
  });
}

InstrumentSetBuilder::InstrumentSetBuilder(AssetId asset, SourceMapBuilder* sourceMap,
                                           std::vector<Diagnostic>* diagnostics)
    : asset_(asset), sourceMap_(sourceMap), diagnostics_(diagnostics) {
}

InstrumentSetBuilder::Entry InstrumentSetBuilder::append(Instrument instrument) {
  if (finished_) {
    throw std::logic_error("Cannot add an instrument after InstrumentSetBuilder::finish()");
  }
  return appendAccepted(std::move(instrument));
}

InstrumentSetBuilder::Entry InstrumentSetBuilder::add(u64 groupingKey, Instrument instrument) {
  if (finished_) {
    throw std::logic_error("Cannot add an instrument after InstrumentSetBuilder::finish()");
  }
  if (indexes_.contains(groupingKey)) {
    report(Severity::Error, "synth.instrument-key.duplicate",
           "Duplicate instrument grouping key " + std::to_string(groupingKey), instrument.range);
    return {};
  }
  auto entry = appendAccepted(std::move(instrument));
  indexes_.emplace(groupingKey, entry.index_);
  return entry;
}

InstrumentSetBuilder::Entry InstrumentSetBuilder::getOrAdd(u64 groupingKey, Instrument initialValue) {
  if (finished_) {
    throw std::logic_error("Cannot find or add an instrument after InstrumentSetBuilder::finish()");
  }
  if (const auto found = indexes_.find(groupingKey); found != indexes_.end()) {
    return Entry{*this, found->second};
  }
  return add(groupingKey, std::move(initialValue));
}

std::optional<InstrumentSetBuilder::Entry> InstrumentSetBuilder::find(u64 groupingKey) {
  if (finished_) {
    throw std::logic_error("Cannot find an instrument after InstrumentSetBuilder::finish()");
  }
  const auto found = indexes_.find(groupingKey);
  if (found == indexes_.end()) {
    return std::nullopt;
  }
  return Entry{*this, found->second};
}

AnnotationBuilder InstrumentSetBuilder::source(SourceRole role, std::string_view label, SourceRange range,
                                               std::string_view kind) {
  recordRange(range, false);
  if (sourceMap_ == nullptr || !range.valid()) {
    return {};
  }
  auto annotation = sourceMap_->annotation(role, label, range).owner(ObjectRefs::asset(asset_));
  if (!kind.empty()) {
    annotation.kind(kind);
  }
  return annotation;
}

AnnotationBuilder InstrumentSetBuilder::source(SourceRole role, std::string_view label, const SourceRecord& record,
                                               std::string_view kind) {
  return source(role, label, record.range, kind).fields(record.fields);
}

InstrumentSetBuilder& InstrumentSetBuilder::include(SourceRange range) {
  recordRange(range, true);
  return *this;
}

SourceRange InstrumentSetBuilder::range() const noexcept {
  if (includedRange_) {
    return *includedRange_;
  }
  return observedRange_.value_or(SourceRange{});
}

void InstrumentSetBuilder::warning(std::string message, SourceRange range) {
  report(Severity::Warning, {}, std::move(message), range);
}

void InstrumentSetBuilder::error(std::string message, SourceRange range) {
  report(Severity::Error, {}, std::move(message), range);
}

BuiltInstrumentSet InstrumentSetBuilder::finish() && {
  if (finished_) {
    throw std::logic_error("InstrumentSetBuilder was finished more than once");
  }
  addFallbackSources();
  annotateValues();
  const SourceRange finalRange = range();
  finished_ = true;
  return BuiltInstrumentSet{
      .values = std::move(instruments_),
      .range = finalRange,
  };
}

InstrumentSetBuilder::Entry::Entry(InstrumentSetBuilder& builder, u32 index) : builder_(&builder), index_(index) {
}

InstrumentSetBuilder::Entry::operator bool() const noexcept {
  return builder_ != nullptr && builder_->validInstrument(index_);
}

const Instrument& InstrumentSetBuilder::Entry::value() const {
  if (!*this) {
    throw std::logic_error("Invalid InstrumentSetBuilder entry");
  }
  return builder_->instruments_[index_];
}

AnnotationBuilder InstrumentSetBuilder::Entry::source(std::string_view label, SourceRange range,
                                                      std::string_view kind) const {
  return *this ? builder_->addInstrumentSource(index_, label, range, kind) : AnnotationBuilder{};
}

AnnotationBuilder InstrumentSetBuilder::Entry::source(std::string_view label, const SourceRecord& record,
                                                      std::string_view kind) const {
  return source(label, record.range, kind).fields(record.fields);
}

InstrumentSetBuilder::RegionEntry InstrumentSetBuilder::Entry::region(SampleRef sample, Region region) const {
  return *this ? builder_->appendRegion(index_, sample, std::move(region)) : RegionEntry{};
}

InstrumentSetBuilder::RegionEntry InstrumentSetBuilder::Entry::regionAt(u32 regionIndex) const {
  if (!*this) {
    return {};
  }
  return builder_->validRegion(index_, regionIndex) ? RegionEntry{*builder_, index_, regionIndex} : RegionEntry{};
}

InstrumentSetBuilder::RegionEntry::RegionEntry(InstrumentSetBuilder& builder, u32 instrumentIndex, u32 regionIndex)
    : builder_(&builder), instrumentIndex_(instrumentIndex), regionIndex_(regionIndex) {
}

InstrumentSetBuilder::RegionEntry::operator bool() const noexcept {
  return builder_ != nullptr && builder_->validRegion(instrumentIndex_, regionIndex_);
}

const Region& InstrumentSetBuilder::RegionEntry::value() const {
  if (!*this) {
    throw std::logic_error("Invalid InstrumentSetBuilder region entry");
  }
  return builder_->instruments_[instrumentIndex_].regions[regionIndex_];
}

AnnotationBuilder InstrumentSetBuilder::RegionEntry::source(std::string_view label, SourceRange range,
                                                            std::string_view kind) const {
  return *this ? builder_->addRegionSource(instrumentIndex_, regionIndex_, label, range, kind) : AnnotationBuilder{};
}

AnnotationBuilder InstrumentSetBuilder::RegionEntry::source(std::string_view label, const SourceRecord& record,
                                                            std::string_view kind) const {
  return source(label, record.range, kind).fields(record.fields).fieldsAsChildren();
}

bool InstrumentSetBuilder::validInstrument(u32 index) const noexcept {
  return !finished_ && index < instruments_.size();
}

bool InstrumentSetBuilder::validRegion(u32 instrumentIndex, u32 regionIndex) const noexcept {
  return validInstrument(instrumentIndex) && regionIndex < instruments_[instrumentIndex].regions.size();
}

InstrumentSetBuilder::Entry InstrumentSetBuilder::appendAccepted(Instrument instrument) {
  const u32 index = static_cast<u32>(instruments_.size());
  recordRange(instrument.range, false);
  InstrumentState state{.rangeWasExplicit = instrument.range.valid()};
  state.regions.reserve(instrument.regions.size());
  for (const auto& region : instrument.regions) {
    recordRange(region.range, false);
    state.regions.push_back(RegionState{.rangeWasExplicit = region.range.valid()});
  }
  instruments_.push_back(std::move(instrument));
  states_.push_back(std::move(state));
  return Entry{*this, index};
}

InstrumentSetBuilder::RegionEntry InstrumentSetBuilder::appendRegion(u32 instrumentIndex, SampleRef sample,
                                                                     Region region) {
  region.sample = sample;
  const u32 regionIndex = static_cast<u32>(instruments_[instrumentIndex].regions.size());
  recordRange(region.range, false);
  states_[instrumentIndex].regions.push_back(RegionState{.rangeWasExplicit = region.range.valid()});
  instruments_[instrumentIndex].regions.push_back(std::move(region));
  for (const auto source : states_[instrumentIndex].sources) {
    linkSample(source, sample, "Sample");
  }
  return RegionEntry{*this, instrumentIndex, regionIndex};
}

AnnotationBuilder InstrumentSetBuilder::addInstrumentSource(u32 index, std::string_view label, SourceRange range,
                                                            std::string_view kind) {
  recordInstrumentRange(index, range);
  if (sourceMap_ == nullptr || !range.valid()) {
    return {};
  }
  auto annotation =
      sourceMap_->annotation(SourceRole::Instrument, label, range).owner(ObjectRefs::instrument(asset_, index));
  if (!kind.empty()) {
    annotation.kind(kind);
  }
  states_[index].sources.push_back(annotation.id());
  states_[index].latestSource = annotation.id();
  linkInstrumentSamples(index, annotation.id());
  return annotation;
}

AnnotationBuilder InstrumentSetBuilder::addRegionSource(u32 instrumentIndex, u32 regionIndex, std::string_view label,
                                                        SourceRange range, std::string_view kind) {
  recordRegionRange(instrumentIndex, regionIndex, range);
  if (sourceMap_ == nullptr || !range.valid()) {
    return {};
  }
  auto annotation = sourceMap_->annotation(SourceRole::Region, label, range)
                        .owner(ObjectRefs::region(asset_, instrumentIndex, regionIndex));
  if (!kind.empty()) {
    annotation.kind(kind);
  }
  if (states_[instrumentIndex].latestSource) {
    annotation.parent(*states_[instrumentIndex].latestSource);
  }
  states_[instrumentIndex].regions[regionIndex].sources.push_back(annotation.id());
  linkSample(annotation.id(), instruments_[instrumentIndex].regions[regionIndex].sample, "Sample");
  return annotation;
}

void InstrumentSetBuilder::addFallbackSources() {
  if (sourceMap_ == nullptr) {
    return;
  }
  for (u32 instrumentIndex = 0; instrumentIndex < instruments_.size(); ++instrumentIndex) {
    auto& instrument = instruments_[instrumentIndex];
    auto& state = states_[instrumentIndex];
    if (state.sources.empty() && instrument.range.valid()) {
      const std::string label =
          instrument.name.empty() ? "Instrument " + std::to_string(instrumentIndex) : instrument.name;
      auto annotation = sourceMap_->annotation(SourceRole::Instrument, label, instrument.range)
                            .owner(ObjectRefs::instrument(asset_, instrumentIndex));
      state.sources.push_back(annotation.id());
      state.latestSource = annotation.id();
      linkInstrumentSamples(instrumentIndex, annotation.id());
    }
    for (u32 regionIndex = 0; regionIndex < instrument.regions.size(); ++regionIndex) {
      auto& region = instrument.regions[regionIndex];
      auto& regionState = state.regions[regionIndex];
      if (!regionState.sources.empty() || !region.range.valid()) {
        continue;
      }
      auto annotation = sourceMap_->annotation(SourceRole::Region, "Region", region.range)
                            .owner(ObjectRefs::region(asset_, instrumentIndex, regionIndex));
      if (state.latestSource) {
        annotation.parent(*state.latestSource);
      }
      regionState.sources.push_back(annotation.id());
      linkSample(annotation.id(), region.sample, "Sample");
    }
  }
}

void InstrumentSetBuilder::annotateValues() {
  if (sourceMap_ == nullptr) {
    return;
  }
  for (u32 instrumentIndex = 0; instrumentIndex < instruments_.size(); ++instrumentIndex) {
    const auto& instrument = instruments_[instrumentIndex];
    const auto& state = states_[instrumentIndex];
    for (const SourceAnnotationId source : state.sources) {
      annotateSynthValue(AnnotationBuilder{*sourceMap_, source}, instrument);
    }
    for (u32 regionIndex = 0; regionIndex < instrument.regions.size(); ++regionIndex) {
      for (const SourceAnnotationId source : state.regions[regionIndex].sources) {
        annotateSynthValue(AnnotationBuilder{*sourceMap_, source}, instrument.regions[regionIndex]);
      }
    }
  }
}

void InstrumentSetBuilder::linkInstrumentSamples(u32 instrumentIndex, SourceAnnotationId annotation) {
  for (const auto& region : instruments_[instrumentIndex].regions) {
    linkSample(annotation, region.sample, "Sample");
  }
}

void InstrumentSetBuilder::linkSample(SourceAnnotationId annotation, SampleRef sample, std::string_view label) {
  if (sourceMap_ != nullptr && annotation.valid() && sample.valid()) {
    AnnotationBuilder{*sourceMap_, annotation}.link(SourceLinkRole::UsesSample, sampleTarget(sample), label);
  }
}

void InstrumentSetBuilder::recordInstrumentRange(u32 index, SourceRange range) {
  auto& state = states_[index];
  recordRange(range, false);
  mergeRange(state.observedRange, range);
  if (!state.rangeWasExplicit && state.observedRange) {
    instruments_[index].range = *state.observedRange;
  }
}

void InstrumentSetBuilder::recordRegionRange(u32 instrumentIndex, u32 regionIndex, SourceRange range) {
  auto& state = states_[instrumentIndex].regions[regionIndex];
  recordRange(range, false);
  mergeRange(state.observedRange, range);
  if (!state.rangeWasExplicit && state.observedRange) {
    instruments_[instrumentIndex].regions[regionIndex].range = *state.observedRange;
  }
}

void InstrumentSetBuilder::recordRange(SourceRange range, bool explicitlyIncluded) {
  mergeRange(explicitlyIncluded ? includedRange_ : observedRange_, range);
}

void InstrumentSetBuilder::report(Severity severity, std::string code, std::string message, SourceRange range) {
  if (diagnostics_ == nullptr) {
    return;
  }
  diagnostics_->push_back(Diagnostic{
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .range = diagnosticRange(range),
      .object = ObjectRefs::asset(asset_),
  });
}

}  // namespace vgmtrans::core
