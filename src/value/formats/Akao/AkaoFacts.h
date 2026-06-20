/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/scan/FormatModule.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::akao {

inline constexpr std::string_view kAkaoFactSequence = "akao.sequence";
inline constexpr std::string_view kAkaoFactInstrumentSet = "akao.instrument-set";
inline constexpr std::string_view kAkaoFactSampleCollection = "akao.sample-collection";
inline constexpr std::string_view kAkaoFactRequiredArticulation = "akao.required-articulation";

inline constexpr std::string_view kAkaoFieldSequenceId = "sequence_id";
inline constexpr std::string_view kAkaoFieldSampleSetId = "sample_set_id";
inline constexpr std::string_view kAkaoFieldOffset = "offset";
inline constexpr std::string_view kAkaoFieldVersion = "version";
inline constexpr std::string_view kAkaoFieldFirstArt = "first_art";
inline constexpr std::string_view kAkaoFieldArtCount = "art_count";
inline constexpr std::string_view kAkaoFieldScanOrdinal = "scan_ordinal";
inline constexpr std::string_view kAkaoFieldArtId = "art_id";

[[nodiscard]] inline core::FormatSpecificFact akaoFact(std::string kind, std::vector<core::MatchField> fields) {
  return core::FormatSpecificFact{
      .kind = std::move(kind),
      .fields = std::move(fields),
  };
}

inline void addOptionalFactField(std::vector<core::MatchField>& fields, std::string name, std::optional<u32> value) {
  if (value) {
    fields.push_back(core::MatchField{.name = std::move(name), .value = std::to_string(*value)});
  }
}

}  // namespace vgmtrans::formats::akao
