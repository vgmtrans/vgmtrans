/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "About.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <version.h>

About::About(QWidget *parent) : QDialog(parent) {
    setWindowTitle("About VGMTrans");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto text = R"(
        <p style='font-size:36pt; font-weight:300; margin-bottom:0;'>VGMTrans</p>
        <p style='margin-top:0;'>Version: <b>%1 (%2, %3)</b></p>
        <hr>
        <p>An open-source videogame music translator</p><br>
        <a href='https://github.com/vgmtrans/vgmtrans'>Source code</a>
    )";

    QLabel *text_label = new QLabel(QString(text).arg(VGMTRANS_VERSION).arg(VGMTRANS_REVISION).arg(VGMTRANS_BRANCH));
    text_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    text_label->setOpenExternalLinks(true);
    text_label->setContentsMargins(15, 0, 15, 0);

    QLabel *copyright =
        new QLabel("<p style='margin-top:0; margin-bottom:0; font-size:small;'>&copy; 2002-2021 VGMTrans Team | Licensed under the zlib license<br>SoundFont&reg; is a registered trademark of Creative Technology Ltd.</p>");

    QLabel *logo = new QLabel();
    logo->setPixmap(QPixmap(":/vgmtrans.png").scaledToHeight(250, Qt::SmoothTransformation));
    logo->setContentsMargins(15, 0, 15, 0);

    QVBoxLayout *main_layout = new QVBoxLayout;
    QHBoxLayout *h_layout = new QHBoxLayout;

    setLayout(main_layout);
    main_layout->addLayout(h_layout);
    main_layout->addWidget(copyright);
    copyright->setAlignment(Qt::AlignCenter);
    copyright->setContentsMargins(0, 15, 0, 0);

    h_layout->setAlignment(Qt::AlignLeft);
    h_layout->addWidget(logo);
    h_layout->addWidget(text_label);
}