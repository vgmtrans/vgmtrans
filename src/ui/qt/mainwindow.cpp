#include <qtextedit.h>
#include <QDragEnterEvent>
#include <QMimeData>
#include "mainwindow.h"
#include "QtVGMRoot.h"
#include "RawFileListView.h"
#include "VGMFileListView.h"
#include "VGMCollListView.h"


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setAcceptDrops(true);

    QTextEdit *editor1 = new QTextEdit;
    QTextEdit *editor2 = new QTextEdit;

    RawFileListViewModel *rawFileListViewModel = new RawFileListViewModel(this);
    rawFileListView = new QListView();
    rawFileListView->setModel(rawFileListViewModel);
    rawFileListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    rawFileListView->setSelectionRectVisible(true);

    VGMFileListViewModel *vgmFileListViewModel = new VGMFileListViewModel(this);
    vgmFileListView = new QListView();
    vgmFileListView->setModel(vgmFileListViewModel);
    vgmFileListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    vgmFileListView->setSelectionRectVisible(true);

    VGMCollListViewModel *vgmCollListViewModel = new VGMCollListViewModel(this);
    vgmCollListView = new QListView();
    vgmCollListView->setModel(vgmCollListViewModel);
    vgmCollListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    vgmCollListView->setSelectionRectVisible(true);

    vertSplitter = new QSplitter(Qt::Vertical, this);
    horzSplitter = new QSplitter(Qt::Horizontal, vertSplitter);
    vertSplitterLeft = new QSplitter(Qt::Vertical, horzSplitter);

    vertSplitter->addWidget(horzSplitter);
    vertSplitter->addWidget(vgmCollListView);

    horzSplitter->addWidget(vertSplitterLeft);
    horzSplitter->addWidget(editor1);
    horzSplitter->setStretchFactor(1, 1);

    vertSplitterLeft->addWidget(rawFileListView);
    vertSplitterLeft->addWidget(vgmFileListView);
    vertSplitterLeft->setStretchFactor(1, 1);

//    horzSplitter->show();
    setCentralWidget(vertSplitter);


}

MainWindow::~MainWindow()
{
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    event->acceptProposedAction();
}

wchar_t* qstringTowchar_t(QString text){
//    qDebug()<<text.length();
    wchar_t* c_Text = new wchar_t[text.length() + 1];
    text.toWCharArray(c_Text);

    c_Text[text.length()] = 0; //Add this line should work as you expected
    return c_Text;
}

void MainWindow::dropEvent(QDropEvent *event)
{

    const QMimeData *mimeData = event->mimeData();

//    if (mimeData->hasImage()) {
//        setPixmap(qvariant_cast<QPixmap>(mimeData->imageData()));
//    } else if (mimeData->hasHtml()) {
//        setText(mimeData->html());
//        setTextFormat(Qt::RichText);
//    } else
    if (mimeData->hasText()) {
        std::string utf8_text = mimeData->text().toUtf8().constData();
        printf(utf8_text.c_str());
//        setText(mimeData->text());
//        setTextFormat(Qt::PlainText);
    }
    if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        int urlSize = urlList.size();
        printf("%d", urlSize);
        QString text;
        for (int i = 0; i < urlList.size() && i < 32; ++i) {
            QString url = urlList.at(i).path();
            wchar_t *str = qstringTowchar_t(url);
//            qDebug() << text.length();
            qtVGMRoot.OpenRawFile(str);
//            text += url + QString("\n");
        }
        printf(text.toUtf8().constData());
//        qtVGMRoot.OpenRawFile(urlList)
//        setText(text);
    } else {
//        setText(tr("Cannot display data"));
    }
//! [dropEvent() function part2]

//! [dropEvent() function part3]
    setBackgroundRole(QPalette::Dark);
    event->acceptProposedAction();

}
