/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <string_view>

namespace vgmtrans::formats::akao {

inline constexpr std::string_view kAkaoSequenceIdDomain = "akao.sequence-id";
inline constexpr std::string_view kAkaoSampleSetDomain = "akao.sample-set";
inline constexpr std::string_view kAkaoArticulationDomain = "akao.articulation";

}  // namespace vgmtrans::formats::akao
