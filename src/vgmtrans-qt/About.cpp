#include "About.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>

About::About(QWidget *parent) : QDialog(parent) {
    setWindowTitle("About VGMTrans");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    const QString small =
        QStringLiteral("<p style='margin-top:0; margin-bottom:0; font-size:small;'>");
    const QString medium = QStringLiteral("<p style='margin-top:15px;'>");

    QString text;
    text.append(QStringLiteral("<p style='font-size:38pt; font-weight:400; margin-bottom:0;'>") +
                "VGMTransQt" + QStringLiteral("</p>"));

    text.append(medium + "An open-source videogame music translator</p>");
    text.append(
        medium +
        QStringLiteral("<a href='https://github.com/sykhro/vgmtrans-qt'>Source code</a><br/><a "
                       "href='https://github.com/vgmtrans'>Original organization</a>"));

    QLabel *text_label = new QLabel(text);
    text_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    text_label->setOpenExternalLinks(true);
    text_label->setContentsMargins(30, 0, 30, 0);

    QLabel *copyright =
        new QLabel(small + "\u00A9 2002-2019 VGMTrans Team | Licensed under the zlib license" +
                   QStringLiteral("</p>"));

    QLabel *logo = new QLabel();
    logo->setPixmap(QPixmap(":/images/logo.png").scaledToHeight(300, Qt::SmoothTransformation));
    logo->setContentsMargins(30, 0, 30, 0);

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
