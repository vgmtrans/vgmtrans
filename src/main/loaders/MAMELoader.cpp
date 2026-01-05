/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#if defined(_WIN32) || defined(WIN32)
  #include <ioapi.h>
  #include <unzip.h>
  #include "iowin32.h"
#endif
#include <nlohmann/json.hpp>
#include <spdlog/fmt/std.h>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <ranges>

#include "MAMELoader.h"
#include "Format.h"
#include "KabukiDecrypt.h"
#include "CPS3Decrypt.h"
#include "helper.h"
#include "LoaderManager.h"
#include "LogManager.h"
#include "Root.h"
#include "Scanner.h"

namespace vgmtrans::loaders {
LoaderRegistration<MAMELoader> _mame("MAME");
}

using json = nlohmann::json;

bool MAMERomGroup::getHexAttribute(const std::string& attrName, uint32_t* out) const {
  auto it = attributes.find(attrName);
  if (it == attributes.end()) {
    return false;  // Key not found
  }

  auto strValue = it->second;
  if (strValue.empty()) {
    return false;  // Value is empty
  }

  *out = static_cast<uint32_t>(std::strtoul(strValue.c_str(), nullptr, 16));
  return true;
}

MAMERomGroup* MAMEGame::getRomGroupOfType(const std::string& strType) {
  if (auto group = std::ranges::find_if(
          romgroupentries, [&strType](const auto& group) { return group.type == strType; });
      group != romgroupentries.end()) {
    return std::addressof(*group);
  }

  return nullptr;
}

MAMELoader::MAMELoader() {
  bLoadedXml = loadJSON();
}

MAMELoader::~MAMELoader() {
  deleteMap<std::string, MAMEGame>(gamemap);
}

namespace {

const std::filesystem::path kMameJsonFilename = "mame_roms.json";

std::string jsonToString(const json& value) {
  if (value.is_string())
    return value.get<std::string>();
  if (value.is_null())
    return {};
  return value.dump();
}

bool parseLoadMethod(const std::string& method, LoadMethod* out) {
  if (method == "append")
    *out = LoadMethod::APPEND;
  else if (method == "append_swap16")
    *out = LoadMethod::APPEND_SWAP16;
  else if (method == "deinterlace")
    *out = LoadMethod::DEINTERLACE;
  else if (method == "deinterlace_pairs")
    *out = LoadMethod::DEINTERLACE_PAIRS;
  else
    return false;

  return true;
}

bool parseLoadOrder(const std::string& order, LoadOrder* out) {
  if (order == "normal" || order.empty())
    *out = LoadOrder::NORMAL;
  else if (order == "reverse")
    *out = LoadOrder::REVERSE;
  else
    return false;

  return true;
}

void ingestAttributes(const json& attributesObject, std::map<const std::string, std::string>& attributes) {
  if (!attributesObject.is_object())
    return;

  for (const auto& [key, value] : attributesObject.items())
    attributes[key] = jsonToString(value);
}

bool loadRomGroupEntry(const json& romgroupJson, MAMEGame* gameentry) {
  if (!romgroupJson.is_object())
    return false;

  MAMERomGroup romgroupentry;

  auto typeIt = romgroupJson.find("type");
  auto loadMethodIt = romgroupJson.find("load_method");
  if (typeIt == romgroupJson.end() || !typeIt->is_string() || loadMethodIt == romgroupJson.end() ||
      !loadMethodIt->is_string()) {
    return false;
  }

  romgroupentry.type = typeIt->get<std::string>();

  if (!parseLoadMethod(loadMethodIt->get<std::string>(), &romgroupentry.loadmethod)) {
    return false;
  }

  auto loadOrderIt = romgroupJson.find("load_order");
  if (loadOrderIt != romgroupJson.end()) {
    if (!loadOrderIt->is_string() ||
        !parseLoadOrder(loadOrderIt->get<std::string>(), &romgroupentry.load_order)) {
      return false;
    }
  } else {
    romgroupentry.load_order = LoadOrder::NORMAL;
  }

  auto encryptionIt = romgroupJson.find("encryption");
  if (encryptionIt != romgroupJson.end()) {
    if (!encryptionIt->is_string()) {
      return false;
    }
    romgroupentry.encryption = encryptionIt->get<std::string>();
  }

  auto attributesIt = romgroupJson.find("attributes");
  if (attributesIt != romgroupJson.end()) {
    ingestAttributes(*attributesIt, romgroupentry.attributes);
  }

  for (const auto& [key, value] : romgroupJson.items()) {
    if (key == "type" || key == "load_method" || key == "load_order" || key == "roms" ||
        key == "encryption" || key == "attributes") {
      continue;
    }
    if (value.is_object() || value.is_array()) {
      continue;
    }
    romgroupentry.attributes[key] = jsonToString(value);
  }

  auto romsIt = romgroupJson.find("roms");
  if (romsIt == romgroupJson.end() || !romsIt->is_array() || romsIt->empty()) {
    return false;
  }

  for (const auto& rom : *romsIt) {
    if (!rom.is_string()) {
      return false;
    }
    romgroupentry.roms.push_back(rom.get<std::string>());
  }

  gameentry->romgroupentries.push_back(std::move(romgroupentry));
  return true;
}

MAMEGame* loadGameEntry(const json& gameJson) {
  if (!gameJson.is_object()) {
    return nullptr;
  }

  auto nameIt = gameJson.find("name");
  auto formatIt = gameJson.find("format");
  if (nameIt == gameJson.end() || !nameIt->is_string() || formatIt == gameJson.end() ||
      !formatIt->is_string()) {
    return nullptr;
  }

  auto* gameentry = new MAMEGame;
  gameentry->name = nameIt->get<std::string>();
  gameentry->format = formatIt->get<std::string>();
  if (auto fmtVersionIt = gameJson.find("fmt_version");
      fmtVersionIt != gameJson.end() && fmtVersionIt->is_string()) {
    gameentry->fmt_version_str = fmtVersionIt->get<std::string>();
  }

  const json* romGroupsArray = nullptr;
  if (auto romGroupsIt = gameJson.find("rom_groups"); romGroupsIt != gameJson.end()) {
    if (!romGroupsIt->is_array()) {
      delete gameentry;
      return nullptr;
    }
    romGroupsArray = &(*romGroupsIt);
  }

  if (!romGroupsArray) {
    delete gameentry;
    return nullptr;
  }

  for (const auto& romgroupJson : *romGroupsArray) {
    if (!loadRomGroupEntry(romgroupJson, gameentry)) {
      delete gameentry;
      return nullptr;
    }
  }
  return gameentry;
}

}  // namespace

bool MAMELoader::loadJSON() {
  const auto jsonFilePath = pRoot->UI_getResourceDirPath() / kMameJsonFilename;
  std::ifstream jsonFile(jsonFilePath);
  if (!jsonFile.is_open()) {
    L_ERROR("Failed to open MAME ROM definition JSON at {}", jsonFilePath);
    return false;
  }

  json document;
  try {
    document = json::parse(jsonFile);
  } catch (const json::parse_error& error) {
    L_ERROR("Failed to parse MAME ROM definition JSON: {}", error.what());
    return false;
  }

  const json* games = nullptr;
  if (document.is_array()) {
    games = &document;
  }
  else if (document.is_object()) {
    if (auto gamesIt = document.find("games"); gamesIt != document.end() && gamesIt->is_array()) {
      games = &(*gamesIt);
    }
  }

  if (!games) {
    if (auto gamesIt = document.find("games"); gamesIt != document.end() && gamesIt->is_array()) {
      games = &(*gamesIt);
    }
  }

  if (!games) {
    L_ERROR("MAME ROM definition JSON does not contain a 'games' array");
    return false;
  }

  for (const auto& gameJson : *games) {
    MAMEGame* gameentry = loadGameEntry(gameJson);
    if (!gameentry) {
      return false;
    }
    gamemap[gameentry->name] = gameentry;
  }

  return true;
}

void MAMELoader::apply(const RawFile* file) {
  if (!bLoadedXml || file->extension() != "zip")
    return;

  std::string filename = file->stem();

  /* Look for the game in our database */
  GameMap::iterator it = gamemap.find(filename);
  if (it == gamemap.end()) {
    return;
  }

  MAMEGame* gameentry = it->second;

  /* Check if we support this format */
  Format* fmt = Format::formatFromName(gameentry->format);
  if (!fmt) {
    return;
  }

#if defined(_WIN32) || defined(WIN32)
  zlib_filefunc64_def ffunc{};
  fill_win32_filefunc64W(&ffunc);     // use CreateFileW-based open
  unzFile cur_file = unzOpen2_64(file->path().c_str(), &ffunc);
#else
  unzFile cur_file = unzOpen(file->path().c_str());
#endif

  if (!cur_file) {
    return;
  }

  // Now we try to load the rom groups.  We save the created file into the rom MAMERomGroupEntry's
  // file member
  // Note that this does not check for an error, so the romgroup entry's file member may receive
  // NULL. This must be checked for in Scan().
  for (auto& entry : gameentry->romgroupentries) {
    entry.file = loadRomGroup(entry, gameentry->format, cur_file);
  }

  fmt->getScanner().scan(nullptr, gameentry);
  for (auto& entry : gameentry->romgroupentries) {
    if (entry.file) {
      enqueue(entry.file);
    }
  }

  (void)unzClose(cur_file);
}

VirtFile* MAMELoader::loadRomGroup(const MAMERomGroup& entry, const std::string& format,
                                   const unzFile& cur_file) {
  uint32_t destFileSize = 0;
  std::list<std::pair<uint8_t*, uint32_t>> buffers;
  auto roms = entry.roms;
  for (auto& rom : roms) {
    int ret = unzLocateFile(cur_file, rom.c_str(), 0);
    if (ret == UNZ_END_OF_LIST_OF_FILE) {
      // file not found
      deleteBuffers(buffers);
      return nullptr;
    }

    unz_file_info info;
    ret = unzGetCurrentFileInfo(cur_file, &info, nullptr, 0, nullptr, 0, nullptr, 0);
    if (ret != UNZ_OK) {
      // could not get zipped file info
      deleteBuffers(buffers);
      return nullptr;
    }

    destFileSize += info.uncompressed_size;
    ret = unzOpenCurrentFile(cur_file);
    if (ret != UNZ_OK) {
      // could not open file in zip archive
      deleteBuffers(buffers);
      return nullptr;
    }

    uint8_t* buf = new uint8_t[info.uncompressed_size];
    ret = unzReadCurrentFile(cur_file, buf, static_cast<uint32_t>(info.uncompressed_size));
    if (ret != info.uncompressed_size) {
      // error reading file in zip archive
      delete[] buf;
      deleteBuffers(buffers);
      return nullptr;
    }

    ret = unzCloseCurrentFile(cur_file);
    if (ret != UNZ_OK) {
      // could not close file in zip archive
      deleteBuffers(buffers);
      return nullptr;
    }

    buffers.emplace_back(std::make_pair(buf, info.uncompressed_size));
  }

  uint8_t* destFile = new uint8_t[destFileSize];
  switch (entry.loadmethod) {
    // append the files
    case LoadMethod::APPEND: {
      uint32_t curOffset = 0;

      if (entry.load_order == LoadOrder::REVERSE) {
        for (auto it = buffers.rbegin(); it != buffers.rend(); ++it) {
          uint8_t* buf = it->first;
          uint32_t romSize = it->second;
          memcpy(destFile + curOffset, buf, romSize);
          curOffset += romSize;
        }
      } else {
        for (auto& bufInfo : buffers) {
          uint8_t* buf = bufInfo.first;
          uint32_t romSize = bufInfo.second;
          memcpy(destFile + curOffset, buf, romSize);
          curOffset += romSize;
        }
      }
      break;
    }

    // append the files and swap every 16 byte word
    case LoadMethod::APPEND_SWAP16: {
      uint32_t curDestOffset = 0;

      if (entry.load_order == LoadOrder::REVERSE) {
        for (auto it = buffers.rbegin(); it != buffers.rend(); ++it) {
          uint8_t* romBuf = it->first;
          uint32_t romSize = it->second;
          for (uint32_t i = 0; i < romSize; i += 2) {
            destFile[curDestOffset + i] = romBuf[i + 1];
            destFile[curDestOffset + i + 1] = romBuf[i];
          }
          curDestOffset += romSize;
        }
      } else {
        for (auto& bufInfo : buffers) {
          uint8_t* buf = bufInfo.first;
          uint32_t romSize = bufInfo.second;
          for (uint32_t i = 0; i < romSize; i += 2) {
            destFile[curDestOffset + i] = buf[i + 1];
            destFile[curDestOffset + i + 1] = buf[i];
          }
          curDestOffset += romSize;
        }
      }
      break;
    }

    // Deinterlace the bytes from each rom.
    // For example, for an entry of 2 roms, read from rom 1, then rom 2, then rom 1, then rom 2.
    case LoadMethod::DEINTERLACE: {
      uint32_t curDestOffset = 0;
      uint32_t curRomOffset = 0;

      while (curDestOffset < destFileSize) {
        if (entry.load_order == LoadOrder::REVERSE) {
          for (auto & buffer : std::ranges::reverse_view(buffers))
            destFile[curDestOffset++] = buffer.first[curRomOffset];
        }
        else {
          for (auto& bufInfo : buffers)
            destFile[curDestOffset++] = bufInfo.first[curRomOffset];
        }
        curRomOffset++;
      }
      break;
    }

    case LoadMethod::DEINTERLACE_PAIRS: {
      if (buffers.size() % 2 > 0) {
        L_ERROR("MAMELoader was going to load a rom group by deinterlacing rom pairs, but there"
                "an odd number of roms in the group. Aborting.");
        deleteBuffers(buffers);
        delete[] destFile;
        return nullptr;
      }

      uint32_t curDestOffset = 0;
      uint32_t curRomOffset = 0;
      auto it = buffers.begin();

      while (curDestOffset < destFileSize && it != buffers.end()) {
        // Get the next pair
        if (it != buffers.end()) {
          auto& buf1 = *it++;
          auto& buf2 = *it++;

          auto firstBuf = entry.load_order == LoadOrder::REVERSE ? &buf2 : &buf1;
          auto secondBuf = entry.load_order == LoadOrder::REVERSE ? &buf1 : &buf2;

          while (curDestOffset < destFileSize && curRomOffset < firstBuf->second &&
                 curRomOffset < secondBuf->second) {
            destFile[curDestOffset++] = firstBuf->first[curRomOffset];
            destFile[curDestOffset++] = secondBuf->first[curRomOffset];
            curRomOffset++;
          }
          curRomOffset = 0;  // Reset for the next pair
        }
      }
      break;
    }
  }
  deleteBuffers(buffers);

  // If an encryption type is defined, decrypt the data
  if (!entry.encryption.empty()) {
    if (entry.encryption == "kabuki") {
      uint32_t swap_key1, swap_key2, addr_key, xor_key;
      if (!entry.getHexAttribute("kabuki_swap_key1", &swap_key1) ||
          !entry.getHexAttribute("kabuki_swap_key2", &swap_key2) ||
          !entry.getHexAttribute("kabuki_addr_key", &addr_key) ||
          !entry.getHexAttribute("kabuki_xor_key", &xor_key)) {
        delete[] destFile;
        return nullptr;
      }
      uint8_t* decrypt = new uint8_t[0x8000];
      KabukiDecrypter::kabuki_decode(destFile, decrypt, destFile, 0x0000, 0x8000, swap_key1,
                                     swap_key2, addr_key, xor_key);
      delete[] decrypt;
    } else if (entry.encryption == "cps3") {
      uint32_t key1, key2;
      if (!entry.getHexAttribute("key1", &key1) ||
          !entry.getHexAttribute("key2", &key2)) {
        delete[] destFile;
        return nullptr;
      }

      if (key1 != 0 && key2 != 0) {
        CPS3Decrypt::cps3_decode(reinterpret_cast<uint32_t*>(destFile),
                                 reinterpret_cast<uint32_t*>(destFile), key1, key2, destFileSize);
      }
    }
  }

  VirtFile* newVirtFile = new VirtFile(destFile, destFileSize, fmt::format("romgroup - {}", entry.type.c_str()));
  newVirtFile->setUseLoaders(false);
  newVirtFile->setUseScanners(false);
  delete[] destFile;
  return newVirtFile;
}

void MAMELoader::deleteBuffers(const std::list<std::pair<uint8_t*, uint32_t>>& buffers) {
  for (auto& buf : buffers) {
    delete[] buf.first;
  }
}
