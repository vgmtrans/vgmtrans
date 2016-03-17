#include "pch.h"

#include "VGMTag.h"

VGMTag::VGMTag(void) :
    title(),
    album(),
    artist(),
    comment(),
    track_number(0),
    length(0.0) {
}

VGMTag::VGMTag(const std::wstring &_title, const std::wstring &_artist, const std::wstring &_album) :
    title(_title),
    album(_album),
    artist(_artist),
    comment(),
    track_number(0),
    length(0.0) {
}

VGMTag::~VGMTag(void) {
}

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
