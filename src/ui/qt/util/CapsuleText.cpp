/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "CapsuleText.h"

#include "ColorHelpers.h"

#include <algorithm>

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>

namespace {

constexpr int horizontalPadding = 7;
constexpr int totalVerticalPadding = 3;
constexpr int itemSpacing = 4;
constexpr int lineSpacing = 2;

QFont capsuleFont(QFont font) {
  if (font.pointSizeF() > 2.0) {
    font.setPointSizeF(font.pointSizeF() - 2.0);
  } else if (font.pixelSize() > 2) {
    font.setPixelSize(font.pixelSize() - 2);
  }
  return font;
}

template <typename Visitor>
int layout(const CapsuleText& text, const QFontMetrics& plainMetrics,
           const QFontMetrics& capsuleMetrics, int width, bool wrap, Visitor&& visitor) {
  if (text.isEmpty()) {
    return 0;
  }

  const int capsuleHeight = capsuleMetrics.height() + totalVerticalPadding;
  const int lineHeight =
      std::max(text.prefix.isEmpty() ? 0 : plainMetrics.height(),
               text.capsules.isEmpty() ? 0 : capsuleHeight);
  int x = 0;
  int y = 0;

  auto place = [&](const QString& value, bool capsule) {
    const QFontMetrics& metrics = capsule ? capsuleMetrics : plainMetrics;
    const int itemWidth =
        metrics.horizontalAdvance(value) + (capsule ? horizontalPadding * 2 : 0);
    int left = x == 0 ? 0 : x + itemSpacing;
    if (wrap && x != 0 && left + itemWidth > width) {
      left = 0;
      x = 0;
      y += lineHeight + lineSpacing;
    }
    const int itemHeight = capsule ? capsuleHeight : plainMetrics.height();
    visitor(value, QRect(left, y + ((lineHeight - itemHeight) / 2), itemWidth, itemHeight),
            capsule);
    x = left + itemWidth;
  };

  if (!text.prefix.isEmpty()) {
    place(text.prefix, false);
  }
  for (const QString& capsule : text.capsules) {
    place(capsule, true);
  }
  return y + lineHeight;
}

}  // namespace

QString CapsuleText::plainText() const {
  if (prefix.isEmpty()) {
    return capsules.join(QStringLiteral(", "));
  }
  if (capsules.isEmpty()) {
    return prefix;
  }
  return QStringLiteral("%1, %2").arg(prefix, capsules.join(QStringLiteral(", ")));
}

namespace CapsuleTextLayout {

int heightForWidth(const CapsuleText& text, const QFont& font, int width, bool wrap) {
  return layout(text, QFontMetrics(font), QFontMetrics(capsuleFont(font)),
                std::max(1, width), wrap,
                [](const QString&, const QRect&, bool) {});
}

void paint(QPainter& painter, const QRect& rect, const CapsuleText& text,
           const QPalette& palette, const QColor& textColor, bool wrap) {
  if (text.isEmpty() || rect.isEmpty()) {
    return;
  }

  const QFont plainFont = painter.font();
  const QFont fieldFont = capsuleFont(plainFont);
  const QFontMetrics plainMetrics(plainFont);
  const QFontMetrics fieldMetrics(fieldFont);
  const int contentHeight = heightForWidth(text, plainFont, rect.width(), wrap);
  const int top = rect.top() + std::max(0, (rect.height() - contentHeight) / 2);
  const QColor base = palette.color(QPalette::Base);
  const QColor blue(63, 120, 181);
  const QColor background = blendColors(blue, base, isDarkPalette(palette) ? 0.38 : 0.30);
  const QColor capsuleText = contrastingTextColor(background, base, palette);

  painter.save();
  painter.setClipRect(rect);
  painter.translate(rect.left(), top);
  painter.setRenderHint(QPainter::Antialiasing);
  layout(text, plainMetrics, fieldMetrics, rect.width(), wrap,
         [&](const QString& value, const QRect& itemRect, bool capsule) {
           if (capsule) {
             painter.setPen(Qt::NoPen);
             painter.setBrush(background);
             painter.drawRoundedRect(itemRect, itemRect.height() / 2.0,
                                     itemRect.height() / 2.0);
             painter.setFont(fieldFont);
             painter.setPen(capsuleText);
             painter.drawText(itemRect.adjusted(horizontalPadding, 0, -horizontalPadding, 0),
                              Qt::AlignLeft | Qt::AlignVCenter, value);
           } else {
             painter.setFont(plainFont);
             painter.setPen(textColor);
             painter.drawText(itemRect, Qt::AlignLeft | Qt::AlignVCenter, value);
           }
         });
  painter.restore();
}

}  // namespace CapsuleTextLayout
