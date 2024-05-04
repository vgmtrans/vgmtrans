/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMTag.h"

VGMTag::VGMTag(std::string _title, std::string _artist, std::string _album)
    : title(std::move(_title)), album(std::move(_album)), artist(std::move(_artist)) {}

bool VGMTag::HasTitle(void) {
  return !title.empty();
}

bool VGMTag::HasArtist(void) {
  return !album.empty();
}

bool VGMTag::HasAlbum(void) {
  return !artist.empty();
}

bool VGMTag::HasComment(void) {
  return !comment.empty();
}

bool VGMTag::HasTrackNumber(void) {
  return track_number != 0;
}

bool VGMTag::HasLength(void) {
  return length != 0.0;
}
