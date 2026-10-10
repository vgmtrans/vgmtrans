/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "TestSupport.h"
#include "value/export/CollectionBinding.h"

// Tests of native driver behavior request the unadapted event stream directly.
// CollectionPlayback retains encoded audio inputs and the highlighting timeline.
inline vgmtrans::core::PerformanceSequence renderTestCollection(
    const vgmtrans::core::SessionSnapshot& snapshot, vgmtrans::core::CollectionId collection,
    const vgmtrans::core::SequenceRenderOptions& options = {}) {
  const auto binding = vgmtrans::core::bindCollection(snapshot, collection);
  expect(binding.collection.has_value(), "the format fixture should bind successfully");
  auto rendered = vgmtrans::core::renderCollection(*binding.collection, options);
  expect(rendered.performance.has_value(), "the format fixture should render successfully");
  return std::move(*rendered.performance);
}
