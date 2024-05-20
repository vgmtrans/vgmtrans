/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "Settings.h"
#include "NotificationCenter.h"

SettingsGroup::SettingsGroup(Settings* parent) : parent(parent), settings(parent->settings) {
}

Settings::Settings(QObject *parent) :
      VGMFileTreeView(this)
{
}

void Settings::VGMFileTreeViewSettings::setShowDetails(bool showDetails) const {
  settings.beginGroup("VGMFileTreeView");
  settings.setValue("showDetails", showDetails);
  settings.endGroup();
  NotificationCenter::the()->vgmfiletree_setShowDetails(showDetails);
}

bool Settings::VGMFileTreeViewSettings::showDetails() const {
  settings.beginGroup("VGMFileTreeView");
  bool showDetails = settings.value("showDetails", false).toBool();
  settings.endGroup();

  return showDetails;
}