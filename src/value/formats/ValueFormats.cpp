/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ValueFormats.h"

#include "value/formats/CapcomSnes/CapcomSnes.h"
#include "value/formats/KonamiSnes/KonamiSnesModule.h"
#include "value/formats/KonamiSnes/KonamiSnesSequence.h"
#include "value/formats/AkaoSnes/AkaoSnesModule.h"
#include "value/formats/AkaoSnes/AkaoSnesSequence.h"
#include "value/formats/Akao/AkaoModule.h"
#include "value/formats/Akao/AkaoSequence.h"
#include "value/formats/NDS/NdsModule.h"
#include "value/formats/NDS/NdsSequence.h"
#include "value/extractors/PsfExtractor.h"
#include "value/extractors/SnesRsnExtractor.h"
#include "value/extractors/SnesSpcExtractor.h"
#include "value/session/Session.h"

namespace vgmtrans::formats {

void registerValueFormats(core::Session& session) {
  snes_rsn::registerSnesRsnExtractor(session.formats());
  snes_spc::registerSnesSpcExtractor(session.formats());
  psf::registerPsfExtractor(session.formats());

  // Capcom is the semantic vertical slice: one definition registers both scan
  // and execution. The remaining direct calls are migration adapters and can
  // disappear format by format without changing Session again.
  session.registerFormat(capcom_snes::capcomSnesDefinition());

  akao::registerAkaoModule(session.formats());
  akao_snes::registerAkaoSnesModule(session.formats());
  nds::registerNdsModule(session.formats());
  konami_snes::registerKonamiSnesModule(session.formats());

  akao::registerAkaoSequenceDialects(session.dialects());
  akao_snes::registerAkaoSnesSequenceDialects(session.dialects());
  konami_snes::registerKonamiSnesSequenceDialects(session.dialects());
  nds::registerNdsSequenceDialect(session.dialects());
}

}  // namespace vgmtrans::formats
