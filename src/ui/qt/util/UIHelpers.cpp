/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "UIHelpers.h"
#include <QWidget>
#include <QScrollArea>

QScrollArea* getContainingScrollArea(QWidget* widget) {
  QWidget* viewport = widget->parentWidget();
  if (viewport) {
    QScrollArea* scrollArea = qobject_cast<QScrollArea*>(viewport->parentWidget());
    if (scrollArea && scrollArea->viewport() == viewport) {
      return scrollArea;
    }
  }
  return nullptr;
}