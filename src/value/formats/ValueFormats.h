/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/core/FormatModule.h"
#include "value/core/PerformanceLowerer.h"
#include "value/core/ProjectSession.h"

namespace vgmtrans::formats {

void registerValueFormatModules(core::FormatRegistry& registry);
void registerValueSequencerProfiles(core::SequencerProfileRegistry& registry);
void registerValueFormats(core::ProjectSession& session);

}  // namespace vgmtrans::formats
