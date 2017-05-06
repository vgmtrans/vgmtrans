#include <QApplication>
#include <QtPlugin>
#include <QFile>

#include "ui/qt/MainWindow.h"
#include "ui/qt/QtUICallbacks.h"
#include "main/Core.h"

#ifdef Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

#ifdef Q_OS_MAC
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#endif

#include <QProxyStyle>

class MyProxyStyle : public QProxyStyle
{
 public:
  int styleHint(StyleHint hint, const QStyleOption *option = 0,
                const QWidget *widget = 0, QStyleHintReturn *returnData = 0) const
  {
    if (hint == QStyle::SH_ScrollBar_Transient)
      return true;
    return QProxyStyle::styleHint(hint, option, widget, returnData);
  }
};


int main(int argc, char *argv[])
{
  QApplication app(argc, argv);

  // Load qdarkstyle stylesheet
  QFile file(":/qdarkstyle/style.qss");
  file.open(QFile::ReadOnly);
  QString styleSheet = QLatin1String(file.readAll());

  QFile vgmtransStyleFile(":/vgmtransstyle/style.qss");
  vgmtransStyleFile.open(QFile::ReadOnly);
  QString test = QLatin1String(vgmtransStyleFile.readAll());
  styleSheet.append(test);
  app.setStyleSheet(styleSheet);

  app.setStyle(new MyProxyStyle);

  core.Init(&qtUICallbacks);

  MainWindow window;
  window.resize(900, 600);
  window.show();

  return app.exec();
}
//! [main() function]

