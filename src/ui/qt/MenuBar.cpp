/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MenuBar.h"

#include "services/Settings.h"

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

#include <QActionGroup>
#include <QDir>
#include <QDockWidget>
#include <QInputDialog>
#include <QMenu>
#include <QSignalBlocker>

namespace {

template <typename Enum, size_t Size, typename Setter>
void appendEnumOptions(QMenu* parent, const QString& title, Enum selected,
                       const std::array<std::pair<QString, Enum>, Size>& options, Setter setter) {
  QMenu* menu = parent->addMenu(title);
  auto* group = new QActionGroup(menu);
  group->setExclusive(true);
  for (const auto& [label, value] : options) {
    QAction* action = menu->addAction(label);
    action->setData(static_cast<int>(value));
    action->setCheckable(true);
    action->setChecked(value == selected);
    group->addAction(action);
  }
  QObject::connect(group, &QActionGroup::triggered, menu, [setter = std::move(setter)](QAction* action) {
    setter(static_cast<Enum>(action->data().toInt()));
  });
}

}  // namespace

MenuBar::MenuBar(QWidget* parent, const QList<QDockWidget*>& dockWidgets)
    : QMenuBar(parent) {
  appendFileMenu();
  appendViewMenu(dockWidgets);
  appendOptionsMenu();
  appendInfoMenu();
}

void MenuBar::setShortcutHost(QWidget* host) {
  m_shortcutHost = host;
}

void MenuBar::appendFileMenu() {
  m_fileMenu = addMenu(tr("File"));
  m_topLevelMenus.insert(m_fileMenu->title(), m_fileMenu);
  QAction* open = m_fileMenu->addAction(tr("Scan File"));
  open->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  connect(open, &QAction::triggered, this, &MenuBar::openFile);

  m_recentFilesMenu = m_fileMenu->addMenu(tr("Scan Recent"));
  updateRecentFilesMenu();

  m_exitSeparator = m_fileMenu->addSeparator();
  m_exitAction = m_fileMenu->addAction(tr("Exit"));
  m_exitAction->setShortcut(QKeySequence(QStringLiteral("Alt+F4")));
  m_exitAction->setMenuRole(QAction::QuitRole);
  connect(m_exitAction, &QAction::triggered, this, &MenuBar::exit);
}

void MenuBar::appendViewMenu(const QList<QDockWidget*>& dockWidgets) {
  m_viewMenu = addMenu(tr("View"));
  m_topLevelMenus.insert(m_viewMenu->title(), m_viewMenu);
  for (QDockWidget* dock : dockWidgets) {
    m_viewMenu->addAction(dock->toggleViewAction());
  }
  m_viewMenu->addSeparator();
  QAction* reset = m_viewMenu->addAction(tr("Reset Dock Layout"));
  connect(reset, &QAction::triggered, this, &MenuBar::resetDockLayout);
  m_viewMenu->addSeparator();

  m_increaseHexFont = m_viewMenu->addAction(tr("Increase Font Size in Hex View"));
  m_increaseHexFont->setShortcut(QKeySequence::ZoomIn);
  m_increaseHexFont->setShortcutContext(Qt::WidgetShortcut);
  connect(m_increaseHexFont, &QAction::triggered, this,
          &MenuBar::increaseHexFontRequested);

  m_decreaseHexFont = m_viewMenu->addAction(tr("Decrease Font Size in Hex View"));
  m_decreaseHexFont->setShortcut(QKeySequence::ZoomOut);
  m_decreaseHexFont->setShortcutContext(Qt::WidgetShortcut);
  connect(m_decreaseHexFont, &QAction::triggered, this,
          &MenuBar::decreaseHexFontRequested);

  m_resetHexFont = m_viewMenu->addAction(tr("Reset Font Size in Hex View"));
  m_resetHexFont->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
  m_resetHexFont->setShortcutContext(Qt::WidgetShortcut);
  connect(m_resetHexFont, &QAction::triggered, this,
          &MenuBar::resetHexFontRequested);
  setHexViewAvailable(false);
#if defined(Q_OS_MACOS)
  // Add a separator between these actions and the automatically added "Enter Full Screen" action.
  m_viewMenu->addSeparator();
#endif
}

void MenuBar::setHexViewAvailable(bool available) {
  m_increaseHexFont->setEnabled(available);
  m_decreaseHexFont->setEnabled(available);
  m_resetHexFont->setEnabled(available);
}

void MenuBar::appendOptionsMenu() {
  m_optionsMenu = addMenu(tr("Options"));
  m_topLevelMenus.insert(m_optionsMenu->title(), m_optionsMenu);
  QMenu* bank = m_optionsMenu->addMenu(tr("Bank Select Style"));
  auto* bankGroup = new QActionGroup(bank);
  bankGroup->setExclusive(true);
  const BankSelectStyle savedStyle = Settings::the()->conversion.bankSelectStyle();
  for (const auto& option : std::array<std::pair<const char*, BankSelectStyle>, 2>{{
           {"GS (Default)", BankSelectStyle::GS}, {"MMA", BankSelectStyle::MMA}}}) {
    QAction* action = bank->addAction(tr(option.first));
    action->setData(static_cast<int>(option.second));
    action->setCheckable(true);
    action->setChecked(option.second == savedStyle);
    bankGroup->addAction(action);
  }
  connect(bankGroup, &QActionGroup::triggered, this, [](QAction* action) {
    const auto style = static_cast<BankSelectStyle>(action->data().toInt());
    Settings::the()->conversion.setBankSelectStyle(style);
    if (style == BankSelectStyle::MMA) {
      qWarning("MMA style (CC0 * 128 + CC32) bank select was chosen and "
               "it will be used for bank select events in generated MIDIs. This "
               "will cause in-program playback to sound incorrect!");
    }
  });

  using vgmtrans::core::MidiPitchTransitionRendering;
  appendEnumOptions(
      m_optionsMenu, tr("Pitch Transition Rendering"),
      Settings::the()->conversion.pitchTransitionRendering(),
      std::array{
          std::pair{tr("Preserve Format (Default)"), MidiPitchTransitionRendering::PreserveFormat},
          std::pair{tr("Portamento"), MidiPitchTransitionRendering::Portamento},
          std::pair{tr("Pitch Bend"), MidiPitchTransitionRendering::PitchBend},
      },
      [](MidiPitchTransitionRendering rendering) {
        Settings::the()->conversion.setPitchTransitionRendering(rendering);
      });

  using vgmtrans::core::ModulationConversionPolicy;
  appendEnumOptions(
      m_optionsMenu, tr("Modulation Conversion"),
      Settings::the()->conversion.modulationConversion(),
      std::array{
          std::pair{tr("Synth Modulators (Default)"), ModulationConversionPolicy::SynthModulators},
          std::pair{tr("Sequence Event Simulation"), ModulationConversionPolicy::SequenceEventSimulation},
      },
      [](ModulationConversionPolicy conversion) {
        Settings::the()->conversion.setModulationConversion(conversion);
      });

  QMenu* loops = m_optionsMenu->addMenu(tr("Sequence Loops"));
  auto* loopsGroup = new QActionGroup(loops);
  loopsGroup->setExclusive(true);
  const int savedLoops = Settings::the()->conversion.numSequenceLoops();
  const std::array loopOptions{
      std::pair{tr("0 Loops"), 0},
      std::pair{tr("1 Loop"), 1},
      std::pair{tr("2 Loops"), 2},
  };
  std::array<QAction*, 3> presetLoopActions{};
  size_t loopIndex = 0;
  for (const auto& [label, count] : loopOptions) {
    QAction* action = loops->addAction(label);
    action->setData(count);
    action->setCheckable(true);
    action->setChecked(savedLoops == count);
    loopsGroup->addAction(action);
    presetLoopActions[loopIndex++] = action;
  }
  QAction* custom = loops->addAction(tr("Custom..."));
  custom->setData(-1);
  custom->setCheckable(true);
  custom->setChecked(savedLoops > 2);
  if (savedLoops > 2) {
    custom->setText(tr("Custom (%1)").arg(savedLoops));
  }
  loopsGroup->addAction(custom);

  const auto updateLoopMenu = [presetLoopActions, custom, loopsGroup]() {
    const int currentLoops = Settings::the()->conversion.numSequenceLoops();
    QSignalBlocker blocker(loopsGroup);
    if (currentLoops >= 0 && currentLoops < static_cast<int>(presetLoopActions.size())) {
      custom->setText(tr("Custom..."));
      presetLoopActions[static_cast<size_t>(currentLoops)]->setChecked(true);
    } else {
      custom->setText(tr("Custom (%1)").arg(currentLoops));
      custom->setChecked(true);
    }
  };

  connect(loopsGroup, &QActionGroup::triggered, this, [this, updateLoopMenu](QAction* action) {
    int value = action->data().toInt();
    if (value < 0) {
      bool accepted = false;
      value = QInputDialog::getInt(this, tr("Sequence loops"), tr("Number of loops"),
                                   Settings::the()->conversion.numSequenceLoops(), 0,
                                   Settings::ConversionSettings::kMaxSequenceLoops, 1, &accepted);
      if (!accepted) {
        updateLoopMenu();
        return;
      }
    }
    Settings::the()->conversion.setNumSequenceLoops(value);
    updateLoopMenu();
  });
  connect(loops, &QMenu::aboutToShow, this, updateLoopMenu);
  updateLoopMenu();

  QAction* onlyUsedInstruments = m_optionsMenu->addAction(tr("Export used instrument data only"));
  onlyUsedInstruments->setCheckable(true);
  onlyUsedInstruments->setChecked(Settings::the()->conversion.exportOnlyUsedInstruments());
  connect(onlyUsedInstruments, &QAction::toggled, this, [](bool checked) {
    Settings::the()->conversion.setExportOnlyUsedInstruments(checked);
  });

  QAction* skipChannel10 = m_optionsMenu->addAction(tr("Skip MIDI channel 10"));
  skipChannel10->setCheckable(true);
  skipChannel10->setChecked(Settings::the()->conversion.skipChannel10());
  connect(skipChannel10, &QAction::toggled, this, [this](bool checked) {
    Settings::the()->conversion.setSkipChannel10(checked);
    if (!checked) {
      qWarning("Tracks using MIDI channel 10 will be silent during in-app playback.");
      emit showToastRequested(
          tr("Tracks using MIDI channel 10 will be silent during in-app playback."),
          ToastType::Info, 3000);
    }
  });
}

void MenuBar::appendInfoMenu() {
  m_helpMenu = addMenu(tr("Help"));
  m_topLevelMenus.insert(m_helpMenu->title(), m_helpMenu);
  QAction* report = m_helpMenu->addAction(tr("Report a Bug"));
  connect(report, &QAction::triggered, this, &MenuBar::reportBug);
  m_helpMenu->addSeparator();
  QAction* about = m_helpMenu->addAction(tr("About VGMTrans"));
  about->setMenuRole(QAction::AboutRole);
  connect(about, &QAction::triggered, this, &MenuBar::showAbout);
}

void MenuBar::reportBug() {
  emit reportBugRequested();
}

void MenuBar::setContext(Context context) {
  clearContextualMenus();
  appendContextualCommands(context);
}

void MenuBar::appendContextualCommands(Context context) {
  if (context == Context::None) {
    return;
  }

  const auto addAction = [this](const QStringList& path, const QString& text,
                                bool enabled, const QList<QKeySequence>& shortcuts,
                                const std::function<void()>& invoke = {}) {
    QMenu* target = ensureMenuForPath(path);
    if (target == nullptr) {
      return static_cast<QAction*>(nullptr);
    }
    if (m_contextActions[target].empty() &&
        std::find(m_dynamicTopLevelMenus.begin(), m_dynamicTopLevelMenus.end(), target) ==
            m_dynamicTopLevelMenus.end()) {
      QAction* separator = target->addSeparator();
      m_contextSeparators[target] = separator;
    }
    QAction* action = target->addAction(text);
    action->setEnabled(enabled);
    action->setShortcuts(shortcuts);
    if (invoke) {
      connect(action, &QAction::triggered, this, invoke);
    }
    if (m_shortcutHost != nullptr) {
      m_shortcutHost->addAction(action);
    }
    m_contextActions[target].push_back(action);
    if (target == m_fileMenu) {
      ensureExitActionAtBottom();
    }
    return action;
  };
  const auto addSeparator = [this](const QStringList& path) {
    if (QMenu* target = ensureMenuForPath(path)) {
      QAction* separator = target->addSeparator();
      m_contextActions[target].push_back(separator);
    }
  };

  const QStringList convert{tr("Convert")};
  const QStringList file{tr("File")};
  if (context == Context::Source) {
    addAction(convert, tr("Save as Original Format"), true, {},
              [this] { emit saveSelectedSourceOriginal(); });
    QAction* close = addAction(file, tr("Close"), true, {Qt::Key_Backspace, Qt::Key_Delete},
                               [this] { emit closeSelectedSources(); });
    close->setShortcutContext(Qt::WidgetShortcut);
    return;
  }

  if (context == Context::Sequence) {
    addAction(convert, tr("Save as MIDI"), true, {},
              [this] { emit exportSelectedSequenceMidi(); });
    addAction(convert, tr("Save as Original Format"), true, {},
              [this] { emit saveSelectedAssetOriginal(); });
    addSeparator(convert);
    addAction(convert, tr("Stitch"), false, {});
  } else if (context == Context::InstrumentSet) {
    addAction(convert, tr("Save as SF2"), true, {},
              [this] { emit exportSelectedInstrumentSetSf2(); });
    addAction(convert, tr("Save as DLS"), true, {},
              [this] { emit exportSelectedInstrumentSetDls(); });
    addAction(convert, tr("Save as Original Format"), true, {},
              [this] { emit saveSelectedAssetOriginal(); });
  } else if (context == Context::SampleCollection) {
    addAction(convert, tr("Save all samples as WAV"), false, {});
    addAction(convert, tr("Save as Original Format"), true, {},
              [this] { emit saveSelectedAssetOriginal(); });
  } else if (context == Context::Misc) {
    addAction(convert, tr("Save as Original Format"), true, {},
              [this] { emit saveSelectedAssetOriginal(); });
  } else if (context == Context::Collection) {
    addAction(convert, tr("Export as MIDI and SF2"), true, {},
              [this] { emit exportSelectedCollection(0); });
    addAction(convert, tr("Export as MIDI and DLS"), true, {},
              [this] { emit exportSelectedCollection(1); });
    addAction(convert, tr("Export as MIDI, SF2, and DLS"), true, {},
              [this] { emit exportSelectedCollection(2); });
    addSeparator(convert);
    addAction(convert, tr("Stitch"), false, {});
    return;
  }

  addAction(file, tr("Open Analysis"), true, {Qt::Key_Return},
            [this] { emit openSelectedAsset(); });
  QAction* remove = addAction(file, tr("Remove"), true, {Qt::Key_Backspace, Qt::Key_Delete},
                              [this] { emit removeSelectedAssets(); });
  remove->setShortcutContext(Qt::WidgetShortcut);
}

void MenuBar::clearContextualMenus() {
  for (auto& [menu, actions] : m_contextActions) {
    for (QAction* action : actions) {
      if (m_shortcutHost != nullptr && action != nullptr) {
        m_shortcutHost->removeAction(action);
      }
      if (menu != nullptr && action != nullptr) {
        menu->removeAction(action);
        action->deleteLater();
      }
    }
  }
  m_contextActions.clear();

  for (auto& [menu, separator] : m_contextSeparators) {
    if (menu != nullptr && separator != nullptr) {
      menu->removeAction(separator);
      separator->deleteLater();
    }
  }
  m_contextSeparators.clear();

  for (QMenu* submenu : m_dynamicSubmenus) {
    if (submenu == nullptr) {
      continue;
    }
    QMenu* parentMenu = qobject_cast<QMenu*>(submenu->parentWidget());
    if (parentMenu == nullptr) {
      parentMenu = qobject_cast<QMenu*>(submenu->parent());
    }
    if (parentMenu != nullptr) {
      parentMenu->removeAction(submenu->menuAction());
    }
    submenu->deleteLater();
  }
  m_dynamicSubmenus.clear();

  for (QMenu* menu : m_dynamicTopLevelMenus) {
    if (menu == nullptr) {
      continue;
    }
    removeAction(menu->menuAction());
    m_topLevelMenus.remove(menu->title());
    menu->deleteLater();
  }
  m_dynamicTopLevelMenus.clear();
  ensureExitActionAtBottom();
}

QMenu* MenuBar::ensureMenuForPath(const QStringList& path) {
  if (path.isEmpty()) {
    return nullptr;
  }

  QMenu* current = m_topLevelMenus.value(path.front(), nullptr);
  if (current == nullptr) {
    current = new QMenu(path.front(), this);
    insertMenu(m_optionsMenu->menuAction(), current);
    m_topLevelMenus.insert(path.front(), current);
    m_dynamicTopLevelMenus.push_back(current);
  }

  for (qsizetype index = 1; index < path.size(); ++index) {
    QMenu* submenu = nullptr;
    const auto candidates = current->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
    for (QMenu* candidate : candidates) {
      if (candidate->title() == path[index]) {
        submenu = candidate;
        break;
      }
    }
    if (submenu == nullptr) {
      submenu = current->addMenu(path[index]);
      m_dynamicSubmenus.push_back(submenu);
    }
    current = submenu;
  }
  return current;
}

void MenuBar::ensureExitActionAtBottom() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  return;
#endif
  if (m_fileMenu == nullptr || m_exitAction == nullptr) {
    return;
  }
  if (m_exitSeparator != nullptr) {
    m_fileMenu->removeAction(m_exitSeparator);
  }
  m_fileMenu->removeAction(m_exitAction);
  if (!m_fileMenu->actions().isEmpty()) {
    if (m_exitSeparator == nullptr) {
      m_exitSeparator = new QAction(m_fileMenu);
      m_exitSeparator->setSeparator(true);
    }
    m_fileMenu->addAction(m_exitSeparator);
  }
  m_fileMenu->addAction(m_exitAction);
}

void MenuBar::updateRecentFilesMenu() {
  m_recentFilesMenu->clear();
  const QStringList files = Settings::the()->recentFiles.list();
  const QString home = QDir::homePath();
  for (const QString& file : files) {
    QString display = file;
    if (display.startsWith(home, Qt::CaseInsensitive)) {
      display.replace(0, home.size(), QStringLiteral("~"));
    }
    QAction* action = m_recentFilesMenu->addAction(display);
    connect(action, &QAction::triggered, this,
            [this, file] { emit openRecentFile(file); });
  }
  m_recentFilesMenu->addSeparator();
  QAction* clear = m_recentFilesMenu->addAction(tr("Clear Items"));
  clear->setEnabled(!files.isEmpty());
  connect(clear, &QAction::triggered, this, [this] {
    Settings::the()->recentFiles.clear();
    updateRecentFilesMenu();
  });
  m_recentFilesMenu->setEnabled(!files.isEmpty());
}
