#ifndef VGMTRANS_VGMFILETREEVIEW_H
#define VGMTRANS_VGMFILETREEVIEW_H

#include <qtreeview.h>
#include "VGMFileItemModel.h"

class VGMFile;

class VGMFileTreeView : public QTreeView {
    Q_OBJECT

private:
    VGMFile *vgmfile;
    VGMFileItemModel model;

public:
    VGMFileTreeView(VGMFile *vgmfile, QWidget *parent = 0);
    ~VGMFileTreeView();
};


#endif //VGMTRANS_VGMFILETREEVIEW_H
