/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoModule.h"

#include "value/formats/Akao/AkaoResolver.h"
#include "value/formats/Akao/AkaoScanner.h"
#include "value/formats/Akao/AkaoTypes.h"
#include "value/scan/FormatRegistry.h"

namespace vgmtrans::formats::akao {

using namespace core;

void registerAkaoModule(FormatRegistry& registry) {
  registry.add(FormatModule{
      .name = std::string(kAkaoFormatName),
      .canScan = canScanAkao,
      .scan = scanAkao,
      .collectionResolverId = std::string(kAkaoCollectionResolver),
      .resolveCollections = resolveAkaoCollections,
  });
}

}  // namespace vgmtrans::formats::akao
