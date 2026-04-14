/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QProxyStyle>
#include <QScrollBar>
#include <QStyleFactory>

namespace QtUi {

#ifdef Q_OS_MAC

class NonTransientScrollBarStyle final : public QProxyStyle {
public:
  using QProxyStyle::QProxyStyle;

  int styleHint(StyleHint hint,
                const QStyleOption* option = nullptr,
                const QWidget* widget = nullptr,
                QStyleHintReturn* returnData = nullptr) const override {
    if (hint == QStyle::SH_ScrollBar_Transient) {
      return 0;
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
  }
};

inline void applyNonTransientScrollBarStyle(QScrollBar* scrollBar) {
  if (!scrollBar) {
    return;
  }

  QStyle* baseStyle = QStyleFactory::create(QStringLiteral("macos"));
  if (!baseStyle) {
    baseStyle = QStyleFactory::create(QStringLiteral("macintosh"));
  }
  if (!baseStyle) {
    baseStyle = QStyleFactory::create(QStringLiteral("Fusion"));
  }

  auto* style = baseStyle ? new NonTransientScrollBarStyle(baseStyle)
                          : new NonTransientScrollBarStyle();
  scrollBar->setStyle(style);
  if (!style->parent()) {
    style->setParent(scrollBar);
  }
}

#else

inline void applyNonTransientScrollBarStyle(QScrollBar*) {
}

#endif

}  // namespace QtUi
