#include "value/formats/NinSnes/KoeiSnesDriver.h"
#include "value/formats/NinSnes/NinSnes.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {

using namespace vgmtrans::core;
using namespace vgmtrans::formats::nin_snes;

void expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeLe16(std::vector<u8>& bytes, u32 offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

}  // namespace

void ninSnesKoeiDriverTraitsAndSixTrackSections() {
  std::vector<u8> bytes(kAramSize);
  std::ranges::copy(koei::detail::kSixTrackLoader, bytes.begin() + 0x600);
  bytes[0] = 1;
  bytes[0xf4] = 6;

  const auto traits = koei::detect(bytes);
  expect(traits && traits->sectionPointerAddress == 0x1d && traits->bgmTrackCount == 6 &&
             traits->requestedSong == 6,
         "Koei detection should keep its BGM topology and request handshake outside NinSnes profiles");

  writeLe16(bytes, 0x1000, 0x1100);
  writeLe16(bytes, 0x1002, 0);
  writeLe16(bytes, 0x1100, 0x110c);
  bytes[0x110c] = 0x18;
  bytes[0x110d] = 0x80;
  bytes[0x110e] = 0;

  const ByteReader reader(SourceId{1}, bytes);
  const Layout layout{.signature = Signature::Standard,
                      .profile = ProfileId::Standard,
                      .playlistAddress = 0x1000,
                      .sectionTrackCount = traits->bgmTrackCount};
  expect(isValidPlaylist(reader, layout), "Koei's six-pointer BGM section should validate");

  const SequenceParse parsed = decodeSequence(reader, layout, AssetId{1});
  const auto play = std::ranges::find_if(parsed.program.sectionPlaylist->commands, [](const PlaylistCommand& command) {
    return command.kind == PlaylistCommandKind::PlaySection;
  });
  expect(parsed.program.tracks.size() == 6 && play != parsed.program.sectionPlaylist->commands.end() &&
             play->trackStarts.size() == 6,
         "Koei BGM should decode as six tracks without changing the Standard profile");
}
