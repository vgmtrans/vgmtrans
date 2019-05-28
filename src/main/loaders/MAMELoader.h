/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Loader.h"

/* Our own unzip */
#include <unzip.h>

class TiXmlElement;
class VirtFile;

enum LoadMethod { LM_APPEND, LM_APPEND_SWAP16, LM_DEINTERLACE };

using namespace std;

/**
Converts a std::string to any class with a proper overload of the >> opertor
@param temp			The string to be converted
@param out	[OUT]	The container for the returned value
*/
template <class T>
void FromString(const std::string &temp, T *out) {
    std::istringstream val(temp);
    val >> *out;

    assert(!val.fail());
}

typedef struct _MAMERomGroupEntry {
    _MAMERomGroupEntry() : file(NULL) {}
    template <class T>
    bool GetAttribute(const std::string &attrName, T *out) {
        string strValue = attributes[attrName];
        if (strValue.empty())
            return false;  // Attribute name does not exist.

        FromString(strValue, out);
        return true;
    }
    bool GetHexAttribute(const std::string &attrName, uint32_t *out);

    LoadMethod loadmethod;
    std::string type;
    std::string encryption;
    std::map<const std::string, std::string> attributes;
    std::list<std::string> roms;
    VirtFile *file;
} MAMERomGroupEntry;

typedef struct _MAMEGameEntry {
    _MAMEGameEntry() {}
    MAMERomGroupEntry *GetRomGroupOfType(const std::string &strType);

    std::string name;
    std::string format;
    float fmt_version;
    std::string fmt_version_str;
    // map<const std::string, const std::string> attributes;
    std::list<MAMERomGroupEntry> romgroupentries;
} MAMEGameEntry;

typedef std::map<std::string, MAMEGameEntry *> GameMap;

class MAMELoader : public VGMLoader {
   public:
    MAMELoader();
    ~MAMELoader();
    virtual PostLoadCommand Apply(RawFile *theFile);

   private:
    VirtFile *LoadRomGroup(MAMERomGroupEntry *romgroupentry, const std::string &format,
                           unzFile &cur_file);
    void DeleteBuffers(std::list<std::pair<uint8_t *, uint32_t>> &buffers);

   private:
    int LoadXML();
    MAMEGameEntry *LoadGameEntry(TiXmlElement *gameElmt);
    int LoadRomGroupEntry(TiXmlElement *romgroupElmt, MAMEGameEntry *gameentry);

   private:
    GameMap gamemap;
    bool bLoadedXml;
};