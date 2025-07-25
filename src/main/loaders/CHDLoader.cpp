/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "common.h"
#include "CHDLoader.h"
#include "FileLoader.h"
#include "LoaderManager.h"
#include "LogManager.h"

extern "C" {
#include <libchdr/chd.h>
}

namespace vgmtrans::loaders {
LoaderRegistration<CHDLoader> _chd{"CHD"};
}

struct MemoryCoreFileCtx {
  const RawFile *file;
  size_t pos;
};

static uint64_t mem_fsize(core_file *fp) {
  auto ctx = reinterpret_cast<MemoryCoreFileCtx *>(fp->argp);
  return ctx->file->size();
}

static size_t mem_fread(void *ptr, size_t size, size_t nmemb, core_file *fp) {
  auto ctx = reinterpret_cast<MemoryCoreFileCtx *>(fp->argp);
  size_t len = size * nmemb;
  if (ctx->pos + len > ctx->file->size()) {
    len = ctx->file->size() - ctx->pos;
  }
  memcpy(ptr, ctx->file->data() + ctx->pos, len);
  ctx->pos += len;
  return size ? len / size : 0;
}

static int mem_fclose(core_file *) { return 0; }

static int mem_fseek(core_file *fp, int64_t offset, int whence) {
  auto ctx = reinterpret_cast<MemoryCoreFileCtx *>(fp->argp);
  size_t newpos = 0;
  switch (whence) {
    case SEEK_SET:
      newpos = static_cast<size_t>(offset);
      break;
    case SEEK_CUR:
      newpos = ctx->pos + offset;
      break;
    case SEEK_END:
      newpos = ctx->file->size() + offset;
      break;
    default:
      return -1;
  }
  if (newpos > ctx->file->size()) return -1;
  ctx->pos = newpos;
  return 0;
}

void CHDLoader::apply(const RawFile *file) {
  if (file->size() < 8) return;
  if (!std::equal(file->begin(), file->begin() + 8, "MComprHD")) return;

  MemoryCoreFileCtx ctx{file, 0};
  core_file cfile{&ctx, mem_fsize, mem_fread, mem_fclose, mem_fseek};

  chd_file *chd = nullptr;
  chd_error err = chd_open_core_file(&cfile, CHD_OPEN_READ, nullptr, &chd);
  if (err != CHDERR_NONE) {
    L_ERROR("Failed to open CHD: {}", chd_error_string(err));
    return;
  }

  const chd_header *hdr = chd_get_header(chd);
  if (!hdr) {
    chd_close(chd);
    return;
  }

  std::vector<uint8_t> data(static_cast<size_t>(hdr->logicalbytes));
  std::vector<uint8_t> hunkbuf(hdr->hunkbytes);
  size_t offset = 0;
  for (uint32_t h = 0; h < hdr->hunkcount; ++h) {
    err = chd_read(chd, h, hunkbuf.data());
    if (err != CHDERR_NONE) {
      L_ERROR("CHD read error: {}", chd_error_string(err));
      chd_close(chd);
      return;
    }
    size_t to_copy = std::min<size_t>(hdr->hunkbytes, data.size() - offset);
    memcpy(data.data() + offset, hunkbuf.data(), to_copy);
    offset += to_copy;
  }
  chd_close(chd);

  auto virtFile = new VirtFile(data.data(), static_cast<uint32_t>(data.size()),
                               file->name(), file->path().string(), file->tag);
  enqueue(virtFile);
}