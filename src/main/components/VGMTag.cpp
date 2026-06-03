/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMTag.h"

VGMTag::VGMTag(std::string title, std::string artist, std::string album, std::string comment)
    : title(std::move(title)), artist(std::move(artist)), album(std::move(album)),
      comment(std::move(comment)) {
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
