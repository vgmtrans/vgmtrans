/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "MainWindow.h"

#include <QApplication>
#include <QWidget>

namespace QtUi {

inline MainWindow* findMainWindowForWidget(QWidget* origin) {
  for (QWidget* widget = origin; widget; widget = widget->parentWidget()) {
    if (auto* mainWindow = qobject_cast<MainWindow*>(widget)) {
      return mainWindow;
    }
  }

  const auto topLevelWidgets = QApplication::topLevelWidgets();
  for (QWidget* widget : topLevelWidgets) {
    if (auto* mainWindow = qobject_cast<MainWindow*>(widget)) {
      return mainWindow;
    }
  }

  return nullptr;
}

}  // namespace QtUi
