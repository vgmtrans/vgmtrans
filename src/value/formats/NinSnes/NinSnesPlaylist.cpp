/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnesPlaylist.h"

#include "value/sequence/CommandSourceMap.h"

#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <set>

namespace vgmtrans::formats::nin_snes {

using namespace core;

[[nodiscard]] PlaylistDecode decodePlaylist(ByteReader reader, const Layout& layout, AssetId sequenceId,
                                            SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const Profile& selected = profile(layout.profile);
  const u8 trackCount = layout.trackCount();
  std::map<u32, PlaylistCommand> commands;
  using SectionTracks = std::vector<std::optional<Address>>;
  std::map<u32, SectionTracks> sections;
  std::vector<u32> pending{layout.playlistAddress};

  const auto warn = [&](std::string message, SourceRange range) {
    if (diagnostics != nullptr) {
      diagnostics->push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = std::move(message),
          .range = range,
      });
    }
  };
  const auto queue = [&](u32 address) {
    if (reader.has(address, 2) && !commands.contains(address) && std::ranges::find(pending, address) == pending.end()) {
      pending.push_back(address);
    }
  };
  const auto decodeSection = [&](u16 address) -> std::optional<SectionTracks> {
    if (!reader.has(address, trackCount * 2)) {
      warn(fmt::format("NinSnes section ${:04X} did not contain {} track pointers", address, trackCount),
           reader.range(address, reader.has(address, 1) ? 1 : 0));
      return std::nullopt;
    }
    SectionTracks trackStarts(trackCount);
    bool active = false;
    for (u8 track = 0; track < trackCount; ++track) {
      const u16 raw = reader.le16(address + track * 2);
      if ((raw & 0xff00) == 0) {
        continue;
      }
      const u16 start = layout.resolveAddress(raw);
      if (!reader.has(start, 1)) {
        warn(fmt::format("NinSnes track pointer ${:04X} was outside ARAM", start),
             reader.range(address + track * 2, 2));
        continue;
      }
      trackStarts[track] = Address{start};
      active = true;
    }
    return active ? std::optional<SectionTracks>{std::move(trackStarts)} : std::nullopt;
  };

  while (!pending.empty() && commands.size() < 4096) {
    const u32 address = pending.back();
    pending.pop_back();
    if (commands.contains(address) || !reader.has(address, 2)) {
      continue;
    }
    const u16 value = reader.le16(address);
    PlaylistCommand command{
        .address = Address{address},
        .fallthrough = Address{address + 2},
        .range = reader.range(address, 2),
    };
    if (value != 0 && value <= 0xff) {
      if (!reader.has(address, 4)) {
        warn("NinSnes playlist repeat was truncated", reader.range(address, 2));
      } else {
        const u16 storedDestination = reader.le16(address + 2);
        const u16 destination = layout.resolveAddress(storedDestination);
        const bool infinite = isInfinitePlaylistRepeat(selected.playlist, value);
        command.fallthrough = Address{address + 4};
        command.range = reader.range(address, 4);
        command.kind = PlaylistCommandKind::Repeat;
        command.target = Address{destination};
        command.additionalPlays = infinite ? 0 : value;
        queue(destination);
        // Infinite repeats never reach their encoded fallthrough, which may be adjacent song data.
        if (!infinite) {
          queue(address + 4);
        }
      }
    } else if (value > 0xff) {
      const u16 sectionAddress = layout.resolveAddress(value);
      command.kind = PlaylistCommandKind::PlaySection;
      command.target = Address{sectionAddress};
      auto section = sections.find(sectionAddress);
      if (section == sections.end()) {
        if (auto decodedSection = decodeSection(sectionAddress)) {
          section = sections.emplace(sectionAddress, std::move(*decodedSection)).first;
        }
      }
      if (section != sections.end()) {
        command.trackStarts = section->second;
      }
      queue(address + 2);
    }
    commands.emplace(address, std::move(command));
  }

  PlaylistDecode decoded{
      .playlist =
          SectionPlaylist{
              .startAddress = Address{layout.playlistAddress},
          },
  };
  for (auto& [_, command] : commands) {
    decoded.playlist.commands.push_back(std::move(command));
  }

  if (sourceMap == nullptr || decoded.playlist.commands.empty()) {
    return decoded;
  }
  const u64 first = decoded.playlist.commands.front().range.offset;
  u64 last = first;
  for (const auto& command : decoded.playlist.commands) {
    last = std::max(last, command.range.endOffset());
  }
  decoded.annotation = sourceMap
                           ->header("Section Playlist",
                                    SourceRange{
                                        .source = reader.source(),
                                        .offset = first,
                                        .size = static_cast<u32>(last - first),
                                    })
                           .kind("nin-snes-playlist")
                           .owner(ObjectRefs::sequence(sequenceId))
                           .id();

  for (auto& command : decoded.playlist.commands) {
    const bool play = command.kind == PlaylistCommandKind::PlaySection;
    const bool repeat = command.kind == PlaylistCommandKind::Repeat;
    auto annotation = sourceMap
                          ->command(play ? "Play Section" : (repeat ? "Repeat Playlist" : "Playlist End"),
                                    command.range, repeat ? SequenceSemantic::Repeat : SequenceSemantic::Meta)
                          .kind("nin-snes-playlist-command")
                          .parent(*decoded.annotation)
                          .field("value", reader.range(command.range.offset, 2), reader.le16(command.range.offset),
                                 SourceValueDisplay::Hex);
    if (play) {
      annotation.derived("section", command.target.value, SourceValueDisplay::Address)
          .link(SourceLinkRole::PointsTo, SourceTarget{reader.range(command.target.value, trackCount * 2)});
    } else if (repeat) {
      annotation
          .field("destination", reader.range(command.range.offset + 2, 2), command.target.value,
                 SourceValueDisplay::Address)
          .derived("additional_plays", command.additionalPlays)
          .derived("infinite", command.additionalPlays == 0)
          .link(SourceLinkRole::RepeatTarget, SourceTarget{reader.range(command.target.value, 2)});
    }
  }

  for (const auto& [address, trackStarts] : sections) {
    auto annotation = sourceMap->header("Section", reader.range(address, trackCount * 2))
                          .kind("nin-snes-section")
                          .parent(*decoded.annotation)
                          .owner(ObjectRefs::sequence(sequenceId));
    for (u8 track = 0; track < trackStarts.size(); ++track) {
      const SourceRange range = reader.range(address + track * 2, 2);
      if (trackStarts[track]) {
        annotation
            .field(fmt::format("track_{}", track + 1), range, trackStarts[track]->value,
                   SourceValueDisplay::Address)
            .link(SourceLinkRole::PointsTo, SourceTarget{reader.range(trackStarts[track]->value, 1)});
      } else {
        annotation.field(fmt::format("track_{}", track + 1), range, 0, SourceValueDisplay::Address);
      }
    }
  }
  return decoded;
}

bool isValidPlaylist(ByteReader reader, const Layout& layout) {
  const u8 trackCount = layout.trackCount();
  const SectionPlaylist& playlist = decodePlaylist(reader, layout, AssetId{}, nullptr, nullptr).playlist;
  if (playlist.commands.empty() ||
      std::ranges::none_of(playlist.commands, [](const PlaylistCommand& command) {
        return command.kind == PlaylistCommandKind::PlaySection && !command.trackStarts.empty();
      })) {
    return false;
  }

  std::set<u64> commandAddresses;
  for (const PlaylistCommand& command : playlist.commands) {
    commandAddresses.insert(command.address.value);
  }
  if (!commandAddresses.contains(playlist.startAddress.value)) {
    return false;
  }

  for (const PlaylistCommand& command : playlist.commands) {
    if (command.kind == PlaylistCommandKind::PlaySection) {
      if (command.trackStarts.size() != trackCount || !commandAddresses.contains(command.fallthrough.value)) {
        return false;
      }
    } else if (command.kind == PlaylistCommandKind::Repeat) {
      if (!commandAddresses.contains(command.target.value) ||
          (command.additionalPlays != 0 && !commandAddresses.contains(command.fallthrough.value))) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace vgmtrans::formats::nin_snes
