#include "MdiArea.h"
#include <QTabBar>
#include <QAbstractButton>
//#include <QStyleOptionTab>
//#include <QStyle>
#include <QDebug>
#include <QApplication>

MdiArea::MdiArea(QWidget *parent)
        : QMdiArea(parent)
{
    setViewMode(QMdiArea::TabbedView);
    setDocumentMode(true);
    setTabsMovable(true);
    setTabsClosable(true);
    setTabShape(QTabWidget::Triangular);

    QTabBar *tabBar = getTabBar();
    if (tabBar) {
        tabBar->setExpanding(false);
        tabBar->setUsesScrollButtons(true);

//        tabBar->setStyle(new TabBarStyle(tabBar->style()));
    }
}

QMdiSubWindow* MdiArea::addSubWindow(QWidget *widget) {

    QTabBar *tabBar = getTabBar();
    QMdiSubWindow* newWindow = QMdiArea::addSubWindow(widget);
    auto index = tabBar->count()-1;
//    QPushButton *button = new QPushButton("X");
//    button->setFixedSize(32, tabBar->height());
    tabBar->setTabToolTip(index, widget->windowTitle());
//    QAbstractButton *button = getCloseButton();

//    connect(button, SIGNAL (clicked()), this, SLOT (closeButtonClicked()));

//    qDebug() << "button position is: " << button->pos();
//    button->setGeometry(0, 0, 20, 20);
//    tabBar->setTabButton(index, QTabBar::RightSide, button);
    return newWindow;

}

void MdiArea::closeButtonClicked()
{
    qDebug() << "CLOSE BUTTON CLICKED";
}

QTabBar* MdiArea::getTabBar() {
    QList<QTabBar *> tabBarList = findChildren<QTabBar*>();
    QTabBar *tabBar = tabBarList.at(0);
//    QList<QAbstractButton*> buttonList = tabBar->findChildren<QAbstractButton*>() ;
//
//    QStyleOptionTab opt;
//    QRect rect = style()->subElementRect(QStyle::SE_TabBarTabRightButton, &opt, this);
//    qDebug() << "TABBAR RIGHT: " << rect.left() << rect;

//    qDebug() << "buttonList: " << buttonList.count();
//    for (auto *button : buttonList ) {
//        qDebug() << "button position: " << button->pos() << "  typeid: " << typeid(button).name();
//        button->move(0, 0);
//        button->setGeometry(10, 10, 50, 50);
//    }
    return tabBarList.at(0);
}

QAbstractButton* MdiArea::getCloseButton() {
    QTabBar *tabBar = getTabBar();
    QList<QAbstractButton*> buttonList = tabBar->findChildren<QAbstractButton*>();

    return buttonList.at(0);
//    for (int i=0; i<buttonList.count(); i++) {
//        auto button = buttonList[i];
//
//        qDebug() << "button " << i << " position: " << button->pos();
//
//    }
    //    for (auto *button : buttonList ) {
//        qDebug() << "button position: " << button->pos() << "  typeid: " << typeid(button).name();
//        button->move(0, 0);
//        button->setGeometry(10, 10, 50, 50);
//    }
}