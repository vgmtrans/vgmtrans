/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include "MainWindow.h"
#include "QtVGMRoot.h"
#include "phantom/phantomstyle.h"

QPalette namedColorSchemePalette() {
    struct ThemeColors {
        QColor window;
        QColor text;
        QColor disabledText;
        QColor brightText;
        QColor highlight;
        QColor highlightedText;
        QColor base;
        QColor alternateBase;
        QColor shadow;
        QColor button;
        QColor disabledButton;
        QColor unreadBadge;
        QColor unreadBadgeText;
        QColor icon;
        QColor disabledIcon;
        QColor chatTimestampText;
    };

    auto themeColorsToPalette = [](const ThemeColors &x) -> QPalette {
        QPalette pal;
        pal.setColor(QPalette::Window, x.window);
        pal.setColor(QPalette::WindowText, x.text);
        pal.setColor(QPalette::Text, x.text);
        pal.setColor(QPalette::ButtonText, x.text);
        if (x.brightText.isValid())
            pal.setColor(QPalette::BrightText, x.brightText);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, x.disabledText);
        pal.setColor(QPalette::Disabled, QPalette::Text, x.disabledText);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, x.disabledText);
        pal.setColor(QPalette::Base, x.base);
        pal.setColor(QPalette::AlternateBase, x.alternateBase);
        if (x.shadow.isValid())
            pal.setColor(QPalette::Shadow, x.shadow);
        pal.setColor(QPalette::Button, x.button);
        pal.setColor(QPalette::Highlight, x.highlight);
        pal.setColor(QPalette::HighlightedText, x.highlightedText);
        if (x.disabledButton.isValid())
            pal.setColor(QPalette::Disabled, QPalette::Button, x.disabledButton);
        // Used as the shadow text color on disabled menu items
        pal.setColor(QPalette::Disabled, QPalette::Light, Qt::transparent);
        return pal;
    };

    ThemeColors c;
    QColor window(60, 61, 64);
    QColor button(74, 75, 80);
    QColor base(46, 47, 49);
    QColor alternateBase(41, 41, 43);
    QColor text(208, 209, 212);
    QColor highlight(0xbfc7d5);
    QColor highlightedText(0x2d2c27);
    QColor disabledText(0x60a4a6a8);
    c.window = window;
    c.text = text;
    c.disabledText = disabledText;
    c.base = base;
    c.alternateBase = alternateBase;
    c.shadow = base;
    c.button = button;
    c.disabledButton = button.darker(107);
    c.brightText = Qt::white;
    c.highlight = highlight;
    c.highlightedText = highlightedText;
    c.icon = text;
    c.disabledIcon = c.disabledText;
    c.unreadBadge = c.text;
    c.unreadBadgeText = c.highlightedText;
    c.chatTimestampText = c.base.lighter(160);

    return themeColorsToPalette(c);
}

int main(int argc, char *argv[]) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QCoreApplication::setOrganizationName(QStringLiteral("VGMTrans"));
    QCoreApplication::setApplicationName(QStringLiteral("VGMTransQt"));
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    QApplication app(argc, argv);
    QApplication::setStyle(new PhantomStyle);
    QApplication::setPalette(namedColorSchemePalette());

    qtVGMRoot.Init();
    MainWindow window;
    window.resize(1385, 785);
    window.show();

    return app.exec();
}
