/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/base/CoreTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

// Item nodes are a UI/debug navigation index over source bytes. They should
// point at the parsed model, not become the parsed model themselves.
enum class ItemKind {
  Source,
  Header,
  Sequence,
  Track,
  Command,
  InstrumentSet,
  Instrument,
  Region,
  SampleCollection,
  Sample,
  Misc,
};

struct ItemNode {
  ItemId id;
  std::optional<ItemId> parent;
  ItemKind kind = ItemKind::Misc;
  std::string detailKind;
  std::string name;
  std::string description;
  SourceRange range;
  std::vector<ItemId> children;
};

struct ItemTree {
  // Item trees are a source-backed presentation index, not ownership of parsed data.
  std::optional<ItemId> root;
  std::vector<ItemNode> nodes;
};

// Asset metadata is the common envelope for sequences, synth sets, and sample
// collections. Format scanners fill this in once, and exporters can remain
// focused on model data instead of rediscovering names/ranges.
struct AssetMetadata {
  AssetId id;
  std::string format;
  std::string name;
  SourceRange range;
  ItemTree items;
};

// Address is a driver/source address, not necessarily a file offset after a
// source has been extracted or otherwise derived.
struct Address {
  u64 value = 0;
};

struct Timebase {
  u32 ppqn = 48;
};

// Loop policy describes export/playback interpretation. The parsed sequence
// still keeps the original loop commands so richer exports can use them later.
enum class LoopPolicy {
  Default,
  PlayOnce,
  Preserve,
};

}  // namespace vgmtrans::core
