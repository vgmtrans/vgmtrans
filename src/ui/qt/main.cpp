#include <QApplication>
#include "dropsitewindow.h"
#include "mainwindow.h"
#include "QtVGMRoot.h"

//! [main() function]
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qtVGMRoot.Init();

    MainWindow window;
    window.show();
    return app.exec();
}
//! [main() function]

