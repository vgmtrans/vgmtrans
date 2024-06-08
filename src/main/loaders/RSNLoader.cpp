/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "common.h"
#include "RSNLoader.h"
#include "FileLoader.h"
#include "LogManager.h"
#include "unarr.h"
#include "LoaderManager.h"

namespace vgmtrans::loaders {
LoaderRegistration<RSNLoader> _rsn("RSN");
}

#define FILE_SIGNATURE_SIZE 7

void RSNLoader::apply(const RawFile *file) {

  // Read header
  uint8_t rarHeader[FILE_SIGNATURE_SIZE];
  file->readBytes(0, FILE_SIGNATURE_SIZE, rarHeader);

  if (memcmp(rarHeader, reinterpret_cast<const void *>("Rar!\x1A\x07\x00"), FILE_SIGNATURE_SIZE) != 0) {
    return;
  }

  auto path = file->path();
  auto stream = ar_open_file(path.string().c_str());
  ar_archive *ar = ar_open_rar_archive(stream);

  while (ar_parse_entry(ar)) {
    size_t size = ar_entry_get_size(ar);
    const char *raw_filename = ar_entry_get_name(ar);
    if (raw_filename) {
      L_INFO("Decompressing file from rar archive: {}", raw_filename);
    } else {
      continue;
    }
    auto buffer = new uint8_t[size];
    if (!ar_entry_uncompress(ar, buffer, size)) {
      L_ERROR("Error decompressing file from rar archive: {}", raw_filename);
      delete[] buffer;
      ar_close_archive(ar);
      ar_close(stream);
      return;
    }
    auto virtFile = new VirtFile(buffer, static_cast<uint32_t>(size), raw_filename, "", file->tag);
    enqueue(virtFile);
  }
  ar_close_archive(ar);
  ar_close(stream);
}
