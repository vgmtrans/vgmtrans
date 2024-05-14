/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <sstream>
#include <minizip-ng/unzip.h>
#include <list>

#include "components/FileLoader.h"

class TiXmlElement;
class VirtFile;

enum LoadMethod { LM_APPEND, LM_APPEND_SWAP16, LM_DEINTERLACE };

/**
Converts a std::string to any class with a proper overload of the >> operator
@param temp			The string to be converted
@param out	[OUT]	The container for the returned value
*/
template <class T>
void FromString(const std::string &temp, T *out) {
  std::istringstream val(temp);
  val >> *out;

  assert(!val.fail());
}

struct MAMERomGroup {
    MAMERomGroup() : loadmethod(LM_APPEND), file(nullptr) {}
    template <class T>
    bool GetAttribute(const std::string &attrName, T *out) {
        std::string strValue = attributes[attrName];
        if (strValue.empty())
            return false;  // Attribute name does not exist.

        FromString(strValue, out);
        return true;
    }
    bool GetHexAttribute(const std::string &attrName, uint32_t *out) const;

    LoadMethod loadmethod;
    std::string type;
    std::string encryption;
    std::map<const std::string, std::string> attributes;
    std::list<std::string> roms;
    VirtFile *file;
};

struct MAMEGame {
    MAMERomGroup *GetRomGroupOfType(const std::string &strType);

    std::string name;
    std::string format;
    std::string fmt_version_str;
    std::list<MAMERomGroup> romgroupentries;
};

typedef std::map<std::string, MAMEGame *> GameMap;

class MAMELoader : public FileLoader {
   public:
    MAMELoader();
    ~MAMELoader() override;
    void apply(const RawFile *theFile) override;

   private:
    static VirtFile *LoadRomGroup(const MAMERomGroup &romgroup, const std::string &format,
                                  const unzFile &cur_file);
    static void DeleteBuffers(const std::list<std::pair<uint8_t *, uint32_t>> &buffers);

    int LoadXML();
    static MAMEGame *LoadGameEntry(TiXmlElement *gameElmt);
    static int LoadRomGroupEntry(TiXmlElement *romgroupElmt, MAMEGame *gameentry);

    GameMap gamemap;
    bool bLoadedXml;
};
