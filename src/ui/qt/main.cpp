#include <QApplication>
#include <QtPlugin>
#include "dropsitewindow.h"
#include "mainwindow.h"
#include "QtVGMRoot.h"

#ifdef Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

#ifdef Q_OS_MAC
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#endif


//! [main() function]
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qApp->setStyleSheet("QSplitter::handle{background-color: #B8B8B8;}");

    qtVGMRoot.Init();

    MainWindow window;
    window.show();
    return app.exec();
}
//! [main() function]

