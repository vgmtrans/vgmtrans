/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "main/base/ToastType.h"

#include <map>
#include <vector>

#include <QList>
#include <QMap>
#include <QMenuBar>
#include <QPointer>
#include <QStringList>

class QDockWidget;
class QMenu;
class QWidget;

class MenuBar final : public QMenuBar {
  Q_OBJECT

public:
  enum class Context {
    None,
    Source,
    Sequence,
    SoundBank,
    SamplePool,
    Misc,
    Collection,
  };

  explicit MenuBar(QWidget* parent = nullptr, const QList<QDockWidget*>& dockWidgets = {});
  void updateRecentFilesMenu();
  void setShortcutHost(QWidget* host);
  void setContext(Context context);
  void setHexViewAvailable(bool available);

signals:
  void openFile();
  void openRecentFile(const QString& filename);
  void exit();
  void showAbout();
  void reportBugRequested();
  void resetDockLayout();
  void closeSelectedSources();
  void removeSelectedAssets();
  void openSelectedAsset();
  void saveSelectedSourceOriginal();
  void saveSelectedAssetOriginal();
  void exportSelectedSequenceMidi();
  void exportSelectedSoundBankSf2();
  void exportSelectedSoundBankDls();
  void exportSelectedSamplesWav();
  void exportSelectedCollection(int choice);
  void stitchSelectedCollections();
  void increaseHexFontRequested();
  void decreaseHexFontRequested();
  void resetHexFontRequested();
  void showToastRequested(const QString& message, ToastType type, int durationMs);

private:
  void appendFileMenu();
  void appendViewMenu(const QList<QDockWidget*>& dockWidgets);
  void appendOptionsMenu();
  void appendInfoMenu();
  void reportBug();
  void appendContextualCommands(Context context);
  void clearContextualMenus();
  QMenu* ensureMenuForPath(const QStringList& path);
  void ensureExitActionAtBottom();

  QMenu* m_fileMenu{};
  QMenu* m_viewMenu{};
  QMenu* m_recentFilesMenu{};
  QMenu* m_optionsMenu{};
  QMenu* m_helpMenu{};
  QAction* m_exitSeparator{};
  QAction* m_exitAction{};
  QAction* m_increaseHexFont{};
  QAction* m_decreaseHexFont{};
  QAction* m_resetHexFont{};

  QMap<QString, QMenu*> m_topLevelMenus;
  std::vector<QMenu*> m_dynamicTopLevelMenus;
  std::vector<QMenu*> m_dynamicSubmenus;
  std::map<QMenu*, std::vector<QAction*>> m_contextActions;
  std::map<QMenu*, QAction*> m_contextSeparators;
  QPointer<QWidget> m_shortcutHost;
};
