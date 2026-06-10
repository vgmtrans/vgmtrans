/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ValueFormats.h"

#include "value/formats/CapcomSnes/CapcomSnesModule.h"
#include "value/formats/CapcomSnes/CapcomSnesProfile.h"
#include "value/extractors/SnesRsnExtractor.h"
#include "value/extractors/SnesSpcExtractor.h"

namespace vgmtrans::formats {

void registerValueFormatModules(core::FormatRegistry& registry) {
  snes_rsn::registerSnesRsnExtractor(registry);
  snes_spc::registerSnesSpcExtractor(registry);
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
