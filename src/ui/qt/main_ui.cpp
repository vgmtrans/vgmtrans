/*
* VGMTrans (c) 2018
* Licensed under the zlib license, 
* refer to the included LICENSE.txt file
*/

#include <QApplication>
#include <QTextStream>
#include "MainWindow.h"
#include "QtVGMRoot.h"

int main(int argc, char *argv[]) {
  
  #if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  #endif

  QCoreApplication::setOrganizationName(QStringLiteral("VGMTrans"));
  QCoreApplication::setApplicationName(QStringLiteral("vgmtrans"));

  QApplication app(argc, argv);
  QFile f(":qdarkstyle/style.qss");
  f.open(QFile::ReadOnly | QFile::Text);
  QTextStream ts(&f);
  app.setStyleSheet(ts.readAll());
    
  qtVGMRoot.Init();
  MainWindow window;
  window.show();

  return app.exec();
}
