#pragma once

#include <string>

enum class SynthType {
  SoundFont,
  YM2151,
};

constexpr std::string synthTypeToString(SynthType st) throw()
{
  switch (st)
  {
    case SynthType::SoundFont: return "soundfont";
    case SynthType::YM2151: return "ym2151";
    default: throw std::invalid_argument("Unimplemented item");
  }
}