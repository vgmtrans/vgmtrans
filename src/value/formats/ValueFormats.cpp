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
  session.registerFormat(snes_rsn::snesRsnExtractorDefinition());
  session.registerFormat(snes_spc::snesSpcExtractorDefinition());
  session.registerFormat(psf::psfExtractorDefinition());
  session.registerFormat(capcom_snes::capcomSnesDefinition());
  session.registerFormat(nds::ndsDefinition());
  session.registerFormat(akao_snes::akaoSnesDefinition());
  session.registerFormat(akao::akaoDefinition());
  session.registerFormat(konami_snes::konamiSnesDefinition());
}

}  // namespace vgmtrans::formats
