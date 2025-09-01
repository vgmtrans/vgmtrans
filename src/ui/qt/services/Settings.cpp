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
    conversion(this),
    detection(this)
{
  conversion.loadIntoOptionsStore();
  detection.loadIntoOptionsStore();
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

/// Put settings into an OptionStore and have ConversionOptions load from it
void Settings::DetectionSettings::loadIntoOptionsStore() const {
  QtOptionsStore store(settings);
  ConversionOptions::the().load(store);
}

/// Put settings into an OptionStore and have ConversionOptions save into it
void Settings::DetectionSettings::saveFromOptionsStore() const {
  QtOptionsStore store(settings);
  ConversionOptions::the().save(store);
}

void Settings::DetectionSettings::setMinSequenceSize(int size) const {
  ConversionOptions::the().setMinSequenceSize(size);
  saveFromOptionsStore();
}

