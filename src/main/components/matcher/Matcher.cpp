/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Matcher.h"

Matcher::Matcher(Format *format) {
  fmt = format;
}

bool Matcher::onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  if(auto seq = std::get_if<VGMSeq *>(&file)) {
    onNewSeq(*seq);
  } else if(auto instr = std::get_if<VGMInstrSet *>(&file)) {
    onNewInstrSet(*instr);
  } else if(auto sampcoll = std::get_if<VGMSampColl *>(&file)) {
    onNewSampColl(*sampcoll);
}

  return false;
}

bool Matcher::onCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  if(auto seq = std::get_if<VGMSeq *>(&file)) {
    onCloseSeq(*seq);
  } else if(auto instr = std::get_if<VGMInstrSet *>(&file)) {
    onCloseInstrSet(*instr);
  } else if(auto sampcoll = std::get_if<VGMSampColl *>(&file)) {
    onCloseSampColl(*sampcoll);
  }
  return false;
}
