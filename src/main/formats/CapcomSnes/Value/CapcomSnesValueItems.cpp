/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "formats/CapcomSnes/Value/CapcomSnesValueItems.h"

namespace vgmtrans::formats::capcom_snes {

std::string capcomSnesCommandDetailKind(const core::SequencerCommand& command) {
  return "capcom-snes-" + core::defaultCommandDetailKind(command);
}

std::string capcomSnesCommandDescription(const core::SequencerCommand& command) {
  return core::defaultCommandDescription(command);
}

}  // namespace vgmtrans::formats::capcom_snes
