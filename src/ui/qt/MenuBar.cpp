/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MenuBar.h"

#include <array>
#include <QDockWidget>
#include <QtGlobal>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QDir>
#include "Options.h"
#include "Root.h"
#include "LogManager.h"
#include "services/MenuManager.h"
#include "services/Settings.h"
#include "services/NotificationCenter.h"
#include "VGMItem.h"
#include "VGMColl.h"
#include "VGMFile.h"
#include "RawFile.h"


MenuBar::MenuBar(QWidget *parent, const QList<QDockWidget *> &dockWidgets) : QMenuBar(parent) {
  appendFileMenu();
  appendViewMenu(dockWidgets);
  appendOptionsMenu();
  appendInfoMenu();

  connect(NotificationCenter::the(), &NotificationCenter::vgmFileContextCommandsChanged,
          this, &MenuBar::handleVGMFileContextChange);
  connect(NotificationCenter::the(), &NotificationCenter::vgmCollContextCommandsChanged,
          this, &MenuBar::handleVGMCollContextChange);
  connect(NotificationCenter::the(), &NotificationCenter::rawFileContextCommandsChanged,
          this, &MenuBar::handleRawFileContextChange);
}

void MenuBar::appendFileMenu() {
  m_fileMenu = addMenu("File");
  m_topLevelMenus.insert("File", m_fileMenu);

  menu_open_file = m_fileMenu->addAction("Scan File");
  menu_open_file->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  connect(menu_open_file, &QAction::triggered, this, &MenuBar::openFile);

  menu_recent_files = m_fileMenu->addMenu("Scan Recent");
  updateRecentFilesMenu();

  menu_exit_separator = m_fileMenu->addSeparator();

  menu_app_exit = m_fileMenu->addAction("Exit");
  menu_app_exit->setShortcut(QKeySequence(QStringLiteral("Alt+F4")));
  menu_app_exit->setMenuRole(QAction::QuitRole);
  connect(menu_app_exit, &QAction::triggered, this, &MenuBar::exit);
}

void MenuBar::ensureExitActionAtBottom() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  return;
#endif

  if (!m_fileMenu || !menu_app_exit) {
    return;
  }

  if (menu_exit_separator) {
    m_fileMenu->removeAction(menu_exit_separator);
  }

  m_fileMenu->removeAction(menu_app_exit);

  if (!m_fileMenu->actions().isEmpty()) {
    if (!menu_exit_separator) {
      menu_exit_separator = new QAction(m_fileMenu);
      menu_exit_separator->setSeparator(true);
    }

    m_fileMenu->addAction(menu_exit_separator);
  }

  m_fileMenu->addAction(menu_app_exit);
}

void MenuBar::appendViewMenu(const QList<QDockWidget *> &dockWidgets) {
  m_viewMenu = addMenu("View");
  m_topLevelMenus.insert("View", m_viewMenu);

  auto *toolWindowsMenu = m_viewMenu->addMenu("Tool Windows");

  for (auto &widget : dockWidgets) {
    toolWindowsMenu->addAction(widget->toggleViewAction());
  }
}

void MenuBar::appendInfoMenu() {
  m_helpMenu = addMenu("Help");
  m_topLevelMenus.insert("Help", m_helpMenu);
  menu_about_dlg = m_helpMenu->addAction("About VGMTrans");
  connect(menu_about_dlg, &QAction::triggered, this, &MenuBar::showAbout);
}

void MenuBar::appendOptionsMenu() {
  m_optionsMenu = addMenu("Options");
  m_topLevelMenus.insert("Options", m_optionsMenu);
  auto bs = m_optionsMenu->addMenu("Bank select style");

  auto bankSelectStyle = Settings::the()->conversion.bankSelectStyle();

  QActionGroup *bs_grp = new QActionGroup(this);
  auto act = bs->addAction("GS (Default)");
  act->setCheckable(true);
  act->setChecked(bankSelectStyle == BankSelectStyle::GS);
  bs_grp->addAction(act);
  act = bs->addAction("MMA");
  act->setCheckable(true);
  act->setChecked(bankSelectStyle == BankSelectStyle::MMA);
  bs_grp->addAction(act);

  connect(bs_grp, &QActionGroup::triggered, [](const QAction *bs_style) {
    if (auto text = bs_style->text(); text == "GS (Default)") {
      Settings::the()->conversion.setBankSelectStyle(BankSelectStyle::GS);
    } else if (text == "MMA") {
      Settings::the()->conversion.setBankSelectStyle(BankSelectStyle::MMA);
      L_WARN("MMA style (CC0 * 128 + CC32) bank select was chosen and "
             "it will be used for bank select events in generated MIDIs. This "
             "will cause in-program playback to sound incorrect!");
    }
  });

  auto loopsMenu = m_optionsMenu->addMenu(tr("Sequence loops"));

  QActionGroup *loopsGroup = new QActionGroup(this);
  loopsGroup->setExclusive(true);

  auto addLoopOption = [loopsMenu, loopsGroup](const QString &label, int loops) {
    QAction *action = loopsMenu->addAction(label);
    action->setCheckable(true);
    action->setData(loops);
    loopsGroup->addAction(action);
    return action;
  };

  std::array presetLoopActions = {
      addLoopOption(tr("0 loops"), 0),
      addLoopOption(tr("1 loop"), 1),
      addLoopOption(tr("2 loops"), 2),
  };

  QAction *customLoopsAction = addLoopOption(tr("Custom..."), -1);

  auto updateLoopMenu = [this, presetLoopActions, customLoopsAction, loopsGroup]() mutable {
    const int currentLoops = Settings::the()->conversion.numSequenceLoops();
    QSignalBlocker blocker(loopsGroup);

    if (currentLoops >= 0 && currentLoops < static_cast<int>(presetLoopActions.size())) {
      customLoopsAction->setText(tr("Custom..."));
      presetLoopActions.at(static_cast<size_t>(currentLoops))->setChecked(true);
    } else {
      customLoopsAction->setText(tr("Custom (%1)").arg(currentLoops));
      customLoopsAction->setChecked(true);
    }
  };

  connect(loopsGroup, &QActionGroup::triggered, this, [this, presetLoopActions, customLoopsAction, loopsGroup, updateLoopMenu](QAction *action) mutable {
    const int selectedLoops = action->data().toInt();
    if (selectedLoops >= 0) {
      if (selectedLoops != Settings::the()->conversion.numSequenceLoops()) {
        Settings::the()->conversion.setNumSequenceLoops(selectedLoops);
      }
      updateLoopMenu();
      return;
    }

    bool ok = false;
    const int currentLoops = Settings::the()->conversion.numSequenceLoops();
    const int newLoops = QInputDialog::getInt(this, tr("Sequence loops"), tr("Number of loops"),
      currentLoops, 0, ConversionOptions::kMaxSequenceLoops, 1, &ok);
    if (ok) {
      Settings::the()->conversion.setNumSequenceLoops(newLoops);
    }

    updateLoopMenu();
  });

  connect(loopsMenu, &QMenu::aboutToShow, this, [updateLoopMenu]() mutable { updateLoopMenu(); });
  updateLoopMenu();

  act = m_optionsMenu->addAction("Skip MIDI channel 10");
  act->setCheckable(true);
  act->setChecked(Settings::the()->conversion.skipChannel10());
  connect(act, &QAction::toggled, [](bool skip) {
    if (!skip) {
      pRoot->UI_toast("Tracks using MIDI channel 10 will be silent during in-app playback.", ToastType::Info);
    }
    Settings::the()->conversion.setSkipChannel10(skip);
  });
}

void MenuBar::handleVGMFileContextChange(const QList<VGMFile*>& files) {
  m_selectedVGMFiles = files;
  m_selectedVGMColls.clear();
  m_selectedRawFiles.clear();
  refreshContextualMenus();
}

void MenuBar::handleVGMCollContextChange(const QList<VGMColl*>& colls) {
  m_selectedVGMColls = colls;
  m_selectedVGMFiles.clear();
  m_selectedRawFiles.clear();
  refreshContextualMenus();
}

void MenuBar::handleRawFileContextChange(const QList<RawFile*>& files) {
  m_selectedRawFiles = files;
  m_selectedVGMFiles.clear();
  m_selectedVGMColls.clear();
  refreshContextualMenus();
}

void MenuBar::refreshContextualMenus() {
  clearContextualMenus();

  if (!m_selectedRawFiles.isEmpty()) {
    auto items = std::make_shared<std::vector<RawFile*>>();
    items->reserve(m_selectedRawFiles.size());
    for (auto* file : m_selectedRawFiles) {
      if (file) {
        items->push_back(file);
      }
    }
    if (!items->empty()) {
      auto commands = MenuManager::the()->commandsByMenuForItems<RawFile>(items);
      appendContextualCommands<RawFile>(commands, items);
    }
    return;
  }

  if (!m_selectedVGMFiles.isEmpty()) {
    auto items = std::make_shared<std::vector<VGMFile*>>();
    items->reserve(m_selectedVGMFiles.size());
    for (auto* file : m_selectedVGMFiles) {
      if (file) {
        items->push_back(file);
      }
    }
    if (!items->empty()) {
      auto commands = MenuManager::the()->commandsByMenuForItems<VGMItem>(items);
      appendContextualCommands<VGMItem>(commands, items);
    }
    return;
  }

  if (!m_selectedVGMColls.isEmpty()) {
    auto items = std::make_shared<std::vector<VGMColl*>>();
    items->reserve(m_selectedVGMColls.size());
    for (auto* coll : m_selectedVGMColls) {
      if (coll) {
        items->push_back(coll);
      }
    }
    if (!items->empty()) {
      auto commands = MenuManager::the()->commandsByMenuForItems<VGMColl>(items);
      appendContextualCommands<VGMColl>(commands, items);
    }
  }
}

void MenuBar::clearContextualMenus() {
  for (auto& [menu, actions] : m_contextActions) {
    for (auto* action : actions) {
      if (menu && action) {
        menu->removeAction(action);
        action->deleteLater();
      }
    }
  }
  m_contextActions.clear();

  for (auto& [menu, separator] : m_contextSeparators) {
    if (menu && separator) {
      menu->removeAction(separator);
      separator->deleteLater();
    }
  }
  m_contextSeparators.clear();

  for (auto* submenu : m_dynamicSubmenus) {
    if (!submenu) {
      continue;
    }
    QMenu* parentMenu = qobject_cast<QMenu*>(submenu->parentWidget());
    if (!parentMenu) {
      parentMenu = qobject_cast<QMenu*>(submenu->parent());
    }
    if (parentMenu) {
      parentMenu->removeAction(submenu->menuAction());
    }
    submenu->deleteLater();
  }
  m_dynamicSubmenus.clear();

  for (auto* menu : m_dynamicTopLevelMenus) {
    if (!menu) {
      continue;
    }
    removeAction(menu->menuAction());
    m_topLevelMenus.remove(menu->title());
    menu->deleteLater();
  }
  m_dynamicTopLevelMenus.clear();
}

QMenu* MenuBar::ensureMenuForPath(const MenuManager::MenuPath& path) {
  if (path.empty()) {
    return nullptr;
  }

  const QString topLevelName = QString::fromStdString(path.front());
  QMenu* menu = nullptr;

  if (m_topLevelMenus.contains(topLevelName)) {
    menu = m_topLevelMenus.value(topLevelName);
  } else {
    menu = new QMenu(topLevelName, this);
    if (m_helpMenu) {
      insertMenu(m_helpMenu->menuAction(), menu);
    } else if (m_optionsMenu) {
      insertMenu(m_optionsMenu->menuAction(), menu);
    } else {
      addMenu(menu);
    }
    m_topLevelMenus.insert(topLevelName, menu);
    m_dynamicTopLevelMenus.push_back(menu);
  }

  QMenu* currentMenu = menu;
  for (size_t i = 1; i < path.size(); ++i) {
    const QString submenuTitle = QString::fromStdString(path[i]);
    QMenu* submenu = nullptr;
    const auto submenus = currentMenu->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* candidate : submenus) {
      if (candidate->title() == submenuTitle) {
        submenu = candidate;
        break;
      }
    }
    if (!submenu) {
      submenu = currentMenu->addMenu(submenuTitle);
      m_dynamicSubmenus.push_back(submenu);
    }
    currentMenu = submenu;
  }

  return currentMenu;
}

void MenuBar::updateRecentFilesMenu() {
  menu_recent_files->clear();
  auto files = Settings::the()->recentFiles.list();
  const QString homeDir = QDir::homePath();
  for (const auto& file : files) {
    QString display = file;
    if (display.startsWith(homeDir, Qt::CaseInsensitive)) {
      display.replace(0, homeDir.length(), "~");
    }
    auto act = menu_recent_files->addAction(display);
    connect(act, &QAction::triggered, this, [this, file]() { emit openRecentFile(file); });
  }
  if (!files.isEmpty()) {
    menu_recent_files->addSeparator();
    auto clear_act = menu_recent_files->addAction("Clear Items");
    connect(clear_act, &QAction::triggered, this, [this]() {
      Settings::the()->recentFiles.clear();
      updateRecentFilesMenu();
    });
  }
  menu_recent_files->setEnabled(!files.isEmpty());
}
