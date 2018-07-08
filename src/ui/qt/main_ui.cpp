/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include "MainWindow.h"
#include "QtVGMRoot.h"

#ifdef Q_OS_WIN
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

int main(int argc, char *argv[]) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

  QCoreApplication::setOrganizationName(QStringLiteral("VGMTrans"));
  QCoreApplication::setApplicationName(QStringLiteral("vgmtrans"));

  QApplication app(argc, argv);

  qtVGMRoot.Init();
  MainWindow window;
  window.show();

  return app.exec();
}
