/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <QDockWidget>

#include <LogManager.h>

class QTextEdit;
class QComboBox;
class QPushButton;

class Logger
    : public QDockWidget
    , public Sink
{
    Q_OBJECT

  public:
    explicit Logger(QWidget *parent = nullptr);
    ~Logger() override;

    bool Push(const Entry &e) override;

  signals:
    void closeEvent(QCloseEvent *) override;

  private:
    void CreateElements();
    void ConnectElements();
    void exportLog();

    QWidget *logger_wrapper;
    QTextEdit *logger_textarea;

    QComboBox *logger_filter;
    QPushButton *logger_clear;
    QPushButton *logger_save;
};