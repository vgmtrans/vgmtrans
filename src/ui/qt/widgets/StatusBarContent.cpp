/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/


#include "StatusBarContent.h"

#include "CapsuleText.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>

#include <utility>

class CapsuleTextLabel final : public QWidget {
public:
  using QWidget::QWidget;

  void setText(QString text) {
    content_ = CapsuleText{.prefix = std::move(text)};
    update();
  }

  void setText(CapsuleText text) {
    content_ = std::move(text);
    update();
  }

  void setIndent(int indent) {
    indent_ = indent;
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    CapsuleTextLayout::paint(painter, contentsRect().adjusted(indent_, 0, 0, 0), content_, palette(),
                             palette().color(QPalette::WindowText), false);
  }

private:
  CapsuleText content_;
  int indent_ = 0;
};

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
  descriptionLabel = new CapsuleTextLabel;
  offsetLabel = new QLabel;
  sizeLabel = new QLabel;

  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(10, 0, 10, 0);

  QFont labelFont = nameLabel->font();
  QPalette palette = this->palette();
  QColor subduedTextColor = palette.color(QPalette::WindowText).darker(150);

  for (QLabel* label : {iconLabel, nameLabel, offsetLabel, sizeLabel}) {
    label->setFont(labelFont);
    label->setStyleSheet(QString("color: %1").arg(subduedTextColor.name()));
    label->setMaximumHeight(maxHeight);
  }
  layout->addWidget(iconLabel);
  layout->addWidget(nameLabel);
  layout->addWidget(descriptionLabel, 1);
  layout->addWidget(offsetLabel);
  layout->addWidget(sizeLabel);
  descriptionLabel->setFont(labelFont);
  QPalette descriptionPalette = descriptionLabel->palette();
  descriptionPalette.setColor(QPalette::WindowText, subduedTextColor);
  descriptionLabel->setPalette(descriptionPalette);
  descriptionLabel->setMaximumHeight(maxHeight);
  iconLabel->setFixedWidth(iconLabelWidth);
  offsetLabel->setFixedWidth(offsetLabelWidth);
  sizeLabel->setFixedWidth(sizeLabelWidth);

  layout->setSizeConstraint(QLayout::SetNoConstraint);

  nameLabel->setMinimumWidth(nameLabelMinWidth);
  descriptionLabel->setMinimumWidth(descriptionLabelMinWidth);
  descriptionLabel->setIndent(descriptionLabelIndent);

  this->setLayout(layout);
  this->setMaximumHeight(maxHeight);

}

void StatusBarContent::setStatus(const QString& name, const QString& description, const QIcon* icon, int offset, int size) const {
  descriptionLabel->setText(description);
  setCommonStatus(name, icon, offset, size);
}

void StatusBarContent::setInspectorStatus(const QString& name, const CapsuleText& description,
                                          const QIcon* icon, int offset, int size) const {
  descriptionLabel->setText(description);
  setCommonStatus(name, icon, offset, size);
}

void StatusBarContent::setCommonStatus(const QString& name, const QIcon* icon, int offset,
                                       int size) const {
  nameLabel->setText(name);
  if (icon)
    iconLabel->setPixmap(icon->pixmap(16, 16));
  else
    iconLabel->clear();

  if (offset >= 0)
    offsetLabel->setText(QString{"Offset: 0x%1"}.arg(offset, 0, 16));
  else
    offsetLabel->clear();

  if (size >= 0)
    sizeLabel->setText(QString{"Size: 0x%1"}.arg(size, 0, 16));
  else
    sizeLabel->clear();
}
