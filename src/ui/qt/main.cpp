#include <QApplication>
#include "dropsitewindow.h"

//! [main() function]
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    DropSiteWindow window;
    window.show();
    return app.exec();
}
//! [main() function]

