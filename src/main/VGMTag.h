/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <string>
#include <map>
#include <vector>

#pragma once

class VGMTag {
   public:
    VGMTag(void);
    VGMTag(const std::string &_title, const std::string &_artist = "",
           const std::string &_album = "");
    virtual ~VGMTag(void);

    bool HasTitle(void);
    bool HasArtist(void);
    bool HasAlbum(void);
    bool HasComment(void);
    bool HasTrackNumber(void);
    bool HasLength(void);

   public:
    std::string title;
    std::string artist;
    std::string album;
    std::string comment;
    std::map<std::string, std::vector<uint8_t>> binaries;

    /** Track number */
    int track_number;

    /** Length in seconds */
    double length;
};
