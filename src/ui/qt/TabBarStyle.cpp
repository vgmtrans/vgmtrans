//
// Created by Mike on 9/1/15.
//

#include "TabBarStyle.h"
#include <QStyleOption>

//TabBarStyle::TabBarStyle(QStyle *style) : QProxyStyle(style)
//{
//}

//void TabBarStyle::drawControl(QStyle::ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
//{
//    if (element == CE_TabBarTabLabel)
//    {
//        const QStyleOptionTab *tabOption = qstyleoption_cast<const QStyleOptionTab*>(option);
//
//        if (tabOption)
//        {
//            QStyleOptionTab mutableTabOption(*tabOption);
//            mutableTabOption.shape = QTabBar::RoundedNorth;
//
//            QProxyStyle::drawControl(element, &mutableTabOption, painter, widget);
//
//            return;
//        }
//    }
//
//    QProxyStyle::drawControl(element, option, painter, widget);
//}

//QSize TabBarStyle::sizeFromContents(QStyle::ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const
//{
//    QSize mutableSize = QProxyStyle::sizeFromContents(type, option, size, widget);
//    const QStyleOptionTab *tabOption = qstyleoption_cast<const QStyleOptionTab*>(option);
//
//    if (type == QStyle::CT_TabBarTab && tabOption && (tabOption->shape == QTabBar::RoundedEast || tabOption->shape == QTabBar::RoundedWest))
//    {
//        mutableSize.transpose();
//    }
//
//    return mutableSize;
//}
//
//QRect TabBarStyle::subElementRect(QStyle::SubElement element, const QStyleOption *option, const QWidget *widget) const
//{
//    if (element == QStyle::SE_TabBarTabLeftButton || element == QStyle::SE_TabBarTabRightButton || element == QStyle::SE_TabBarTabText)
//    {
//        const QStyleOptionTab *tabOption = qstyleoption_cast<const QStyleOptionTab*>(option);
//
//        if (tabOption->shape == QTabBar::RoundedEast || tabOption->shape == QTabBar::RoundedWest)
//        {
//            QStyleOptionTab mutableTabOption(*tabOption);
//            mutableTabOption.shape = QTabBar::RoundedNorth;
//
//            QRect rectangle = QProxyStyle::subElementRect(element, &mutableTabOption, widget);
//            rectangle.translate(0, option->rect.top());
//
//            return rectangle;
//        }
//    }
//
//    return QProxyStyle::subElementRect(element, option, widget);
//}