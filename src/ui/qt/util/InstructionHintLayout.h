/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QFont>
#include <QFontMetrics>
#include <QPoint>
#include <QSize>
#include <QString>

class QColor;
class QPainter;

struct InstructionHint {
  QString iconPath;
  QString text;
  qreal fontScale = 1.0;
  qreal iconScale = 1.0;
  qreal spacingScale = 0.3;
  QFont::Weight fontWeight = QFont::Normal;
  int minPointSize = 0;
  QString fontFamily = QStringLiteral("Helvetica Neue");
};

struct InstructionMetrics {
  InstructionHint hint;
  QFont font;
  QFontMetrics metrics;
  int iconSide = 0;
  int spacing = 0;
  QSize size;
};

QFont prepareInstructionFont(const QFont &base, qreal scale,
                             QFont::Weight weight = QFont::Normal,
                             int minPointSize = 0,
                             const QString &fontFamily = {});

InstructionMetrics computeInstructionMetrics(const InstructionHint &hint, const QFont &baseFont);

void paintInstruction(QPainter &painter, const InstructionMetrics &metrics, const QPoint &topLeft,
                      const QColor &accent);
