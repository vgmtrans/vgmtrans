/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QObject>
#include <QSettings>

class Settings;

struct SettingsGroup {
  SettingsGroup(Settings* parent);
  Settings* parent;
  QSettings& settings;
};

class Settings : public QObject {
  Q_OBJECT
  friend struct SettingsGroup;

public:
  static auto the() {
    static Settings* instance = new Settings();
    return instance;
  }

  Settings(const Settings&) = delete;
  Settings&operator=(const Settings&) = delete;
  Settings(Settings&&) = delete;
  Settings&operator=(Settings&&) = delete;

  struct VGMFileTreeViewSettings : public SettingsGroup {
    VGMFileTreeViewSettings(Settings* parent): SettingsGroup(parent) {}

    bool showDetails() const;
    void setShowDetails(bool) const;
  };
  VGMFileTreeViewSettings VGMFileTreeView;

private:
  explicit Settings(QObject *parent = nullptr);
  QSettings settings;
};

