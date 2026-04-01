/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <QApplication>
#include <QFile>
#include <QFileOpenEvent>
#include <QFontDatabase>
#if defined(Q_OS_LINUX) && QT_CONFIG(opengl)
#include <QRhiWidget>
#endif
#include <QTimer>
#include <filesystem>
#include "MainWindow.h"
#include "QtVGMRoot.h"
#include "widgets/Windows11MenuProxyStyle.h"

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

  // Prevent native sibling promotion so dock splitters remain responsive with the RHI QWindow container.
  QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);

  VGMTransApplication app(argc, argv);

#ifdef Q_OS_WIN
  if (QStyle *style = app.style();
      style && (style->inherits("QWindows11Style") ||
                style->name().compare(QStringLiteral("windows11"), Qt::CaseInsensitive) == 0)) {
    app.setStyle(new Windows11MenuProxyStyle(style->name()));
  }

  QFont font = app.font();
  if (font.pointSizeF() > 0.0) {
    font.setPointSizeF(font.pointSizeF() + 1.0);
  } else if (font.pixelSize() > 0) {
    font.setPixelSize(font.pixelSize() + 1);
  }
  app.setFont(font);
#endif

  qtVGMRoot.init();

  QFontDatabase::addApplicationFont(":/fonts/Roboto_Mono/RobotoMono-VariableFont_wght.ttf");

  MainWindow window;

#if defined(Q_OS_LINUX) && QT_CONFIG(opengl)
  // Prime QRhiWidget once at startup to avoid first-use window re-creation. Not necessary for other platforms
  // where we use a QWindow instead of QRhiWidget.
  auto* rhiPrimer = new QRhiWidget(&window);
  rhiPrimer->setApi(QRhiWidget::Api::OpenGL);
  rhiPrimer->hide();
#endif

  window.show();

#if defined(Q_OS_LINUX) && QT_CONFIG(opengl)
  QTimer::singleShot(0, rhiPrimer, &QObject::deleteLater);
#endif

  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); ++i) {
    const QString& s = args.at(i);
    qtVGMRoot.openRawFile(std::filesystem::path(s.toStdWString()));
  }

  return app.exec();
}
