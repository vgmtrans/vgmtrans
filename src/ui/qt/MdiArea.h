/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#ifndef VGMTRANS_MDIAREA_H
#define VGMTRANS_MDIAREA_H

#include <QMdiArea>

class QAbstractButton;

class MdiArea : public QMdiArea {
    Q_OBJECT

public:
    explicit MdiArea(QWidget *parent = nullptr);
    ~MdiArea() = default; // temporary

   
protected:
    QTabBar *getTabBar();
    QAbstractButton *getCloseButton();

};

#endif //VGMTRANS_MDIAREA_H
