/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/SynthBuilder.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

namespace {

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
  const auto time = [&](std::string_view stage, std::optional<double> seconds) {
    if (seconds) {
      const bool infinite = std::isinf(*seconds);
      annotation.derived(std::string(stage) + (infinite ? "_infinite" : "_seconds"),
                         infinite ? SourceValue{true} : SourceValue{*seconds});
    }
  };
  time("attack", envelope.attackSeconds);
  time("hold", envelope.holdSeconds);
  time("decay", envelope.decaySeconds);
  time("second_decay", envelope.secondDecaySeconds);
  if (envelope.sustainAmplitude) {
    annotation.derived("sustain_level", *envelope.sustainAmplitude, SourceValueDisplay::Percent);
  }
  time("release", envelope.releaseSeconds);
}

[[nodiscard]] std::pair<std::string_view, u16> audioCodecInfo(AudioCodec codec) {
  switch (codec) {
    case AudioCodec::Unknown:
      return {"Unknown", 16};
    case AudioCodec::PcmS8:
      return {"PCM 8-bit", 8};
    case AudioCodec::PcmS16:
      return {"PCM 16-bit", 16};
    case AudioCodec::SnesBrr:
      return {"SNES BRR", 16};
    case AudioCodec::SnesDspNoise:
      return {"SNES DSP noise", 16};
    case AudioCodec::NdsImaAdpcm:
      return {"NDS IMA ADPCM", 16};
    case AudioCodec::NdsPsg:
      return {"NDS PSG", 16};
    case AudioCodec::GbaDirectSound:
      return {"GBA DirectSound", 8};
    case AudioCodec::GbaPsg:
      return {"GBA PSG", 16};
    case AudioCodec::GbaPsgWave:
      return {"GBA PSG programmable wave", 16};
    case AudioCodec::PsxAdpcm:
      return {"PSX ADPCM", 16};
    case AudioCodec::KonamiK053260Adpcm:
      return {"Konami K053260 ADPCM", 16};
    case AudioCodec::KonamiK054539Adpcm:
      return {"Konami K054539 ADPCM", 16};
    case AudioCodec::OkiAdpcm:
      return {"OKI ADPCM", 4};
  }
  return {"Unknown", 16};
}

}  // namespace

void annotateSynthValue(AnnotationBuilder annotation, const Sample& sample) {
  const auto [codecName, bitsPerSample] = audioCodecInfo(sample.codec);
  annotation.derived("codec", codecName, SourceValueDisplay::Enum)
      .derived("encoded_bytes", sample.encodedData.size)
      .derived("effective_sample_rate", sample.sampleRate)
      .derived("channels", sample.channels)
      .derived("bits_per_sample", bitsPerSample);
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
        [&](const Ym2151Voice& voice) {
          annotation.derived("synth", "YM2151", SourceValueDisplay::Enum)
              .derived("algorithm", voice.algorithm)
              .derived("feedback", voice.feedback)
              .derived("operator_mask", voice.operatorMask, SourceValueDisplay::Hex);
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
  sources_.emplace_back();
  observedRange_.include(samples_.back().encodedData);
  return Entry{*this, index};
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
  observedRange_.include(range);
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
  includedRange_.include(range);
  return *this;
}

SourceRange SamplePoolBuilder::range() const noexcept {
  return includedRange_.valid() ? includedRange_ : observedRange_;
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
  finishSources();
  const SourceRange finalRange = range();
  indexes_.clear();
  finished_ = true;
  return BuiltSamplePool{
      .value = SamplePool{.samples = std::move(samples_)},
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
  observedRange_.include(range);
  if (sourceMap_ == nullptr || !range.valid()) {
    return {};
  }
  auto annotation = sourceMap_->annotation(SourceRole::Sample, label, range).owner(ObjectRefs::sample(asset_, index));
  if (!kind.empty()) {
    annotation.kind(kind);
  }
  sources_[index].push_back(annotation.id());
  return annotation;
}

void SamplePoolBuilder::finishSources() {
  if (sourceMap_ == nullptr) {
    return;
  }
  for (u32 index = 0; index < samples_.size(); ++index) {
    const auto& sample = samples_[index];
    if (sources_[index].empty() && sample.encodedData.valid()) {
      const std::string label = sample.name.empty() ? "Sample " + std::to_string(index) : sample.name;
      addEntrySource(index, label, sample.encodedData, {});
    }
    for (const SourceAnnotationId source : sources_[index]) {
      annotateSynthValue(AnnotationBuilder{*sourceMap_, source}, sample);
    }
  }
}

void SamplePoolBuilder::report(Severity severity, std::string code, std::string message, SourceRange range) {
  if (diagnostics_ == nullptr) {
    return;
  }
  diagnostics_->push_back(Diagnostic{
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .range = range,
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

AnnotationBuilder InstrumentSetBuilder::source(SourceRole role, std::string_view label, SourceRange range,
                                               std::string_view kind) {
  observedRange_.include(range);
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
  includedRange_.include(range);
  return *this;
}

SourceRange InstrumentSetBuilder::range() const noexcept {
  return includedRange_.valid() ? includedRange_ : observedRange_;
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
  finishSources();
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
  observedRange_.include(instrument.range);
  InstrumentState state{.rangeWasExplicit = instrument.range.valid()};
  state.regions.reserve(instrument.regions.size());
  for (const auto& region : instrument.regions) {
    observedRange_.include(region.range);
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
  observedRange_.include(region.range);
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
  const auto& instrumentSources = states_[instrumentIndex].sources;
  if (!instrumentSources.empty()) {
    annotation.parent(instrumentSources.back());
  }
  states_[instrumentIndex].regions[regionIndex].sources.push_back(annotation.id());
  linkSample(annotation.id(), instruments_[instrumentIndex].regions[regionIndex].sample, "Sample");
  return annotation;
}

void InstrumentSetBuilder::finishSources() {
  if (sourceMap_ == nullptr) {
    return;
  }
  for (u32 instrumentIndex = 0; instrumentIndex < instruments_.size(); ++instrumentIndex) {
    const auto& instrument = instruments_[instrumentIndex];
    const auto& state = states_[instrumentIndex];
    if (state.sources.empty() && instrument.range.valid()) {
      const std::string label =
          instrument.name.empty() ? "Instrument " + std::to_string(instrumentIndex) : instrument.name;
      addInstrumentSource(instrumentIndex, label, instrument.range, {});
    }
    for (const SourceAnnotationId source : state.sources) {
      annotateSynthValue(AnnotationBuilder{*sourceMap_, source}, instrument);
    }
    for (u32 regionIndex = 0; regionIndex < instrument.regions.size(); ++regionIndex) {
      const auto& region = instrument.regions[regionIndex];
      const auto& regionState = state.regions[regionIndex];
      if (regionState.sources.empty() && region.range.valid()) {
        addRegionSource(instrumentIndex, regionIndex, "Region", region.range, {});
      }
      for (const SourceAnnotationId source : regionState.sources) {
        annotateSynthValue(AnnotationBuilder{*sourceMap_, source}, region);
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
  observedRange_.include(range);
  if (!state.rangeWasExplicit) {
    instruments_[index].range.include(range);
  }
}

void InstrumentSetBuilder::recordRegionRange(u32 instrumentIndex, u32 regionIndex, SourceRange range) {
  auto& state = states_[instrumentIndex].regions[regionIndex];
  observedRange_.include(range);
  if (!state.rangeWasExplicit) {
    instruments_[instrumentIndex].regions[regionIndex].range.include(range);
  }
}

void InstrumentSetBuilder::report(Severity severity, std::string code, std::string message, SourceRange range) {
  if (diagnostics_ == nullptr) {
    return;
  }
  diagnostics_->push_back(Diagnostic{
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .range = range,
      .object = ObjectRefs::asset(asset_),
  });
}

}  // namespace vgmtrans::core
