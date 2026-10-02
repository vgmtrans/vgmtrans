/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>

class QColor;
class QFont;
class QPainter;
class QPalette;
class QRect;

struct CapsuleText {
  QString prefix;
  QStringList capsules;

  [[nodiscard]] bool isEmpty() const noexcept { return prefix.isEmpty() && capsules.isEmpty(); }
  [[nodiscard]] QString plainText() const;
};

Q_DECLARE_METATYPE(CapsuleText)

namespace CapsuleTextLayout {

[[nodiscard]] int heightForWidth(const CapsuleText& text, const QFont& font,
                                 int width, bool wrap);
void paint(QPainter& painter, const QRect& rect, const CapsuleText& text,
           const QPalette& palette, const QColor& textColor, bool wrap);

}  // namespace CapsuleTextLayout
