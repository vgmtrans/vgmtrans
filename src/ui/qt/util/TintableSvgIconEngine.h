#pragma once

#include <cmath>
#include <QIconEngine>
#include <QLinearGradient>
#include <QPainter>
#include <QSvgRenderer>
#include <QHash>
#include <qguiapplication.h>
#include <QtMath>

class TintableSvgIconEngine : public QIconEngine
{
public:
  TintableSvgIconEngine(QString svgPath, QColor tint)
      : m_path(std::move(svgPath)), m_startColor(std::move(tint)) {
  }

  TintableSvgIconEngine(QString svgPath, QColor startColor, QColor endColor, int angleDegrees = 90)
      : m_path(std::move(svgPath)),
        m_startColor(std::move(startColor)),
        m_endColor(std::move(endColor)),
        m_angleDegrees(angleDegrees),
        m_useGradient(true) {
  }

  QIconEngine *clone() const override {
    return new TintableSvgIconEngine(*this);
  }

  QPixmap pixmap(const QSize &logical, QIcon::Mode, QIcon::State) override {
    return renderCached(logical, qGuiApp->devicePixelRatio());
  }

  void paint(QPainter *p, const QRect &rect,
             QIcon::Mode mode, QIcon::State state) override {
    p->drawPixmap(rect, pixmap(rect.size(), mode, state));
  }

private:
  QLinearGradient gradientForRect(const QRectF &rect) const {
    const qreal radians = qDegreesToRadians(qreal(m_angleDegrees));
    const QPointF direction(std::cos(radians), std::sin(radians));
    const QPointF center = rect.center();
    const qreal halfLength = std::hypot(rect.width(), rect.height()) * 0.5;

    QLinearGradient gradient(center - direction * halfLength, center + direction * halfLength);
    gradient.setSpread(QGradient::PadSpread);
    gradient.setColorAt(0.0, m_startColor);
    gradient.setColorAt(1.0, m_endColor);
    return gradient;
  }

  QPixmap renderCached(const QSize &logicalSize, qreal dpr) const {
    const QSize phys = logicalSize * dpr;
    const quint64 key = (quint64(phys.width()) << 32) | phys.height();

    if (auto it = m_cache.find(key); it != m_cache.end())
      return it.value();

    QImage img(phys, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QSvgRenderer renderer(m_path);
    QPainter p(&img);
    renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(phys)));

    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), m_useGradient ? QBrush(gradientForRect(QRectF(QPointF(0, 0), QSizeF(phys))))
                                         : QBrush(m_startColor));
    p.end();

    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    m_cache.insert(key, pm);
    return pm;
  }


  QString                m_path;
  QColor                 m_startColor;
  QColor                 m_endColor;
  int                    m_angleDegrees = 90;
  bool                   m_useGradient = false;
  mutable QHash<quint64, QPixmap> m_cache;
};
