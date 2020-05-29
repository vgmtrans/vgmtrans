/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include "MainWindow.h"
#include "QtVGMRoot.h"
#include "phantom/phantomstyle.h"

struct LightTheme {
    QColor window{245, 245, 245};
    QColor text{15, 15, 15};
    QColor disabledText{0x60a4a6a8};
    QColor brightText{Qt::white};
    QColor highlight{0xbfc7d5};
    QColor highlightedText{0x2d2c27};
    QColor base{230, 230, 230};
    QColor alternateBase{230, 230, 230};
    QColor shadow = base;
    QColor button{245, 245, 245};
    QColor disabledButton = button.darker(105);
    QColor icon = text;
    QColor disabledIcon = disabledButton;
};

struct DarkTheme {
    QColor window{60, 63, 65};
    QColor text{185, 185, 185};
    QColor disabledText{0x60a4a6a8};
    QColor brightText{Qt::white};
    QColor highlight{47, 101, 202};
    QColor highlightedText = text;
    QColor base{49, 51, 53};
    QColor alternateBase = base;
    QColor shadow = base;
    QColor button = alternateBase;
    QColor disabledButton = button.lighter(105);
    QColor icon = text;
    QColor disabledIcon = disabledButton;
};

QPalette namedColorSchemePalette(std::variant<LightTheme, DarkTheme> theme) {
    auto themeColorsToPalette = [](auto x) -> QPalette {
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

    return std::visit(themeColorsToPalette, theme);
}

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setOrganizationName(QStringLiteral("VGMCis"));
    QCoreApplication::setApplicationName(QStringLiteral("VGMCisQt"));
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    QApplication app(argc, argv);
    QApplication::setStyle(new PhantomStyle);
    QApplication::setPalette(namedColorSchemePalette(DarkTheme{}));

    qtVGMRoot.Init();
    MainWindow window;
    window.resize(1385, 785);
    window.show();

    return app.exec();
}
