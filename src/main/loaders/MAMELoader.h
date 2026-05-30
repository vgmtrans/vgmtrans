/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"
#include "components/FileLoader.h"

#include <cassert>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <unzip.h>

class VirtFile;

enum class LoadMethod { APPEND, APPEND_SWAP16, DEINTERLACE, DEINTERLACE_PAIRS };
enum class LoadOrder { NORMAL, REVERSE };

/**
Converts a std::string to any class with a proper overload of the >> operator
@param temp			The string to be converted
@param out	[OUT]	The container for the returned value
*/
template <class T>
void fromString(const std::string &temp, T *out) {
  std::istringstream val(temp);
  val >> *out;

  assert(!val.fail());
}

struct MAMERomGroup {
    template <class T>
    bool getAttribute(const std::string &attrName, T *out) {
        std::string strValue = attributes[attrName];
        if (strValue.empty())
            return false;  // Attribute name does not exist.

        fromString(strValue, out);
        return true;
    }
    bool getHexAttribute(const std::string &attrName, u32 *out) const;

    LoadMethod loadmethod{LoadMethod::APPEND};
    LoadOrder load_order{LoadOrder::NORMAL};
    std::string type;
    std::string encryption;
    std::map<const std::string, std::string> attributes;
    std::list<std::string> roms;
    VirtFile *file{};
};

struct MAMEGame {
    MAMERomGroup *getRomGroupOfType(const std::string &strType);

    std::string name;
    std::string format;
    std::string fmt_version_str;
    std::list<MAMERomGroup> romgroupentries;
};

typedef std::map<std::string, std::unique_ptr<MAMEGame>> GameMap;

class MAMELoader : public FileLoader {
   public:
    MAMELoader();
    ~MAMELoader() override;
    void apply(const RawFile *theFile) override;

   private:
    static std::unique_ptr<VirtFile> loadRomGroup(const MAMERomGroup &romgroup, const std::string &format,
                                                  const unzFile &cur_file);
    bool loadJSON();

    GameMap gamemap;
    bool bLoadedXml;
};
