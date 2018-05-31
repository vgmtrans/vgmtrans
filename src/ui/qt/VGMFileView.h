#pragma once

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
    HexView *hexView;
    VGMFileTreeView *treeView;
};
