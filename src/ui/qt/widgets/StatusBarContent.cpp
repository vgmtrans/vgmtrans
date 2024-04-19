/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/


#include "StatusBarContent.h"
#include <QHBoxLayout>
#include <QIcon>
#include "services/StatusManager.h"

constexpr int maxHeight = 25; // Maximum height of the status bar
constexpr int iconLabelWidth = 16;
constexpr int nameLabelMinWidth = 140;
constexpr int descriptionLabelMinWidth = 80;
constexpr int descriptionLabelIndent = 20;
constexpr int offsetLabelWidth = 130;
constexpr int sizeLabelWidth = 130;

StatusBarContent::StatusBarContent(QWidget *parent) : QWidget(parent)
{
  iconLabel = new QLabel;
  nameLabel = new QLabel;
  descriptionLabel = new QLabel;
  offsetLabel = new QLabel;
  sizeLabel = new QLabel;

  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(10, 0, 10, 0);

  QFont labelFont = nameLabel->font();
  QPalette palette = this->palette();
  QColor subduedTextColor = palette.color(QPalette::WindowText).darker(150);

  for (QLabel* label : {iconLabel, nameLabel, descriptionLabel, offsetLabel, sizeLabel}) {
    layout->addWidget(label, (label == descriptionLabel) ? 1 : 0);
    label->setFont(labelFont);
    label->setStyleSheet(QString("color: %1").arg(subduedTextColor.name()));
    label->setMaximumHeight(maxHeight);
  }
  iconLabel->setFixedWidth(iconLabelWidth);
  offsetLabel->setFixedWidth(offsetLabelWidth);
  sizeLabel->setFixedWidth(sizeLabelWidth);

  layout->setSizeConstraint(QLayout::SetNoConstraint);

  nameLabel->setMinimumWidth(nameLabelMinWidth);
  descriptionLabel->setMinimumWidth(descriptionLabelMinWidth);
  descriptionLabel->setIndent(descriptionLabelIndent);

  this->setLayout(layout);
  this->setMaximumHeight(maxHeight);

  connect(StatusManager::the(), &StatusManager::updateStatusSignal, this, &StatusBarContent::setStatus);
}

void StatusBarContent::setStatus(const QString& name, const QString& description, const QIcon* icon, int offset, int size) {
  nameLabel->setText(name);
  descriptionLabel->setText(description);
  if (icon)
    iconLabel->setPixmap(icon->pixmap(16, 16));
  else
    iconLabel->clear();

  if (offset >= 0)
    offsetLabel->setText(QString{"Offset: 0x%1"}.arg(offset));
  else
    offsetLabel->clear();

  if (size >= 0)
    sizeLabel->setText(QString{"Size: 0x%1"}.arg(size));
  else
    sizeLabel->clear();
}
