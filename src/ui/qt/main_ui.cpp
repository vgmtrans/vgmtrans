/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <QApplication>
#include <QtPlugin>
#include <QFile>
#include "MainWindow.h"
#include "QtVGMRoot.h"

int main(int argc, char *argv[]) {
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
  QApplication app(argc, argv);

  qtVGMRoot.Init();

  MainWindow window;
  window.resize(900, 600);
  window.show();
  return app.exec();
}
