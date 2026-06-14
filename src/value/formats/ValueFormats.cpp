/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ValueFormats.h"

#include "value/formats/CapcomSnes/CapcomSnesModule.h"
#include "value/formats/CapcomSnes/CapcomSnesSequence.h"
#include "value/formats/NDS/NdsModule.h"
#include "value/formats/NDS/NdsSequence.h"
#include "value/extractors/PsfExtractor.h"
#include "value/extractors/SnesRsnExtractor.h"
#include "value/extractors/SnesSpcExtractor.h"
#include "value/session/Session.h"

namespace vgmtrans::formats {

void registerValueFormatModules(core::FormatRegistry& registry) {
  snes_rsn::registerSnesRsnExtractor(registry);
  snes_spc::registerSnesSpcExtractor(registry);
  psf::registerPsfExtractor(registry);
  nds::registerNdsModule(registry);
  capcom_snes::registerCapcomSnesModule(registry);
}

void registerValueSequenceDialects(core::SequenceDialectRegistry& registry) {
  capcom_snes::registerCapcomSnesSequenceDialects(registry);
  nds::registerNdsSequenceDialect(registry);
}

void registerValueFormats(core::Session& session) {
  registerValueFormatModules(session.formats());
  registerValueSequenceDialects(session.dialects());
}

}  // namespace vgmtrans::formats
