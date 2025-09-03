/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QActionGroup>
#include <QList>
#include <QString>
#include <QMap>
#include <QMenu>
#include <QMenuBar>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

#include "services/MenuManager.h"

class QDockWidget;
class VGMFile;
class VGMColl;
class RawFile;

class MenuBar final : public QMenuBar {
  Q_OBJECT

public:
  explicit MenuBar(QWidget *parent = nullptr, const QList<QDockWidget *>& dockWidgets = {});
  void updateRecentFilesMenu();

signals:
  void openFile();
  void openRecentFile(const QString& filename);
  void exit();
  void showAbout();

private slots:
  void handleVGMFileContextChange(const QList<VGMFile*>& files);
  void handleVGMCollContextChange(const QList<VGMColl*>& colls);
  void handleRawFileContextChange(const QList<RawFile*>& files);

private:
  void appendFileMenu();
  void appendViewMenu(const QList<QDockWidget *> &dockWidgets);
  void appendInfoMenu();
  void appendOptionsMenu();

  void refreshContextualMenus();
  void clearContextualMenus();
  QMenu* ensureMenuForPath(const MenuManager::MenuPath& path);
  void ensureExitActionAtBottom();

  template <typename Base, typename T>
  void appendContextualCommands(const MenuManager::MenuCommandMap& commands,
                                std::shared_ptr<std::vector<T*>> items);

  // Static menus
  QMenu *m_fileMenu{};
  QMenu *m_viewMenu{};
  QMenu *m_optionsMenu{};
  QMenu *m_helpMenu{};

  // File actions
  QAction *menu_open_file{};
  QMenu *menu_recent_files;
  QAction *menu_exit_separator{};
  QAction *menu_app_exit{};

  // Info actions
  QAction *menu_about_dlg{};

  // Options actions
  QActionGroup *menu_drivers{};

  QMap<QString, QMenu*> m_topLevelMenus;
  std::vector<QMenu*> m_dynamicTopLevelMenus;
  std::vector<QMenu*> m_dynamicSubmenus;
  std::map<QMenu*, std::vector<QAction*>> m_contextActions;
  std::map<QMenu*, QAction*> m_contextSeparators;

  QList<VGMFile*> m_selectedVGMFiles;
  QList<VGMColl*> m_selectedVGMColls;
  QList<RawFile*> m_selectedRawFiles;
};

template <typename Base, typename T>
void MenuBar::appendContextualCommands(const MenuManager::MenuCommandMap& commands,
                                       std::shared_ptr<std::vector<T*>> items) {
  for (const auto& [path, commandList] : commands) {
    if (path.empty()) {
      continue;
    }

    QMenu* targetMenu = ensureMenuForPath(path);
    if (!targetMenu) {
      continue;
    }

    const bool isExistingMenu = m_topLevelMenus.contains(QString::fromStdString(path.front())) &&
                                std::find(m_dynamicTopLevelMenus.begin(), m_dynamicTopLevelMenus.end(), targetMenu) ==
                                    m_dynamicTopLevelMenus.end();

    if (isExistingMenu) {
      auto separator = targetMenu->addSeparator();
      m_contextSeparators[targetMenu] = separator;
    }

    auto actions = MenuManager::the()->createActionsForCommands<Base>(commandList, items, targetMenu, false);
    auto& storedActions = m_contextActions[targetMenu];
    storedActions.insert(storedActions.end(), actions.begin(), actions.end());

    if (targetMenu == m_fileMenu) {
      ensureExitActionAtBottom();
    }
  }
}
