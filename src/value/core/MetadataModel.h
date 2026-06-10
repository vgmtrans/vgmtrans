/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/core/CoreTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

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

struct AssetMetadata {
  AssetId id;
  std::string format;
  std::string name;
  SourceRange range;
  ItemTree items;
};

struct Address {
  u64 value = 0;
};

struct Timebase {
  u32 ppqn = 48;
};

enum class LoopPolicy {
  Default,
  PlayOnce,
  Preserve,
};

}  // namespace vgmtrans::core
