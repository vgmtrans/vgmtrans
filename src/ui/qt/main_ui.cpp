/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <QtGlobal>
#include <QApplication>
#include <QtPlugin>
#include <QFile>
#include <QFontDatabase>
#include <QStyleFactory>
#include "MainWindow.h"
#include "QtVGMRoot.h"

int main(int argc, char *argv[]) {
  QCoreApplication::setOrganizationName("VGMTrans");
  QCoreApplication::setApplicationName("VGMTrans");

  QApplication app(argc, argv);
  #ifdef _WIN32
  app.setStyle(QStyleFactory::create("fusion"));
  #endif
  qtVGMRoot.Init();

  QFontDatabase::addApplicationFont(":/fonts/Roboto_Mono/RobotoMono-VariableFont_wght.ttf");

  MainWindow window;
  window.show();

  return app.exec();
}
