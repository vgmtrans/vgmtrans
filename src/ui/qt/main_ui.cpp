#include <QApplication>
#include <QtPlugin>
#include <QFile>
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
//    QFile file(":/qss/default.qss");
    QFile file(":/qdarkstyle/style.qss");
    file.open(QFile::ReadOnly);
    QString styleSheet = QLatin1String(file.readAll());
    qApp->setStyleSheet(styleSheet);

    qtVGMRoot.Init();

    MainWindow window;
    window.resize(900, 600);
    window.show();
    return app.exec();
}
//! [main() function]

