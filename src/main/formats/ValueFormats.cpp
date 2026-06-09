/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "formats/ValueFormats.h"

#include "formats/CapcomSnes/CapcomSnesModule.h"
#include "formats/CapcomSnes/CapcomSnesProfile.h"
#include "formats/SnesSpc/SnesSpcModule.h"

namespace vgmtrans::formats {

void registerValueFormatModules(core::FormatRegistry& registry) {
  snes_spc::registerSnesSpcModule(registry);
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
