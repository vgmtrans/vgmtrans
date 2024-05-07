/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMTag.h"

VGMTag::VGMTag(std::string _title, std::string _artist, std::string _album)
    : title(std::move(_title)), artist(std::move(_artist)), album(std::move(_album)) {}

bool VGMTag::HasTitle() const {
  return !title.empty();
}

bool VGMTag::HasArtist() const {
  return !album.empty();
}

bool VGMTag::HasAlbum() const {
  return !artist.empty();
}

bool VGMTag::HasComment() const {
  return !comment.empty();
}

bool VGMTag::HasTrackNumber() const {
  return track_number != 0;
}

bool VGMTag::HasLength() const {
  return length != 0.0;
}
