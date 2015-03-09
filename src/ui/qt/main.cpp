#include <QApplication>
#include "dropsitewindow.h"
#include "mainwindow.h"
#include "QtVGMRoot.h"

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

