#ifndef VGMTRANS_HEADERCONTAINER_H
#define VGMTRANS_HEADERCONTAINER_H

#include <QWidget>

class QVBoxLayout;
class QLabel;

class HeaderContainer : public QWidget
{
  Q_OBJECT

 public:
  HeaderContainer(QWidget *content, const QString &title, QObject *parent = 0);

 private:
  QVBoxLayout *layout;
  QLabel *header;

};

#endif //VGMTRANS_HEADERCONTAINER_H
