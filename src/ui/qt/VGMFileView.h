#ifndef VGMTRANS_BREAKDOWNVIEW_H
#define VGMTRANS_BREAKDOWNVIEW_H

#include <QMainWindow>
#include <qsplitter.h>
#include <qtreeview.h>
#include "HexView.h"

class VGMFile;

class VGMFileView : public QSplitter {
    Q_OBJECT

public:
    VGMFileView(VGMFile *vgmFile);
    ~VGMFileView();

    void addToMdi();


protected:
    QSplitter *horzSplitter;
    HexView *hexView;
    QTreeView *treeView;
};


#endif //VGMTRANS_BREAKDOWNVIEW_H
