/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "ConversionTypes.h"
#include "Modulation.h"

#include <array>

struct OptionStore;

class ModSourceMap {
public:
  explicit ModSourceMap(SynthTarget target = SynthTarget::SoundFont);

  void resetToDefaults(SynthTarget target);
  void load(OptionStore& store, SynthTarget target);
  void save(OptionStore& store, SynthTarget target) const;

  [[nodiscard]] ModSource sourceFor(ModDest destination) const;
  void setSourceFor(ModDest destination, ModSource source);

private:
  std::array<ModSource, kModDests.size()> m_sources{};
};
