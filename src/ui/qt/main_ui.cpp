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
#include "MainWindow.h"
#include "QtVGMRoot.h"

int main(int argc, char *argv[]) {
  QCoreApplication::setOrganizationName("VGMTrans");
  QCoreApplication::setApplicationName("VGMTrans");

  QApplication app(argc, argv);
  qtVGMRoot.Init();

  QFontDatabase::addApplicationFont(":/fonts/Ubuntu_Mono/UbuntuMono-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/Ubuntu_Mono/UbuntuMono-Bold.ttf");
  QFontDatabase::addApplicationFont(":/fonts/Ubuntu_Mono/UbuntuMono-Italic.ttf");
  QFontDatabase::addApplicationFont(":/fonts/Ubuntu_Mono/UbuntuMono-BoldItalic.ttf");

  MainWindow window;
  window.show();

  return app.exec();
}
