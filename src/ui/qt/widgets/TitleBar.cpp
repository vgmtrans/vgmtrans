/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "TitleBar.h"
#include <QHBoxLayout>
#include <QLabel>

TitleBar::TitleBar(const QString& title, QWidget *parent) : QWidget(parent) {
  QHBoxLayout *titleLayout = new QHBoxLayout(this);
  titleLayout->setContentsMargins(10, 5, 10, 5);
  QLabel *titleLabel = new QLabel(title);
  titleLayout->addWidget(titleLabel);

  QFont labelFont("Arial", -1, QFont::Bold, true);
  titleLabel->setFont(labelFont);
}