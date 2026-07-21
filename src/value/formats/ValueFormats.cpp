/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ValueFormats.h"

#include "value/formats/CapcomSnes/CapcomSnes.h"
#include "value/formats/KonamiSnes/KonamiSnes.h"
#include "value/formats/AkaoSnes/AkaoSnes.h"
#include "value/formats/Akao/Akao.h"
#include "value/formats/NDS/Nds.h"
#include "value/extractors/PsfExtractor.h"
#include "value/extractors/SnesRsnExtractor.h"
#include "value/extractors/SnesSpcExtractor.h"
#include "value/session/Session.h"

namespace vgmtrans::formats {

void registerValueFormats(core::Session& session) {
  snes_rsn::registerSnesRsnExtractor(session.formats());
  snes_spc::registerSnesSpcExtractor(session.formats());
  psf::registerPsfExtractor(session.formats());

  // Semantic formats register scanning and execution as one definition. The
  // remaining direct calls are migration adapters.
  session.registerFormat(capcom_snes::capcomSnesDefinition());
  session.registerFormat(nds::ndsDefinition());
  session.registerFormat(akao_snes::akaoSnesDefinition());

  akao::registerAkaoModule(session.formats());
  konami_snes::registerKonamiSnesModule(session.formats());

  akao::registerAkaoSequenceDialects(session.dialects());
  konami_snes::registerKonamiSnesSequenceDialects(session.dialects());
}

}  // namespace vgmtrans::formats
