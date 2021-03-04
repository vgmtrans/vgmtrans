#ifndef VGMTRANS_BREAKDOWNVIEW_H
#define VGMTRANS_BREAKDOWNVIEW_H

#include <QMainWindow>
#include <qsplitter.h>

class VGMFile;
class HexView;
class VGMFileTreeView;

class VGMFileView : public QSplitter {
    Q_OBJECT

public:
    VGMFileView(VGMFile *vgmFile);
    ~VGMFileView();

    void addToMdi();


protected:
    QSplitter *horzSplitter;
    HexView *hexView;
    VGMFileTreeView *treeView;
};


#endif //VGMTRANS_BREAKDOWNVIEW_H
