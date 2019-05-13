/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include "MainWindow.h"
#include "QtVGMRoot.h"

int main(int argc, char *argv[]) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

  QCoreApplication::setOrganizationName(QStringLiteral("VGMTrans"));
  QCoreApplication::setApplicationName(QStringLiteral("VGMTrans"));

  QApplication app(argc, argv);

  qtVGMRoot.Init();
  MainWindow window;
  window.show();

  return app.exec();
}
