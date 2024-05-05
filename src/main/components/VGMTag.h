/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <vector>

class VGMTag {
public:
  VGMTag() = default;
  VGMTag(std::string _title, std::string _artist = "", std::string _album = "");
  ~VGMTag() = default;

  bool HasTitle() const;
  bool HasArtist() const;
  bool HasAlbum() const;
  bool HasComment() const;
  bool HasTrackNumber() const;
  bool HasLength() const;

  std::string title;
  std::string artist;
  std::string album;
  std::string comment;
  std::map<std::string, std::vector<uint8_t>> binaries;

  /* Track number */
  int track_number = 0;

  /* Length in seconds */
  double length = 0;
};
