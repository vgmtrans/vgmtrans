#pragma once
#include "Loader.h"
#include <unzip.h>

class TiXmlElement;
class VirtFile;

enum LoadMethod {
  LM_APPEND,
  LM_APPEND_SWAP16,
  LM_DEINTERLACE
};

using namespace std;

struct MAMERomGroup {
  MAMERomGroup() : file(NULL) { }
  template<class T>
  bool GetAttribute(const std::string &attrName, T *out) {
    string strValue = attributes[attrName];
    if (strValue.empty())
      return false;            //Attribute name does not exist.

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
};

struct MAMEGame {
  MAMEGame() { }
  MAMERomGroup *GetRomGroupOfType(const std::string &strType);

  std::string name;
  std::string format;
  std::string fmt_version_str;
  //map<const std::string, const std::string> attributes;
  std::list<MAMERomGroup> romgroupentries;
};

typedef std::map<std::string, MAMEGame *> GameMap;


class MAMELoader:
    public VGMLoader {
 public:
  MAMELoader();
  ~MAMELoader();
  virtual PostLoadCommand Apply(RawFile *theFile);
 private:
  VirtFile *LoadRomGroup(MAMERomGroup *romgroupentry, const std::string &format, unzFile &cur_file);
  void DeleteBuffers(std::list<std::pair<uint8_t *, uint32_t>> &buffers);
 private:
  int LoadXML();
  MAMEGame *LoadGameEntry(TiXmlElement *gameElmt);
  int LoadRomGroupEntry(TiXmlElement *romgroupElmt, MAMEGame *gameentry);

 private:
  GameMap gamemap;
  bool bLoadedXml;
};