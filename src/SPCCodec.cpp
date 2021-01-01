/*
 *  Copyright (C) 2005-2020 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "SPCCodec.h"

#include <kodi/Filesystem.h>

namespace
{

// copied from libspc, then modified. thanks :)
SPC_ID666* SPC_get_id666FP(uint8_t* data)
{
  SPC_ID666* id = new SPC_ID666;
  unsigned char playtime_str[4] = {0, 0, 0, 0};

  char c;
  c = data[0x23];
  if (c == 27)
  {
    delete id;
    return nullptr;
  }

  memcpy(id->songname, data + 0x2E, 32);
  id->songname[32] = '\0';

  memcpy(id->gametitle, data + 32 + 0x2E, 32);
  id->gametitle[32] = '\0';

  memcpy(id->dumper, data + 64 + 0x2E, 16);
  id->dumper[16] = '\0';

  memcpy(id->comments, data + 64 + 16 + 0x2E, 32);
  id->comments[32] = '\0';

  memcpy(playtime_str, data + 0xA9, 3);
  playtime_str[3] = '\0';
  id->playtime = atoi((char*)playtime_str);

  memcpy(id->author, data + 0xB0, 32);
  id->author[32] = '\0';

  return id;
}

} /* namespace */

//------------------------------------------------------------------------------

CSPCCodec::CSPCCodec(KODI_HANDLE instance, const std::string& version)
  : CInstanceAudioDecoder(instance, version)
{
}

CSPCCodec::~CSPCCodec()
{
  delete ctx.tag;
  delete[] ctx.data;
  if (ctx.song)
    spc_delete(ctx.song);
}

bool CSPCCodec::Init(const std::string& filename,
                     unsigned int filecache,
                     int& channels,
                     int& samplerate,
                     int& bitspersample,
                     int64_t& totaltime,
                     int& bitrate,
                     AudioEngineDataFormat& format,
                     std::vector<AudioEngineChannel>& channellist)
{
  kodi::vfs::CFile file;
  if (!file.OpenFile(filename, 0))
    return false;

  ctx.song = spc_new();
  ctx.len = file.GetLength();
  ctx.data = new uint8_t[ctx.len];
  file.Read(ctx.data, ctx.len);
  file.Close();

  ctx.pos = 0;

  spc_load_spc(ctx.song, ctx.data, ctx.len);

  ctx.tag = SPC_get_id666FP(ctx.data);
  if (!ctx.tag->playtime)
    ctx.tag->playtime = 4 * 60;

  channels = 2;
  samplerate = 32000;
  bitspersample = 16;
  totaltime = ctx.tag->playtime * 1000;
  format = AUDIOENGINE_FMT_S16NE;
  bitrate = 0;
  channellist = {AUDIOENGINE_CH_FL, AUDIOENGINE_CH_FR};

  return true;
}

int CSPCCodec::ReadPCM(uint8_t* buffer, int size, int& actualsize)
{
  if (ctx.pos > ctx.tag->playtime * 32000 * 4)
    return -1;

  spc_play(ctx.song, size / 2, (short*)buffer);
  actualsize = size;
  ctx.pos += actualsize;

  if (actualsize)
    return 0;

  return 1;
}

int64_t CSPCCodec::Seek(int64_t time)
{
  if (ctx.pos > time / 1000 * 32000 * 4)
  {
    spc_load_spc(ctx.song, ctx.data, ctx.len);
    ctx.pos = 0;
  }

  spc_skip(ctx.song, time / 1000 * 32000 - ctx.pos / 2);
  return time;
}

bool CSPCCodec::ReadTag(const std::string& filename, kodi::addon::AudioDecoderInfoTag& tag)
{
  kodi::vfs::CFile file;
  if (!file.OpenFile(filename, 0))
    return false;

  int len = file.GetLength();
  uint8_t* data = new uint8_t[len];
  if (!data)
    return false;

  file.Read(data, len);
  file.Close();

  SPC_ID666* spcTag = SPC_get_id666FP(data);
  tag.SetTitle(spcTag->songname);
  tag.SetArtist(spcTag->author);
  tag.SetDuration(spcTag->playtime);
  tag.SetSamplerate(32000);
  tag.SetChannels(2);

  delete[] data;
  delete spcTag;

  return true;
}

//------------------------------------------------------------------------------

class ATTRIBUTE_HIDDEN CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon() = default;
  ADDON_STATUS CreateInstance(int instanceType,
                              const std::string& instanceID,
                              KODI_HANDLE instance,
                              const std::string& version,
                              KODI_HANDLE& addonInstance) override
  {
    addonInstance = new CSPCCodec(instance, version);
    return ADDON_STATUS_OK;
  }
  virtual ~CMyAddon() = default;
};

ADDONCREATOR(CMyAddon)
