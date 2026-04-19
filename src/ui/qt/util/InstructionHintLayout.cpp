/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "util/InstructionHintLayout.h"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include "util/UIHelpers.h"

QFont prepareInstructionFont(const QFont &base, qreal scale,
                             QFont::Weight weight,
                             int minPointSize,
                             const QString &fontFamily) {
  QFont font = base;
  if (font.pointSizeF() > 0.0) {
    const qreal scaledPointSize = font.pointSizeF() * scale;
    font.setPointSizeF(minPointSize > 0
                           ? std::max<qreal>(minPointSize, scaledPointSize)
                           : scaledPointSize);
  } else if (font.pixelSize() > 0) {
    const int scaledPixelSize = static_cast<int>(std::round(font.pixelSize() * scale));
    font.setPixelSize(minPointSize > 0
                          ? std::max(minPointSize, scaledPixelSize)
                          : scaledPixelSize);
  } else {
    const int fallbackSize = scale >= 1.5 ? 18 : 14;
    font.setPointSize(minPointSize > 0
                          ? std::max(minPointSize, fallbackSize)
                          : fallbackSize);
  }

  font.setWeight(weight);
  if (!fontFamily.isEmpty()) {
    font.setFamily(fontFamily);
  }
  return font;
}

InstructionMetrics computeInstructionMetrics(const InstructionHint &hint, const QFont &baseFont) {
  QFont font = prepareInstructionFont(baseFont, hint.fontScale, hint.fontWeight,
                                      hint.minPointSize, hint.fontFamily);
  QFontMetrics metrics(font);
  const int iconSide = static_cast<int>(metrics.height() * hint.iconScale);
  const int spacing = std::max(2, static_cast<int>(metrics.height() * hint.spacingScale));
  const int textWidth = metrics.horizontalAdvance(hint.text);
  const int width = std::max(iconSide, textWidth);
  const int height = iconSide + spacing + metrics.height();
  return {hint, font, metrics, iconSide, spacing, QSize(width, height)};
}

void paintInstruction(QPainter &painter, const InstructionMetrics &metrics, const QPoint &topLeft,
                      const QColor &accent) {
  const QSize iconSize(metrics.iconSide, metrics.iconSide);
  const int iconX = topLeft.x() + (metrics.size.width() - metrics.iconSide) / 2;
  const int iconY = topLeft.y();
  const qreal devicePixelRatio =
      painter.device() ? painter.device()->devicePixelRatioF() : qreal(1.0);
  const QPixmap icon = tintedIconPixmap(QIcon(metrics.hint.iconPath), iconSize, accent,
                                        devicePixelRatio);
  if (!icon.isNull()) {
    painter.drawPixmap(iconX, iconY, icon);
  }

  painter.setFont(metrics.font);
  painter.setPen(accent);

  const int textTop = iconY + metrics.iconSide + metrics.spacing;
  const QRect textRect(topLeft.x(), textTop, metrics.size.width(), metrics.metrics.height());
  painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, metrics.hint.text);
}
