#include "pch.h"
#include "Format.h"
#include "Matcher.h"

using namespace std;

FormatMap& Format::registry() {
  static FormatMap* registry = new FormatMap();
  return *registry;
}

Format::Format(const string &formatName) :
    scanner(NULL),
    matcher(NULL) {
  registry().insert(make_pair(formatName, this));
}

Format::~Format(void) {
  if (scanner != NULL)
    delete scanner;
  if (matcher != NULL)
    delete matcher;
}

void Format::DeleteFormatRegistry() {
  for (auto& pair : registry()) {
    delete pair.second;
  }
  registry().clear();
}

Format *Format::GetFormatFromName(const string &name) {
  FormatMap::iterator findIt = registry().find(name);
  if (findIt == registry().end())
    return NULL;
  return (*findIt).second;
}

bool Format::OnNewFile(VGMFile *file) {
  if (!matcher)
    return false;
  return matcher->OnNewFile(file);
}

VGMColl *Format::NewCollection() {
  return new VGMColl();
}

bool Format::OnCloseFile(VGMFile *file) {
  if (!matcher)
    return false;
  return matcher->OnCloseFile(file);
}

bool Format::Init(void) {
  scanner = NewScanner();
  matcher = NewMatcher();
  return true;
}
