/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "UIHelpers.h"
#include <QWidget>
#include <QScrollArea>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPainter>

QScrollArea* getContainingScrollArea(QWidget* widget) {
  QWidget* viewport = widget->parentWidget();
  if (viewport) {
    auto* scrollArea = qobject_cast<QScrollArea*>(viewport->parentWidget());
    if (scrollArea && scrollArea->viewport() == viewport) {
      return scrollArea;
    }
  }
  return nullptr;
}

void applyEffectToPixmap(QPixmap &src, QPixmap &tgt, QGraphicsEffect *effect, int extent)
{
  if(src.isNull()) return;  // Check if src is valid
  if(!effect) return;       // Check if effect is valid

  QGraphicsScene scene;
  QGraphicsPixmapItem item;
  item.setPixmap(src);
  item.setGraphicsEffect(effect);
  scene.addItem(&item);

  tgt.fill(Qt::transparent);
  QPainter ptr(&tgt);
  scene.render(&ptr, QRectF(), QRectF(-extent, -extent, src.width() + extent*2, src.height() + extent*2));
}
