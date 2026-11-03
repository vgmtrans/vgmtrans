/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "StitchPlanModel.h"

#include "models/ValueModels.h"
#include "util/Colors.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QPainter>
#include <QPixmap>
#include <QScreen>

namespace vgmtrans::ui {
namespace {

QIcon stitchPositionIcon(int oneBasedPosition) {
  constexpr int iconSide = 16;
  const QPalette palette = qApp ? qApp->palette() : QPalette();
  QColor bubbleColor = palette.color(QPalette::Highlight);
  QColor textColor = palette.color(QPalette::HighlightedText);
  if (!bubbleColor.isValid()) {
    bubbleColor = UIColors::FallbackHighlightColor;
  }
  if (!textColor.isValid()) {
    textColor = Qt::white;
  }

  qreal maxDevicePixelRatio = 1.0;
  for (const QScreen* screen : QGuiApplication::screens()) {
    if (screen != nullptr) {
      maxDevicePixelRatio = std::max(maxDevicePixelRatio, screen->devicePixelRatio());
    }
  }
  const int maxScale = std::clamp(static_cast<int>(std::ceil(maxDevicePixelRatio)), 1, 3);

  QIcon icon;
  for (int scale = 1; scale <= maxScale; ++scale) {
    QPixmap pixmap(iconSide * scale, iconSide * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bubbleColor);
    painter.drawEllipse(QRectF(0.5, 0.5, iconSide - 1.0, iconSide - 1.0));

    QFont font = qApp ? qApp->font() : QFont();
    font.setBold(true);
    font.setPixelSize(oneBasedPosition < 10 ? 10 : 8);
    painter.setFont(font);
    painter.setPen(textColor);
    painter.drawText(QRect(0, 0, iconSide, iconSide), Qt::AlignCenter, QString::number(oneBasedPosition));
    painter.end();

    icon.addPixmap(pixmap);
  }
  return icon;
}

}  // namespace

StitchPlanModel::StitchPlanModel(QObject* parent) : QIdentityProxyModel(parent) {
}

void StitchPlanModel::setCollections(std::span<const core::CollectionId> collections) {
  const std::vector updated(collections.begin(), collections.end());
  if (collections_ == updated) {
    return;
  }
  collections_ = updated;
  refreshDecorations();
}

void StitchPlanModel::setPlayingCollection(std::optional<core::CollectionId> collection) {
  if (playingCollection_ == collection) {
    return;
  }
  playingCollection_ = collection;
  refreshDecorations();
}

QVariant StitchPlanModel::data(const QModelIndex& index, int role) const {
  if (role != Qt::DecorationRole || !index.isValid()) {
    return QIdentityProxyModel::data(index, role);
  }

  const core::CollectionId collection{QIdentityProxyModel::data(index, IdRole).toUInt()};
  if (playingCollection_ == collection) {
    return QIdentityProxyModel::data(index, role);
  }
  if (const auto found = std::find(collections_.begin(), collections_.end(), collection); found != collections_.end()) {
    return stitchPositionIcon(static_cast<int>(std::distance(collections_.begin(), found)) + 1);
  }
  return QIdentityProxyModel::data(index, role);
}

Qt::ItemFlags StitchPlanModel::flags(const QModelIndex& index) const {
  const Qt::ItemFlags inherited = QIdentityProxyModel::flags(index);
  return index.isValid() ? inherited | Qt::ItemIsDragEnabled : inherited;
}

void StitchPlanModel::refreshDecorations() {
  if (rowCount() != 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {Qt::DecorationRole});
  }
}

}  // namespace vgmtrans::ui
