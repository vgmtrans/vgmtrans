#ifndef VGMTRANS_MDIAREA_H
#define VGMTRANS_MDIAREA_H

#include <QMdiArea>

class QAbstractButton;

class MdiArea : public QMdiArea {
    Q_OBJECT

public:
    static MdiArea *getInstance() {
        static MdiArea *instance = new MdiArea;
        return instance;
    }

    MdiArea(QWidget *parent = 0);

    QMdiSubWindow *addSubWindow(QWidget *widget);

protected:
    QTabBar *getTabBar();
    QAbstractButton *getCloseButton();

public slots:
    void closeButtonClicked();

};

#endif //VGMTRANS_MDIAREA_H
