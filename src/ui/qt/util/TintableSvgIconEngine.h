#pragma once

#include <QGraphicsPixmapItem>
#include <QIconEngine>
#include <QPainter>
#include <QSvgRenderer>
#include <QHash>
#include <qguiapplication.h>

class TintableSvgIconEngine : public QIconEngine
{
public:
  TintableSvgIconEngine(QString svgPath, QColor tint)
      : m_path(std::move(svgPath)), m_tint(std::move(tint)) {
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
    p.fillRect(img.rect(), m_tint);                             // apply tint
    p.end();

    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    m_cache.insert(key, pm);
    return pm;
  }


  QString                m_path;
  QColor                 m_tint;
  mutable QHash<quint64, QPixmap> m_cache;
};