/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "About.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <version.h>
#include <QListWidget>
#include <QTextEdit>
#include <QDir>

About::About(QWidget *parent) : QDialog(parent) {
  setWindowTitle("About VGMTrans");
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint & Qt::Sheet);
  setWindowModality(Qt::WindowModal);

  tabs = new QTabWidget(this);
  tabs->setTabPosition(QTabWidget::TabPosition::North);

  QWidget* infoTab = new QWidget();
  QWidget* licensesTab = new QWidget();

  setupInfoTab(infoTab);
  setupLicensesTab(licensesTab);

  tabs->addTab(infoTab, "Info");
  tabs->addTab(licensesTab, "Licenses");

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->addWidget(tabs);
  setLayout(layout);

  auto close_btn = new QPushButton("Close");
  connect(close_btn, &QPushButton::pressed, this, &QDialog::accept);
  layout->addWidget(close_btn);
}

void About::setupInfoTab(QWidget* tab) {
  auto text = R"(
        <p style='font-size:36pt; font-weight:300; margin-bottom:0;'>VGMTrans</p>
        <p style='margin-top:0;'>Version: <b>%1 (%2, %3)</b></p>
        <hr>
        <p>An open-source videogame music translator</p><br>
        <a href='https://github.com/vgmtrans/vgmtrans'>Source code</a>
    )";

  QLabel *text_label =
      new QLabel(QString(text).arg(VGMTRANS_VERSION, VGMTRANS_REVISION, VGMTRANS_BRANCH));
  text_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
  text_label->setOpenExternalLinks(true);
  text_label->setContentsMargins(15, 0, 15, 0);

  QLabel *copyright = new QLabel(
      "<p style='margin-top:0; margin-bottom:0; font-size:small;'>&copy; 2002-2025 "
      "VGMTrans Team. Licensed under the zlib license<br/>SoundFont&reg; is a "
      "registered trademark of Creative Technology Ltd.<br/>Commercial use of this software requires "
      "a license for the <a href='https://www.un4seen.com/'>BASS library</a>.</p>");

  QLabel *logo = new QLabel();
  logo->setPixmap(QPixmap(":/vgmtrans.png").scaledToHeight(250, Qt::SmoothTransformation));
  logo->setContentsMargins(15, 0, 15, 0);

  QVBoxLayout *main_layout = new QVBoxLayout;
  QHBoxLayout *h_layout = new QHBoxLayout;

  tab->setLayout(main_layout);
  main_layout->addLayout(h_layout);
  main_layout->addWidget(copyright);

  copyright->setAlignment(Qt::AlignCenter);
  copyright->setContentsMargins(0, 15, 0, 0);

  h_layout->setAlignment(Qt::AlignLeft);
  h_layout->addWidget(logo);
  h_layout->addWidget(text_label);
}

void About::setupLicensesTab(QWidget* tab) {
  QHBoxLayout *layout = new QHBoxLayout(this);

  QListWidget *listWidget = new QListWidget();
  layout->addWidget(listWidget, 1);

  QTextEdit *textEdit = new QTextEdit();
  textEdit->setReadOnly(true);
  layout->addWidget(textEdit, 3);

  QMap<QString, QString> licenses;
  loadLicenses(licenses);

  for (auto it = licenses.constBegin(); it != licenses.constEnd(); ++it) {
    listWidget->addItem(it.key());
  }

  connect(listWidget, &QListWidget::currentTextChanged, [textEdit, licenses](const QString &currentText) {
    textEdit->setText(licenses.value(currentText));
  });

  tab->setLayout(layout);
}

void About::loadLicenses(QMap<QString, QString>& licenses) {
  QDir directory(":/legal");

  foreach (QString filename, directory.entryList(QDir::Files)) {
    QFile file(directory.filePath(filename));

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      QString content = in.readAll();
      licenses.insert(filename, content);

      file.close();
    } else {
      qDebug() << "Failed to open file:" << filename;
    }
  }
}
