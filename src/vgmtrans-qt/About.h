
#pragma once

#include <QDialog>

class About final : public QDialog {
    Q_OBJECT
   public:
    explicit About(QWidget *parent = nullptr);
};
