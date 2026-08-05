/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Settings.h"

#include <algorithm>

SettingsGroup::SettingsGroup(Settings* settingsOwner)
    : owner(settingsOwner), settings(settingsOwner->settings) {}

Settings::Settings(QObject* parent)
    : QObject(parent), VGMFileTreeView(this), conversion(this), recentFiles(this),
      mainWindow(this) {}

bool Settings::VGMFileTreeViewSettings::showDetails() const {
  settings.beginGroup(QStringLiteral("VGMFileTreeView"));
  const bool showDetails = settings.value(QStringLiteral("showDetails"), false).toBool();
  settings.endGroup();
  return showDetails;
}

void Settings::VGMFileTreeViewSettings::setShowDetails(bool showDetails) const {
  settings.beginGroup(QStringLiteral("VGMFileTreeView"));
  settings.setValue(QStringLiteral("showDetails"), showDetails);
  settings.endGroup();
  emit owner->vgmFileTreeShowDetailsChanged(showDetails);
}

BankSelectStyle Settings::ConversionSettings::bankSelectStyle() const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  const int value = settings.value(QStringLiteral("bankSelectStyle"),
                                   static_cast<int>(BankSelectStyle::GS)).toInt();
  settings.endGroup();
  return value == static_cast<int>(BankSelectStyle::MMA) ? BankSelectStyle::MMA
                                                         : BankSelectStyle::GS;
}

void Settings::ConversionSettings::setBankSelectStyle(BankSelectStyle style) const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  settings.setValue(QStringLiteral("bankSelectStyle"), static_cast<int>(style));
  settings.endGroup();
  emit owner->conversionOptionsChanged();
}

int Settings::ConversionSettings::numSequenceLoops() const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  const int loops = std::clamp(settings.value(QStringLiteral("sequenceLoops"), 1).toInt(),
                               0, kMaxSequenceLoops);
  settings.endGroup();
  return loops;
}

void Settings::ConversionSettings::setNumSequenceLoops(int loops) const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  settings.setValue(QStringLiteral("sequenceLoops"),
                    std::clamp(loops, 0, kMaxSequenceLoops));
  settings.endGroup();
  emit owner->conversionOptionsChanged();
}

bool Settings::ConversionSettings::skipChannel10() const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  const bool skip = settings.value(QStringLiteral("skipChannel10"), true).toBool();
  settings.endGroup();
  return skip;
}

void Settings::ConversionSettings::setSkipChannel10(bool skip) const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  settings.setValue(QStringLiteral("skipChannel10"), skip);
  settings.endGroup();
  emit owner->conversionOptionsChanged();
}

bool Settings::ConversionSettings::terminatePreviousVoice() const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  const bool enabled = settings.value(QStringLiteral("terminatePreviousVoice"), false).toBool();
  settings.endGroup();
  return enabled;
}

void Settings::ConversionSettings::setTerminatePreviousVoice(bool enabled) const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  settings.setValue(QStringLiteral("terminatePreviousVoice"), enabled);
  settings.endGroup();
  emit owner->conversionOptionsChanged();
}

vgmtrans::core::MidiPitchTransitionRendering Settings::ConversionSettings::pitchTransitionRendering() const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  const int value =
      settings.value(QStringLiteral("pitchTransitionRendering"),
                     static_cast<int>(vgmtrans::core::MidiPitchTransitionRendering::PreserveFormat))
          .toInt();
  settings.endGroup();
  switch (static_cast<vgmtrans::core::MidiPitchTransitionRendering>(value)) {
    case vgmtrans::core::MidiPitchTransitionRendering::PreserveFormat:
    case vgmtrans::core::MidiPitchTransitionRendering::Portamento:
    case vgmtrans::core::MidiPitchTransitionRendering::PitchBend:
      return static_cast<vgmtrans::core::MidiPitchTransitionRendering>(value);
  }
  return vgmtrans::core::MidiPitchTransitionRendering::PreserveFormat;
}

void Settings::ConversionSettings::setPitchTransitionRendering(
    vgmtrans::core::MidiPitchTransitionRendering rendering) const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  settings.setValue(QStringLiteral("pitchTransitionRendering"), static_cast<int>(rendering));
  settings.endGroup();
  emit owner->conversionOptionsChanged();
}

vgmtrans::core::MidiWideTuningRendering Settings::ConversionSettings::wideTuningRendering() const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  const int value =
      settings.value(QStringLiteral("wideTuningRendering"),
                     static_cast<int>(vgmtrans::core::MidiWideTuningRendering::PitchBend))
          .toInt();
  settings.endGroup();
  return value == static_cast<int>(vgmtrans::core::MidiWideTuningRendering::CoarseTune)
             ? vgmtrans::core::MidiWideTuningRendering::CoarseTune
             : vgmtrans::core::MidiWideTuningRendering::PitchBend;
}

void Settings::ConversionSettings::setWideTuningRendering(
    vgmtrans::core::MidiWideTuningRendering rendering) const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  settings.setValue(QStringLiteral("wideTuningRendering"), static_cast<int>(rendering));
  settings.endGroup();
  emit owner->conversionOptionsChanged();
}

vgmtrans::core::ModulationConversionPolicy Settings::ConversionSettings::modulationConversion() const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  const int value =
      settings.value(QStringLiteral("modulationConversion"),
                     static_cast<int>(vgmtrans::core::ModulationConversionPolicy::SynthModulators))
          .toInt();
  settings.endGroup();
  switch (static_cast<vgmtrans::core::ModulationConversionPolicy>(value)) {
    case vgmtrans::core::ModulationConversionPolicy::SynthModulators:
    case vgmtrans::core::ModulationConversionPolicy::SequenceEventSimulation:
      return static_cast<vgmtrans::core::ModulationConversionPolicy>(value);
  }
  return vgmtrans::core::ModulationConversionPolicy::SynthModulators;
}

void Settings::ConversionSettings::setModulationConversion(
    vgmtrans::core::ModulationConversionPolicy policy) const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  settings.setValue(QStringLiteral("modulationConversion"), static_cast<int>(policy));
  settings.endGroup();
  emit owner->conversionOptionsChanged();
}

vgmtrans::core::DynamicEnvelopePolicy Settings::ConversionSettings::dynamicEnvelopeConversion() const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  const int value =
      settings.value(QStringLiteral("dynamicEnvelopeConversion"),
                     static_cast<int>(vgmtrans::core::DynamicEnvelopePolicy::Ignore))
          .toInt();
  settings.endGroup();
  switch (static_cast<vgmtrans::core::DynamicEnvelopePolicy>(value)) {
    case vgmtrans::core::DynamicEnvelopePolicy::Ignore:
    case vgmtrans::core::DynamicEnvelopePolicy::InstrumentVariants:
      return static_cast<vgmtrans::core::DynamicEnvelopePolicy>(value);
  }
  return vgmtrans::core::DynamicEnvelopePolicy::Ignore;
}

void Settings::ConversionSettings::setDynamicEnvelopeConversion(
    vgmtrans::core::DynamicEnvelopePolicy policy) const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  settings.setValue(QStringLiteral("dynamicEnvelopeConversion"), static_cast<int>(policy));
  settings.endGroup();
  emit owner->conversionOptionsChanged();
}

bool Settings::ConversionSettings::exportOnlyUsedInstruments() const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  const bool enabled = settings.value(QStringLiteral("exportOnlyUsedInstruments"), true).toBool();
  settings.endGroup();
  return enabled;
}

void Settings::ConversionSettings::setExportOnlyUsedInstruments(bool enabled) const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  settings.setValue(QStringLiteral("exportOnlyUsedInstruments"), enabled);
  settings.endGroup();
  emit owner->conversionOptionsChanged();
}

vgmtrans::core::SampleFilteringPolicy Settings::ConversionSettings::sampleFiltering() const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  const auto fallback =
      settings.value(QStringLiteral("preserveSnesGaussianResponse"), true).toBool()
          ? vgmtrans::core::SampleFilteringPolicy::FormatPreferred
          : vgmtrans::core::SampleFilteringPolicy::None;
  const int value = settings.value(QStringLiteral("sampleFiltering"), static_cast<int>(fallback)).toInt();
  settings.endGroup();
  switch (static_cast<vgmtrans::core::SampleFilteringPolicy>(value)) {
    case vgmtrans::core::SampleFilteringPolicy::FormatPreferred:
    case vgmtrans::core::SampleFilteringPolicy::None:
    case vgmtrans::core::SampleFilteringPolicy::SnesDspLowPass:
      return static_cast<vgmtrans::core::SampleFilteringPolicy>(value);
  }
  return vgmtrans::core::SampleFilteringPolicy::FormatPreferred;
}

void Settings::ConversionSettings::setSampleFiltering(vgmtrans::core::SampleFilteringPolicy filtering) const {
  settings.beginGroup(QStringLiteral("ConversionOptions"));
  settings.setValue(QStringLiteral("sampleFiltering"), static_cast<int>(filtering));
  settings.remove(QStringLiteral("preserveSnesGaussianResponse"));
  settings.endGroup();
  emit owner->conversionOptionsChanged();
}

QStringList Settings::RecentFilesSettings::list() const {
  settings.beginGroup(QStringLiteral("RecentFiles"));
  const QStringList files = settings.value(QStringLiteral("files")).toStringList();
  settings.endGroup();
  return files;
}

void Settings::RecentFilesSettings::add(const QString& path) const {
  QStringList files = list();
  files.removeAll(path);
  files.prepend(path);
  while (files.size() > 10) {
    files.removeLast();
  }
  settings.beginGroup(QStringLiteral("RecentFiles"));
  settings.setValue(QStringLiteral("files"), files);
  settings.endGroup();
}

void Settings::RecentFilesSettings::clear() const {
  settings.beginGroup(QStringLiteral("RecentFiles"));
  settings.remove(QStringLiteral("files"));
  settings.endGroup();
}

QByteArray Settings::MainWindowSettings::windowGeometry() const {
  settings.beginGroup(QStringLiteral("MainWindow"));
  const QByteArray geometry = settings.value(QStringLiteral("geometry")).toByteArray();
  settings.endGroup();
  return geometry;
}

void Settings::MainWindowSettings::setWindowGeometry(const QByteArray& geometry) const {
  settings.beginGroup(QStringLiteral("MainWindow"));
  settings.setValue(QStringLiteral("geometry"), geometry);
  settings.endGroup();
}

QByteArray Settings::MainWindowSettings::dockState() const {
  settings.beginGroup(QStringLiteral("MainWindow"));
  const QByteArray state = settings.value(QStringLiteral("dockState")).toByteArray();
  settings.endGroup();
  return state;
}

void Settings::MainWindowSettings::setDockState(const QByteArray& dockState) const {
  settings.beginGroup(QStringLiteral("MainWindow"));
  settings.setValue(QStringLiteral("dockState"), dockState);
  settings.endGroup();
}

void Settings::MainWindowSettings::clearDockState() const {
  settings.beginGroup(QStringLiteral("MainWindow"));
  settings.remove(QStringLiteral("dockState"));
  settings.endGroup();
}

QByteArray Settings::MainWindowSettings::floatingDockGeometry(const QString& dockName) const {
  return settings.value(QStringLiteral("MainWindow/FloatingDocks/%1").arg(dockName)).toByteArray();
}

void Settings::MainWindowSettings::setFloatingDockGeometry(
    const QString& dockName, const QByteArray& geometry) const {
  settings.setValue(QStringLiteral("MainWindow/FloatingDocks/%1").arg(dockName), geometry);
}
