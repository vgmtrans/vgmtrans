/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/core/FormatModule.h"
#include "value/core/MidiSequenceBuilder.h"
#include "value/core/Session.h"

namespace vgmtrans::formats {

void registerValueFormatModules(core::FormatRegistry& registry);
void registerValueMidiSequenceProfiles(core::MidiSequenceProfileRegistry& registry);
void registerValueFormats(core::Session& session);

}  // namespace vgmtrans::formats
