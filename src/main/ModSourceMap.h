/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <array>

#include "Modulation.h"

struct OptionStore;

class ModSourceMap {
public:
  explicit ModSourceMap(ModulationSourceTarget target = ModulationSourceTarget::SoundFont);

  void resetToDefaults(ModulationSourceTarget target);
  void load(OptionStore& store, ModulationSourceTarget target);
  void save(OptionStore& store, ModulationSourceTarget target) const;

  [[nodiscard]] ModSource sourceFor(ModDest destination) const;
  void setSourceFor(ModDest destination, ModSource source);

private:
  std::array<ModSource, kModDests.size()> m_sources{};
};
