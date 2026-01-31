/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <QApplication>
#include <QFile>
#include <QFileOpenEvent>
#include <QFontDatabase>
#include <QStyleFactory>
#include <filesystem>
#include "MainWindow.h"
#include "QtVGMRoot.h"

class VGMTransApplication final : public QApplication {
public:
  using QApplication::QApplication;

protected:
  bool event(QEvent* event) override {
    if (event->type() == QEvent::FileOpen) {
      auto* fileEvent = static_cast<QFileOpenEvent*>(event);
      qtVGMRoot.openRawFile(std::filesystem::path(fileEvent->file().toStdWString()));

      return true;
    }
    return QApplication::event(event);
  }
};

int main(int argc, char *argv[]) {
  QCoreApplication::setOrganizationName("VGMTrans");
  QCoreApplication::setOrganizationDomain("vgmtrans.com");
  QCoreApplication::setApplicationName("VGMTrans");

  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::Round);
  QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);

  VGMTransApplication app(argc, argv);
  #ifdef _WIN32
  app.setStyle(QStyleFactory::create("fusion"));
  #endif
  qtVGMRoot.init();

  QFontDatabase::addApplicationFont(":/fonts/Roboto_Mono/RobotoMono-VariableFont_wght.ttf");

  MainWindow window;
  window.show();

  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); ++i) {
    const QString& s = args.at(i);
    qtVGMRoot.openRawFile(std::filesystem::path(s.toStdWString()));
  }

  return app.exec();
}
