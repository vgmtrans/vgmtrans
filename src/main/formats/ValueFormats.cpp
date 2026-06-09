/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "formats/ValueFormats.h"

#include "formats/CapcomSnes/CapcomSnesModule.h"
#include "formats/CapcomSnes/CapcomSnesProfile.h"

namespace vgmtrans::formats {

void registerValueFormatModules(core::FormatRegistry& registry) {
  capcom_snes::registerCapcomSnesModule(registry);
}

void registerValueSequencerProfiles(core::SequencerProfileRegistry& registry) {
  capcom_snes::registerCapcomSnesProfile(registry);
}

void registerValueFormats(core::ProjectSession& session) {
  registerValueFormatModules(session.formats());
  registerValueSequencerProfiles(session.profiles());
}

}  // namespace vgmtrans::formats
