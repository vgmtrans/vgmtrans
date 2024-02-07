/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <QDialog>

class QTabWidget;

class About final : public QDialog {
   public:
    explicit About(QWidget *parent = nullptr);

   private:
    void setupInfoTab(QWidget* tab);
    void setupLicensesTab(QWidget* tab);
    void loadLicenses(QMap<QString, QString>& licenses);

    QTabWidget* tabs{};
};