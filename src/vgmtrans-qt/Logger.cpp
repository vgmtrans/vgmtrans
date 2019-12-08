/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Logger.h"

#include <QGridLayout>
#include <QComboBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QFileDialog>

#include <LogManager.h>

Logger::Logger(QWidget *parent) : QDockWidget("Log", parent) {
    setAllowedAreas(Qt::AllDockWidgetAreas);

    CreateElements();
    ConnectElements();
}

Logger::~Logger() {
    LogManager::instance().removeSink(this);
}

void Logger::CreateElements() {
    logger_wrapper = new QWidget;

    logger_textarea = new QPlainTextEdit(logger_wrapper);
    logger_textarea->setReadOnly(true);
    logger_textarea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    logger_textarea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    QPalette palette = logger_textarea->palette();
    palette.setColor(QPalette::Base, Qt::black);
    palette.setColor(QPalette::Text, Qt::white);
    logger_textarea->setPalette(palette);

    logger_filter = new QComboBox(logger_wrapper);
    logger_filter->setEditable(false);
    logger_filter->addItems({"Errors", "Errors, warnings", "Errors, warnings, information",
                             "Complete debug information"});
    logger_filter->setCurrentIndex(static_cast<int>(logLevel()));

    logger_clear = new QPushButton("Clear", logger_wrapper);
    logger_save = new QPushButton("Export log", logger_wrapper);

    QGridLayout *logger_layout = new QGridLayout;
    logger_layout->addWidget(logger_filter, 0, 0);
    logger_layout->addWidget(logger_clear, 0, 1);
    logger_layout->addWidget(logger_save, 0, 2);
    logger_layout->addWidget(logger_textarea, 1, 0, 1, -1);

    logger_wrapper->setLayout(logger_layout);

    setWidget(logger_wrapper);
};

void Logger::ConnectElements() {
    connect(logger_clear, &QPushButton::pressed, logger_textarea, &QPlainTextEdit::clear);
    connect(logger_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [=](int level) { setLogLevel(static_cast<LogLevel>(level)); });
    connect(logger_save, &QPushButton::pressed, this, &Logger::exportLog);
}

void Logger::exportLog() {
    if (logger_textarea->toPlainText().isEmpty()) {
        return;
    }

    auto path = QFileDialog::getSaveFileName(this, "Export log");
    if (path.isEmpty()) {
        return;
    }

    QSaveFile log(path);
    log.open(QIODevice::WriteOnly);

    QByteArray out_buf;
    out_buf.append(logger_textarea->toPlainText().toUtf8());
    log.write(out_buf);
    log.commit();
}

bool Logger::Push(const Entry &e) {
    if (e.level > m_level) {
        return false;
    }

    const char *log_colors[] = {"red", "yellow", "darkgrey", "white"};

    logger_textarea->appendHtml(QStringLiteral("<font color=%3>[%1:%2] %4</font>")
                                    .arg(QString::fromStdString(e.file), QString::number(e.line),
                                         QString(log_colors[static_cast<int>(e.level)]),
                                         QString::fromStdString(e.message)));

    return true;
}
