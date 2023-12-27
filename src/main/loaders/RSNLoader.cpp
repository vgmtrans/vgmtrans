/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "common.h"
#include "RSNLoader.h"
#include "Root.h"
#include "unarr.h"

using namespace std;

#define FILE_SIGNATURE_SIZE 7

PostLoadCommand RSNLoader::Apply(RawFile *file) {

  // Read header
  uint8_t rarHeader[FILE_SIGNATURE_SIZE];
  file->GetBytes(0, FILE_SIGNATURE_SIZE, rarHeader);

  if (memcmp(rarHeader, "Rar!\x1A\x07\x00", FILE_SIGNATURE_SIZE) != 0) {
    return KEEP_IT;
  }

  auto path = file->GetFullPath();
  auto stream = ar_open_file(path.c_str());
  ar_archive *ar = ar_open_rar_archive(stream);

  while (ar_parse_entry(ar)) {
    size_t size = ar_entry_get_size(ar);
    const char *raw_filename = ar_entry_get_name(ar);
    if (raw_filename) {
      pRoot->AddLogItem(new LogItem(string("Decompressing file from rar archive:" + string(raw_filename)),
                      LOG_LEVEL_INFO, "RSNLoader"));
    } else {
      continue;
    }
    auto buffer = new uint8_t[size];
    if (!ar_entry_uncompress(ar, buffer, size)) {
      pRoot->AddLogItem(new LogItem(string("Error decompressing file from rar archive:" + string(raw_filename)),
                                    LOG_LEVEL_ERR,"RSNLoader"));
      delete[] buffer;
      ar_close_archive(ar);
      ar_close(stream);
      return KEEP_IT;
    }
    pRoot->CreateVirtFile(buffer, size, raw_filename, "", file->tag);
  }
  ar_close_archive(ar);
  ar_close(stream);
  return DELETE_IT;
}
