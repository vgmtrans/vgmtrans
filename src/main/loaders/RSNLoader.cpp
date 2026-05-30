/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "RSNLoader.h"

#include "base/Types.h"
#include "FileLoader.h"
#include "LoaderManager.h"
#include "LogManager.h"

#include <memory>
#include <vector>

#include "unarr.h"

namespace vgmtrans::loaders {
LoaderRegistration<RSNLoader> _rsn("RSN");
}

#define FILE_SIGNATURE_SIZE 7

void RSNLoader::apply(const RawFile *file) {

  if (file->size() < FILE_SIGNATURE_SIZE)
    return;

  // Read header
  u8 rarHeader[FILE_SIGNATURE_SIZE];
  file->readBytes(0, FILE_SIGNATURE_SIZE, rarHeader);

  if (memcmp(rarHeader, reinterpret_cast<const void *>("Rar!\x1A\x07\x00"), FILE_SIGNATURE_SIZE) != 0) {
    return;
  }

  auto path = file->path();
#ifdef _WIN32
  auto stream = ar_open_file_w(path.c_str());
#else
  auto stream = ar_open_file(path.c_str());
#endif
  ar_archive *ar = ar_open_rar_archive(stream);

  while (ar_parse_entry(ar)) {
    size_t size = ar_entry_get_size(ar);
    const char *raw_filename = ar_entry_get_name(ar);
    if (raw_filename) {
      L_INFO("Decompressing file from rar archive: {}", raw_filename);
    } else {
      continue;
    }
    std::vector<u8> buffer(size);
    if (!ar_entry_uncompress(ar, buffer.data(), size)) {
      L_ERROR("Error decompressing file from rar archive: {}", raw_filename);
      ar_close_archive(ar);
      ar_close(stream);
      return;
    }
    enqueue(std::make_unique<VirtFile>(buffer.data(), static_cast<u32>(size), raw_filename, "", file->tag));
  }
  ar_close_archive(ar);
  ar_close(stream);
}
