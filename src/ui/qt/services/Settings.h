/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "main/ConversionTypes.h"
#include "value/export/ExportTypes.h"

#include <QByteArray>
#include <QObject>
#include <QSettings>
#include <QStringList>

class Settings;

struct SettingsGroup {
  explicit SettingsGroup(Settings* owner);
  Settings* owner;
  QSettings& settings;
};

class Settings final : public QObject {
  Q_OBJECT
  friend struct SettingsGroup;

public:
  static Settings* the() {
    static Settings* instance = new Settings();
    return instance;
  }

  struct VGMFileTreeViewSettings : SettingsGroup {
    using SettingsGroup::SettingsGroup;
    [[nodiscard]] bool showDetails() const;
    void setShowDetails(bool showDetails) const;
  } VGMFileTreeView;

  struct ConversionSettings : SettingsGroup {
    using SettingsGroup::SettingsGroup;
    static constexpr int kMaxSequenceLoops = 100;
    [[nodiscard]] BankSelectStyle bankSelectStyle() const;
    void setBankSelectStyle(BankSelectStyle style) const;
    [[nodiscard]] int numSequenceLoops() const;
    void setNumSequenceLoops(int loops) const;
    [[nodiscard]] bool skipChannel10() const;
    void setSkipChannel10(bool skip) const;
    [[nodiscard]] bool terminatePreviousVoice() const;
    void setTerminatePreviousVoice(bool enabled) const;
    [[nodiscard]] vgmtrans::core::MidiPitchTransitionRendering pitchTransitionRendering() const;
    void setPitchTransitionRendering(vgmtrans::core::MidiPitchTransitionRendering rendering) const;
    [[nodiscard]] vgmtrans::core::MidiTuningRendering tuningRendering() const;
    void setTuningRendering(vgmtrans::core::MidiTuningRendering rendering) const;
    [[nodiscard]] vgmtrans::core::ModulationConversionPolicy modulationConversion() const;
    void setModulationConversion(vgmtrans::core::ModulationConversionPolicy policy) const;
    [[nodiscard]] vgmtrans::core::DynamicEnvelopePolicy dynamicEnvelopeConversion() const;
    void setDynamicEnvelopeConversion(vgmtrans::core::DynamicEnvelopePolicy policy) const;
    [[nodiscard]] bool exportOnlyUsedInstruments() const;
    void setExportOnlyUsedInstruments(bool enabled) const;
    [[nodiscard]] vgmtrans::core::SampleFilteringPolicy sampleFiltering() const;
    void setSampleFiltering(vgmtrans::core::SampleFilteringPolicy filtering) const;
  } conversion;

  struct RecentFilesSettings : SettingsGroup {
    using SettingsGroup::SettingsGroup;
    [[nodiscard]] QStringList list() const;
    void add(const QString& path) const;
    void clear() const;
  } recentFiles;

  struct MainWindowSettings : SettingsGroup {
    using SettingsGroup::SettingsGroup;
    [[nodiscard]] QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry) const;
    [[nodiscard]] QByteArray dockState() const;
    void setDockState(const QByteArray& dockState) const;
    void clearDockState() const;
    [[nodiscard]] QByteArray floatingDockGeometry(const QString& dockName) const;
    void setFloatingDockGeometry(const QString& dockName, const QByteArray& geometry) const;
  } mainWindow;

signals:
  void vgmFileTreeShowDetailsChanged(bool showDetails);
  void conversionOptionsChanged();

private:
  explicit Settings(QObject* parent = nullptr);
  QSettings settings;
};
