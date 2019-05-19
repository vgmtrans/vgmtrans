/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>

class HeaderContainer : public QWidget {
    Q_OBJECT

   public:
    explicit HeaderContainer(QWidget *content, const QString title, QObject *parent = nullptr);

   private:
    QVBoxLayout *layout;
    QLabel *header;
};
