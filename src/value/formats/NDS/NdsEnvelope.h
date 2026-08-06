/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/EnvelopeModel.h"

#include <optional>

namespace vgmtrans::formats::nds {

// SBNK region fields and SSEQ D0-D3 commands use the same raw envelope-byte
// encoding. These conversions are shared so instrument defaults and dynamic
// per-track overrides always use identical physical values.
[[nodiscard]] std::optional<double> ndsAttackSeconds(u8 attack);
[[nodiscard]] std::optional<double> ndsDecaySeconds(u8 decay);
[[nodiscard]] std::optional<double> ndsSustainAmplitude(u8 sustain);
[[nodiscard]] std::optional<double> ndsReleaseSeconds(u8 release);

[[nodiscard]] std::optional<core::Envelope> ndsEnvelope(u8 attack, u8 decay, u8 sustain, u8 release);

}  // namespace vgmtrans::formats::nds
