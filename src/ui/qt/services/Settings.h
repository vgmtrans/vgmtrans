/*
* VGMTrans (c) 2002-2025
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include "Options.h"
#include <QObject>
#include <QSettings>

class Settings;

class QtOptionsStore : public OptionStore {
public:
  explicit QtOptionsStore(QSettings& s) : m_settings(s) {}

  std::unique_ptr<Group> beginGroup(std::string_view path) override {
    m_settings.beginGroup(QString::fromUtf8(path.data(), int(path.size())));
    struct G : Group {
      explicit G(QSettings& s) : s(s) {}
      ~G() override { s.endGroup(); }
      QSettings& s;
    };
    return std::make_unique<G>(m_settings);
  }

  int getInt(std::string_view key, int def) const override {
    return m_settings.value(QString::fromUtf8(key.data(), int(key.size())), def).toInt();
  }

  void setInt(std::string_view key, int value) override {
    m_settings.setValue(QString::fromUtf8(key.data(), int(key.size())), value);
  }

  int getBool(std::string_view key, bool def) const override {
    return m_settings.value(QString::fromUtf8(key.data(), int(key.size())), def).toBool();
  }

  void setBool(std::string_view key, bool value) override {
    m_settings.setValue(QString::fromUtf8(key.data(), int(key.size())), value);
  }

private:
  QSettings& m_settings;
};


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

  struct ConversionSettings : public SettingsGroup {
    using SettingsGroup::SettingsGroup;

    void loadIntoOptionsStore() const;
    void saveFromOptionsStore() const;

    BankSelectStyle bankSelectStyle() const {
      return ConversionOptions::the().bankSelectStyle();
    }
    void setBankSelectStyle(BankSelectStyle s) const;

    int numSequenceLoops() const {
      return ConversionOptions::the().numSequenceLoops();
    }
    void setNumSequenceLoops(int n) const;

    bool skipChannel10() const {
      return ConversionOptions::the().skipChannel10();
    }
    void setSkipChannel10(bool skip) const;
  };
  ConversionSettings conversion;

  struct DetectionSettings : public SettingsGroup {
    using SettingsGroup::SettingsGroup;

    void loadIntoOptionsStore() const;
    void saveFromOptionsStore() const;

    int minSequenceSize() const { return ConversionOptions::the().minSequenceSize(); }
    void setMinSequenceSize(int size) const;
  };
  DetectionSettings detection;

private:
  explicit Settings(QObject *parent = nullptr);
  QSettings settings;
};

