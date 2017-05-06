#include <QFontDatabase>
#include <QBoxLayout>
#include <QLabel>

#include "HeaderContainer.h"

HeaderContainer::HeaderContainer(QWidget *content, const QString &title, QObject *parent)
    : QWidget()
{
  QFont font = QFontDatabase::systemFont(QFontDatabase::TitleFont);
  font.setPixelSize(11);

  header = new QLabel(title);
  layout = new QVBoxLayout;

  header->setContentsMargins(10, 0, 10, 0);
  header->setFont(font);
  layout->addWidget(header);
  layout->addWidget(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setMargin(0);
  layout->setSpacing(0);

  setLayout(layout);
}
