/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MainWindow.h"
#include "application/WorkspaceController.h"
#include "widgets/Windows11ProxyStyle.h"

#include <QApplication>
#include <QByteArray>
#include <QFileOpenEvent>
#include <QFont>
#include <QFontDatabase>
#include <QStyle>
#if defined(VGMTRANS_VALUE_HEX_VIEW) && defined(Q_OS_LINUX) && QT_CONFIG(opengl)
#include <QRhiWidget>
#include <QTimer>
#endif

#include <array>
#include <filesystem>
#include <vector>

namespace {

std::filesystem::path filePath(const QString& path) {
#ifdef Q_OS_WIN
  return std::filesystem::path(path.toStdWString());
#else
  const QByteArray utf8 = path.toUtf8();
  return std::filesystem::path(utf8.constData(), utf8.constData() + utf8.size());
#endif
}

class VGMTransApplication final : public QApplication {
public:
  using QApplication::QApplication;

  void attach(MainWindow& window) {
    window_ = &window;
    if (!pendingPaths_.empty()) {
      window_->openPaths(pendingPaths_);
      pendingPaths_.clear();
    }
  }

protected:
  bool event(QEvent* event) override {
    if (event->type() != QEvent::FileOpen) {
      return QApplication::event(event);
    }

    const auto* fileEvent = static_cast<QFileOpenEvent*>(event);
    const std::array path{filePath(fileEvent->file())};
    if (window_ != nullptr) {
      window_->openPaths(path);
    } else {
      pendingPaths_.push_back(path.front());
    }
    return true;
  }

private:
  MainWindow* window_ = nullptr;
  std::vector<std::filesystem::path> pendingPaths_;
};

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication::setOrganizationName(QStringLiteral("VGMTrans"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("vgmtrans.com"));
  QCoreApplication::setApplicationName(QStringLiteral("VGMTrans"));
  // Prevent native sibling promotion so dock splitters remain responsive when
  // the value-native inspector reconnects its RHI QWindow container.
  QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);

  VGMTransApplication app(argc, argv);

#ifdef Q_OS_WIN
  if (QStyle* style = app.style();
      style && (style->inherits("QWindows11Style") ||
                style->name().compare(QStringLiteral("windows11"), Qt::CaseInsensitive) == 0)) {
    app.setStyle(new Windows11ProxyStyle(style->name()));
  } else {
    app.setStyle(new Windows11ProxyStyle(QStringLiteral("fusion")));
  }
  QFont font = app.font();
  if (font.pointSizeF() > 0.0) {
    font.setPointSizeF(font.pointSizeF() + 1.0);
  } else if (font.pixelSize() > 0) {
    font.setPixelSize(font.pixelSize() + 1);
  }
  app.setFont(font);
#endif

  QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Roboto_Mono/RobotoMono-VariableFont_wght.ttf"));

  vgmtrans::ui::WorkspaceController workspace;
  MainWindow window(workspace);
  app.attach(window);

#if defined(VGMTRANS_VALUE_HEX_VIEW) && defined(Q_OS_LINUX) && QT_CONFIG(opengl)
  // Prime QRhiWidget once at startup to avoid first-use window re-creation.
  auto* rhiPrimer = new QRhiWidget(&window);
  rhiPrimer->setApi(QRhiWidget::Api::OpenGL);
  rhiPrimer->hide();
#endif
  window.show();

#if defined(VGMTRANS_VALUE_HEX_VIEW) && defined(Q_OS_LINUX) && QT_CONFIG(opengl)
  QTimer::singleShot(0, rhiPrimer, &QObject::deleteLater);
#endif

  std::vector<std::filesystem::path> paths;
  const QStringList arguments = app.arguments();
  paths.reserve(arguments.size() > 1 ? static_cast<size_t>(arguments.size() - 1) : 0);
  for (int index = 1; index < arguments.size(); ++index) {
    paths.push_back(filePath(arguments[index]));
  }
  window.openPaths(paths);

  return app.exec();
}
