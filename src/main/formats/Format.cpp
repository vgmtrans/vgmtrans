/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Format.h"

#include "Matcher.h"
#include "Scanner.h"
#include "VGMColl.h"

FormatMap &Format::registry() {
  static FormatMap registry;
  return registry;
}

Format::Format(const std::string &formatName) {
  registry().insert(make_pair(formatName, this));
}

Format::~Format() = default;

Format *Format::formatFromName(const std::string &name) {
  auto findIt = registry().find(name);
  if (findIt == registry().end())
    return nullptr;
  return (*findIt).second;
}

bool Format::onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  if (!matcher) {
    return false;
  }
  return matcher->onNewFile(file);
}

std::unique_ptr<VGMColl> Format::newCollection() {
  return std::make_unique<VGMColl>();
}

std::unique_ptr<Matcher> Format::newMatcher() {
  return nullptr;
}

bool Format::onCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  if (!matcher) {
    return false;
  }
  return matcher->onCloseFile(file);
}

bool Format::init() {
  scanner = newScanner();
  matcher = newMatcher();
  return true;
}
