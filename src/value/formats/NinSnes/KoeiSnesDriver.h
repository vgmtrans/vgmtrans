#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace vgmtrans::formats::nin_snes::koei {

struct Traits {
  std::uint8_t sectionPointerAddress;
  std::uint8_t bgmTrackCount;
  std::uint8_t requestedSong;
};

namespace detail {

// Koei's BGM section loader copies six pointers to voices 0-5. Voices 6-7
// belong to the independent sound-effect sequencer.
inline constexpr std::string_view kSixTrackLoader{
    "\xda\x14\x8d\x0b\x8f\x20\x47\x8f"
    "\x00\x20\xf7\x14\xd6\x32\x00\xd0"
    "\x06\xe4\x47\x04\x20\xc4\x20\xdc"
    "\xf7\x14\xd6\x32\x00\xdc\x4b\x47"
    "\xd0\xe8",
    34};

inline bool contains(std::span<const std::uint8_t> aram, std::string_view signature) {
  const std::string_view bytes{reinterpret_cast<const char*>(aram.data()), aram.size()};
  return bytes.find(signature) != std::string_view::npos;
}

inline bool isSongRequest(std::uint8_t value) {
  return value != 0 && value != 0xff;
}

}  // namespace detail

inline std::optional<Traits> detect(std::span<const std::uint8_t> aram) {
  if (aram.size() != 0x10000 || !detail::contains(aram, detail::kSixTrackLoader)) {
    return std::nullopt;
  }

  // The input port may be one tick newer than its direct-page mirror.
  const std::uint8_t portRequest = aram[0xf4];
  const std::uint8_t requestedSong = detail::isSongRequest(portRequest) ? portRequest : aram[0];
  return Traits{.sectionPointerAddress = 0x1d,
                .bgmTrackCount = 6,
                .requestedSong = requestedSong};
}

}  // namespace vgmtrans::formats::nin_snes::koei
