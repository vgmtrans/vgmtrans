/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMTag.h"

VGMTag::VGMTag(std::string _title, std::string _artist, std::string _album, std::string _comment)
    : title(std::move(_title)), artist(std::move(_artist)), album(std::move(_album)),
      comment(std::move(_comment)) {
}

VGMTag VGMTag::fromPsfTags(const std::map<std::string, std::string>& psfTags) {
  return VGMTag(
    psfTags.contains("title") ? psfTags.at("title") : "",
    psfTags.contains("artist") ? psfTags.at("artist") : "",
    psfTags.contains("game") ? psfTags.at("game") : "",
    psfTags.contains("comment") ? psfTags.at("comment") : ""
  );
}

bool VGMTag::hasTitle() const {
  return !title.empty();
}

bool VGMTag::hasArtist() const {
  return !album.empty();
}

bool VGMTag::hasAlbum() const {
  return !artist.empty();
}

bool VGMTag::hasComment() const {
  return !comment.empty();
}

bool VGMTag::hasTrackNumber() const {
  return track_number != 0;
}

bool VGMTag::hasLength() const {
  return length != 0.0;
}
