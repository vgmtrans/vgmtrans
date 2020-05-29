/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QFontDatabase>
#include "HeaderContainer.h"

HeaderContainer::HeaderContainer(QWidget *content, const QString title, QObject *parent)
    : QWidget() {
    QFont font = QFontDatabase::systemFont(QFontDatabase::TitleFont);
    font.setPixelSize(12);
    font.setBold(true);

    header = new QLabel(title);
    header->setContentsMargins(0, 5, 0, 5);
    header->setFont(font);

    layout = new QVBoxLayout();
    layout->addWidget(header);
    layout->addWidget(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setMargin(0);
    layout->setSpacing(0);

    setLayout(layout);
}
