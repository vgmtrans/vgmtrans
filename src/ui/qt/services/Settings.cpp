/*
* VGMTrans (c) 2002-2025
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "Settings.h"
#include "NotificationCenter.h"

namespace {
constexpr auto kVGMSeqFileViewGroup = "VGMSeqFileView";
constexpr auto kLeftPaneViewKey = "leftPaneView";
constexpr auto kRightPaneViewKey = "rightPaneView";
constexpr auto kRightPaneHiddenKey = "rightPaneHidden";
constexpr auto kLeftPaneWidthKey = "leftPaneWidth";
constexpr auto kHexViewGroup = "HexView";
constexpr auto kHexViewFontPointSizeKey = "fontPointSize";
constexpr int kDefaultLeftPaneView = 0;   // PanelViewKind::Hex
constexpr int kDefaultRightPaneView = 3;  // PanelViewKind::PianoRoll
constexpr bool kDefaultRightPaneHidden = false;
constexpr int kUnsetLeftPaneWidth = -1;
constexpr double kUnsetHexViewFontPointSize = -1.0;
}  // namespace

SettingsGroup::SettingsGroup(Settings* parent) : parent(parent), settings(parent->settings) {
}

Settings::Settings(QObject *parent)
  : QObject(parent),
    VGMFileTreeView(this),
    VGMSeqFileView(this),
    hexView(this),
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

int Settings::VGMSeqFileViewSettings::leftPaneView() const {
  settings.beginGroup(kVGMSeqFileViewGroup);
  int viewKind = settings.value(kLeftPaneViewKey, kDefaultLeftPaneView).toInt();
  settings.endGroup();
  return viewKind;
}

void Settings::VGMSeqFileViewSettings::setLeftPaneView(int viewKind) const {
  settings.beginGroup(kVGMSeqFileViewGroup);
  settings.setValue(kLeftPaneViewKey, viewKind);
  settings.endGroup();
}

int Settings::VGMSeqFileViewSettings::rightPaneView() const {
  settings.beginGroup(kVGMSeqFileViewGroup);
  int viewKind = settings.value(kRightPaneViewKey, kDefaultRightPaneView).toInt();
  settings.endGroup();
  return viewKind;
}

void Settings::VGMSeqFileViewSettings::setRightPaneView(int viewKind) const {
  settings.beginGroup(kVGMSeqFileViewGroup);
  settings.setValue(kRightPaneViewKey, viewKind);
  settings.endGroup();
}

bool Settings::VGMSeqFileViewSettings::rightPaneHidden() const {
  settings.beginGroup(kVGMSeqFileViewGroup);
  bool hidden = settings.value(kRightPaneHiddenKey, kDefaultRightPaneHidden).toBool();
  settings.endGroup();
  return hidden;
}

void Settings::VGMSeqFileViewSettings::setRightPaneHidden(bool hidden) const {
  settings.beginGroup(kVGMSeqFileViewGroup);
  settings.setValue(kRightPaneHiddenKey, hidden);
  settings.endGroup();
}

int Settings::VGMSeqFileViewSettings::leftPaneWidth() const {
  settings.beginGroup(kVGMSeqFileViewGroup);
  int width = settings.value(kLeftPaneWidthKey, kUnsetLeftPaneWidth).toInt();
  settings.endGroup();
  return width;
}

void Settings::VGMSeqFileViewSettings::setLeftPaneWidth(int width) const {
  settings.beginGroup(kVGMSeqFileViewGroup);
  settings.setValue(kLeftPaneWidthKey, width);
  settings.endGroup();
}

double Settings::HexViewSettings::fontPointSize() const {
  settings.beginGroup(kHexViewGroup);
  const double pointSize =
      settings.value(kHexViewFontPointSizeKey, kUnsetHexViewFontPointSize).toDouble();
  settings.endGroup();
  return pointSize;
}

void Settings::HexViewSettings::setFontPointSize(double pointSize) const {
  settings.beginGroup(kHexViewGroup);
  settings.setValue(kHexViewFontPointSizeKey, pointSize);
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
