/*
* VGMTrans (c) 2002-2025
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "Settings.h"
#include "NotificationCenter.h"

SettingsGroup::SettingsGroup(Settings* parent) : parent(parent), settings(parent->settings) {
}

Settings::Settings(QObject *parent)
  : QObject(parent),
    VGMFileTreeView(this),
    VGMFileListView(this),
    conversion(this),
    recentFiles(this)
{
  conversion.loadIntoOptionsStore();
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

int Settings::VGMFileListViewSettings::sortKey() const {
  settings.beginGroup("VGMFileListView");
  int key = settings.value("sortKey", 1).toInt();
  settings.endGroup();
  return key;
}

void Settings::VGMFileListViewSettings::setSortKey(int key) const {
  settings.beginGroup("VGMFileListView");
  settings.setValue("sortKey", key);
  settings.endGroup();
}

int Settings::VGMFileListViewSettings::sortOrder() const {
  settings.beginGroup("VGMFileListView");
  int order = settings.value("sortOrder", static_cast<int>(Qt::DescendingOrder)).toInt();
  settings.endGroup();
  return order;
}

void Settings::VGMFileListViewSettings::setSortOrder(int order) const {
  settings.beginGroup("VGMFileListView");
  settings.setValue("sortOrder", order);
  settings.endGroup();
}

/// Put settings into an OptionStore and have ConversionOptions load from it
void Settings::ConversionSettings::loadIntoOptionsStore() const {
  QtOptionsStore store(settings);
  ConversionOptions::the().load(store);
}

/// Put settings into an OptionStore and have ConversionOptions save into it
void Settings::ConversionSettings::saveFromOptionsStore() const {
  QtOptionsStore store(settings);
  ConversionOptions::the().save(store);
}

void Settings::ConversionSettings::setBankSelectStyle(BankSelectStyle s) const {
  ConversionOptions::the().setBankSelectStyle(s);
  saveFromOptionsStore();
}

void Settings::ConversionSettings::setNumSequenceLoops(int n) const {
  ConversionOptions::the().setNumSequenceLoops(n);
  saveFromOptionsStore();
}

void Settings::ConversionSettings::setSkipChannel10(bool skip) const {
  ConversionOptions::the().setSkipChannel10(skip);
  saveFromOptionsStore();
}

QStringList Settings::RecentFilesSettings::list() const {
  settings.beginGroup("RecentFiles");
  auto files = settings.value("files").toStringList();
  settings.endGroup();
  return files;
}

void Settings::RecentFilesSettings::add(const QString& path) const {
  auto files = list();
  files.removeAll(path);
  files.prepend(path);
  constexpr int MaxRecentFiles = 10;
  while (files.size() > MaxRecentFiles)
    files.removeLast();
  settings.beginGroup("RecentFiles");
  settings.setValue("files", files);
  settings.endGroup();
}

void Settings::RecentFilesSettings::clear() const {
  settings.beginGroup("RecentFiles");
  settings.remove("files");
  settings.endGroup();
}
