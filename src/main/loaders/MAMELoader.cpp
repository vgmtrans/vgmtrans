/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <tinyxml.h>
#include <spdlog/fmt/fmt.h>
#include <cstdlib>

#include "MAMELoader.h"
#include "Format.h"
#include "KabukiDecrypt.h"
#include "CPS3Decrypt.h"
#include "LoaderManager.h"
#include "Root.h"
#include "Scanner.h"
#include "helper.h"

namespace vgmtrans::loaders {
LoaderRegistration<MAMELoader> _mame("MAME");
}

bool MAMERomGroup::GetHexAttribute(const std::string &attrName, uint32_t *out) const {
    // auto strValue = attributes.at(attrName);
    // if (strValue.empty()) {
    //     return false;
    // }
    auto it = attributes.find(attrName);
    if (it == attributes.end()) {
      return false; // Key not found
    }

    auto strValue = it->second;
    if (strValue.empty()) {
      return false; // Value is empty
    }

    *out = std::strtoul(strValue.c_str(), nullptr, 16);
    return true;
}

MAMERomGroup *MAMEGame::GetRomGroupOfType(const std::string &strType) {
    // for (auto it = romgroupentries.begin(); it != romgroupentries.end(); it++) {
    //     if (it->type.compare(strType) == 0)
    //         return &(*it);
    // }
    for (std::list<MAMERomGroup>::iterator it = romgroupentries.begin(); it != romgroupentries.end(); it++) {
      if (it->type.compare(strType) == 0)
        return &(*it);
    }

    return nullptr;
}

MAMELoader::MAMELoader() {
    bLoadedXml = !LoadXML();
}

MAMELoader::~MAMELoader() {
    DeleteMap<std::string, MAMEGame>(gamemap);
}

int MAMELoader::LoadXML() {
    const std::string xmlFilePath = pRoot->UI_GetResourceDirPath() + "mame_roms.xml";

    TiXmlDocument doc(xmlFilePath);
    if (!doc.LoadFile())  // if loading the xml file fails
    {
        return 1;
    }
    TiXmlElement *rootElmt = doc.FirstChildElement();
    const std::string &className = rootElmt->ValueStr();
    if (className != "romlist")
        return 1;

    /// for all "game" elements
    for (TiXmlElement *gameElmt = rootElmt->FirstChildElement(); gameElmt != 0;
         gameElmt = gameElmt->NextSiblingElement()) {
        if (gameElmt->ValueStr() != "game")
            return 1;
        MAMEGame *gameentry = LoadGameEntry(gameElmt);
        if (!gameentry)
            return 1;
        gamemap[gameentry->name] = gameentry;
    }
    return 0;
}

MAMEGame *MAMELoader::LoadGameEntry(TiXmlElement *gameElmt) {
    MAMEGame *gameentry = new MAMEGame;
    std::string gamename, fmtVersionStr;

    if (gameElmt->QueryValueAttribute("name", &gameentry->name) != TIXML_SUCCESS) {
        delete gameentry;
        return NULL;
    }
    if (gameElmt->QueryValueAttribute("format", &gameentry->format) != TIXML_SUCCESS) {
        delete gameentry;
        return NULL;
    }
    if (gameElmt->QueryValueAttribute("fmt_version", &gameentry->fmt_version_str) != TIXML_SUCCESS) {
        gameentry->fmt_version_str = "";
    }

    // Load rom groups
    for (TiXmlElement *romgroupElmt = gameElmt->FirstChildElement(); romgroupElmt != 0;
         romgroupElmt = romgroupElmt->NextSiblingElement()) {
        if (romgroupElmt->ValueStr() != "romgroup") {
            delete gameentry;
            return NULL;
        }
        if (LoadRomGroupEntry(romgroupElmt, gameentry)) {
            delete gameentry;
            return NULL;
        }
    }
    return gameentry;
}

int MAMELoader::LoadRomGroupEntry(TiXmlElement *romgroupElmt, MAMEGame *gameentry) {
    MAMERomGroup romgroupentry;

    // First, get the "type" and "load_method" attributes.  If they don't exist, we return with an
    // error.
    std::string load_method;
    if (romgroupElmt->QueryValueAttribute("type", &romgroupentry.type) != TIXML_SUCCESS)
        return 1;
    if (romgroupElmt->QueryValueAttribute("load_method", &load_method) != TIXML_SUCCESS)
        return 1;

    // Read the encryption type string, if it exists
    romgroupElmt->QueryValueAttribute("encryption", &romgroupentry.encryption);

    // Iterate through the attributes of the romgroup element, recording any user-defined values.
    for (TiXmlAttribute *attr = romgroupElmt->FirstAttribute(); attr; attr = attr->Next()) {
        // NameTStr() is returning an std::string because we have defined TIXML_USE_STL
        const std::string &attrName = attr->NameTStr();
        // Ignore the attribute if it is "type" or "load_method"; we already dealt with those, they
        // are mandated.
        if (attrName.compare("type") && attrName.compare("load_method"))
            romgroupentry.attributes[attrName] = attr->ValueStr();
    }

    if (load_method == "append")
        romgroupentry.loadmethod = LM_APPEND;
    else if (load_method == "append_swap16")
        romgroupentry.loadmethod = LM_APPEND_SWAP16;
    else if (load_method == "deinterlace")
        romgroupentry.loadmethod = LM_DEINTERLACE;
    else
        return 1;

    // load rom entries
    for (TiXmlElement *romElmt = romgroupElmt->FirstChildElement(); romElmt != 0;
         romElmt = romElmt->NextSiblingElement()) {
        const std::string &elmtName = romElmt->ValueStr();
        if (elmtName != "rom")
            return 1;
        romgroupentry.roms.push_back(romElmt->GetText());
    }

    gameentry->romgroupentries.push_back(romgroupentry);
    return 0;
}

void MAMELoader::apply(const RawFile *file) {
    if (!bLoadedXml || file->extension() != "zip")
        return;

    std::string fullfilename = file->name();
    size_t endoffilename = fullfilename.rfind('.');
    if (endoffilename == std::string::npos)
      return;             // no '.' found in filename, so don't do anything
    std::string filename = fullfilename.substr(0, endoffilename);

    /* Look for the game in our databse */
    GameMap::iterator it = gamemap.find(filename);
    if (it == gamemap.end()) {
      return;
    }

    MAMEGame* gameentry = it->second;

    /* Check if we support this format */
    Format *fmt = Format::GetFormatFromName(gameentry->format);
    if (!fmt) {
      return;
    }

    unzFile cur_file = unzOpen(file->path().c_str());
    if (!cur_file) {
        return;
    }

    // Now we try to load the rom groups.  We save the created file into the rom MAMERomGroupEntry's
    // file member
    // Note that this does not check for an error, so the romgroup entry's file member may receive
    // NULL. This must be checked for in Scan().
    for (auto &entry : gameentry->romgroupentries) {
        entry.file = LoadRomGroup(entry, gameentry->format, cur_file);
    }

    fmt->GetScanner().Scan(nullptr, gameentry);
    for (auto &entry : gameentry->romgroupentries) {
        if (entry.file) {
            enqueue(std::shared_ptr<VirtFile>(entry.file));
        }
    }

    (void)unzClose(cur_file);
}

VirtFile *MAMELoader::LoadRomGroup(const MAMERomGroup &entry, const std::string &format,
                                   unzFile &cur_file) {
    uint32_t destFileSize = 0;
    std::list<std::pair<uint8_t *, uint32_t>> buffers;
    auto roms = entry.roms;
    for (auto &rom : roms) {
        int ret = unzLocateFile(cur_file, rom.c_str(), 0);
        if (ret == UNZ_END_OF_LIST_OF_FILE) {
            // file not found
            DeleteBuffers(buffers);
            return 0;
        }

        unz_file_info info;
        ret = unzGetCurrentFileInfo(cur_file, &info, 0, 0, 0, 0, 0, 0);
        if (ret != UNZ_OK) {
            // could not get zipped file info
            DeleteBuffers(buffers);
            return 0;
        }

        destFileSize += info.uncompressed_size;
        ret = unzOpenCurrentFile(cur_file);
        if (ret != UNZ_OK) {
            // could not open file in zip archive
            DeleteBuffers(buffers);
            return 0;
        }

        uint8_t *buf = new uint8_t[info.uncompressed_size];
        ret = unzReadCurrentFile(cur_file, buf, info.uncompressed_size);
        if (ret != info.uncompressed_size) {
            // error reading file in zip archive
            delete[] buf;
            DeleteBuffers(buffers);
            return 0;
        }

        ret = unzCloseCurrentFile(cur_file);
        if (ret != UNZ_OK) {
            // could not close file in zip archive
            DeleteBuffers(buffers);
            return 0;
        }

        buffers.emplace_back(std::make_pair(buf, info.uncompressed_size));
    }

    uint8_t *destFile = new uint8_t[destFileSize];
    switch (entry.loadmethod) {
        // append the files
        case LM_APPEND: {
            uint32_t curOffset = 0;
            for (auto &buf : buffers) {
                memcpy(destFile + curOffset, buf.first, buf.second);
                curOffset += buf.second;
            }
            break;
        }

        // append the files and swap every 16 byte word
        case LM_APPEND_SWAP16: {
            uint32_t curDestOffset = 0;
            for (auto &buf : buffers) {
                uint8_t *romBuf = buf.first;
                uint32_t romSize = buf.second;
                for (uint32_t i = 0; i < romSize; i += 2) {
                    destFile[curDestOffset + i] = romBuf[i + 1];
                    destFile[curDestOffset + i + 1] = romBuf[i];
                }
                curDestOffset += buf.second;
            }
            break;
        }

        // interlace the bytes from each rom
        case LM_DEINTERLACE: {
            uint32_t curDestOffset = 0;
            uint32_t curRomOffset = 0;
            while (curDestOffset < destFileSize) {
                for (auto &buf : buffers) {
                    destFile[curDestOffset++] = buf.first[curRomOffset];
                }
                curRomOffset++;
            }
            break;
        }
    }
    DeleteBuffers(buffers);

    // If an encryption type is defined, decrypt the data
    if (!entry.encryption.empty()) {
        if (entry.encryption == "kabuki") {
            uint32_t swap_key1, swap_key2, addr_key, xor_key;
            if (!entry.GetHexAttribute("kabuki_swap_key1", &swap_key1) ||
                !entry.GetHexAttribute("kabuki_swap_key2", &swap_key2) ||
                !entry.GetHexAttribute("kabuki_addr_key", &addr_key) ||
                !entry.GetHexAttribute("kabuki_xor_key", &xor_key)) {
                delete[] destFile;
                return 0;
            }
            uint8_t *decrypt = new uint8_t[0x8000];
            KabukiDecrypter::kabuki_decode(destFile, decrypt, destFile, 0x0000, 0x8000, swap_key1,
                                           swap_key2, addr_key, xor_key);
            delete[] decrypt;
        } else if (entry.encryption == "cps3") {
          uint32_t key1, key2;
          if (!entry.GetHexAttribute("key1", &key1) ||
              !entry.GetHexAttribute("key2", &key2)) {

            delete[] destFile;
            return 0;
          }

          if (key1 != 0 && key2 != 0) {
            CPS3Decrypt::cps3_decode((uint32_t *)destFile, (uint32_t *)destFile, key1, key2,
                                     destFileSize);
          }
        }
    }

    VirtFile *newVirtFile =
        new VirtFile(destFile, destFileSize, fmt::format("romgroup - {}", entry.type.c_str()));
    newVirtFile->setUseLoaders(false);
    newVirtFile->setUseScanners(false);
    return newVirtFile;
}

void MAMELoader::DeleteBuffers(std::list<std::pair<uint8_t *, uint32_t>> &buffers) {
    for (auto &buf : buffers) {
        delete[] buf.first;
    }
}
