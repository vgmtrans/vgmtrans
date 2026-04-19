/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <concepts>

#include <QString>
#include <QWidget>

#include "util/InstructionHintLayout.h"

class QEvent;
class QLabel;
class QStackedLayout;

namespace emptystate_detail {
template <typename T>
concept HasEmptyMethod = requires(const T &value) {
  { value.empty() } -> std::convertible_to<bool>;
};

template <typename T>
concept HasCountMethod = requires(const T &value) {
  { value.count() } -> std::integral;
};

template <typename T>
concept HasRowCountMethod = requires(const T &value) {
  { value.rowCount() } -> std::integral;
};
}

template <typename T>
concept EmptyStateSource = emptystate_detail::HasEmptyMethod<T>
                        || emptystate_detail::HasCountMethod<T>
                        || emptystate_detail::HasRowCountMethod<T>;

class EmptyStateWidget final : public QWidget {
public:
  explicit EmptyStateWidget(const InstructionHint &headingHint,
                            QWidget *contentWidget = new QWidget(),
                            QWidget *parent = nullptr);

  [[nodiscard]] QWidget *contentWidget() const { return m_contentWidget; }

  void setInstructionHint(const InstructionHint &headingHint);
  void setBodyText(const QString &body);
  void setEmptyStateContent(const QString &iconPath, const QString &heading,
                            const QString &body = {});
  void setEmptyStateShown(bool shown);

  template <EmptyStateSource T>
  void setEmptyStateFrom(const T &source) {
    if constexpr (emptystate_detail::HasEmptyMethod<T>) {
      setEmptyStateShown(source.empty());
    } else if constexpr (emptystate_detail::HasCountMethod<T>) {
      setEmptyStateShown(source.count() == 0);
    } else {
      setEmptyStateShown(source.rowCount() == 0);
    }
  }

  [[nodiscard]] bool emptyStateShown() const { return m_emptyStateShown; }

protected:
  void changeEvent(QEvent *event) override;

private:
  void refreshEmptyStatePresentation();

  QWidget *m_contentWidget = nullptr;
  QWidget *m_emptyView = nullptr;
  QStackedLayout *m_stack = nullptr;
  QLabel *m_iconLabel = nullptr;
  QLabel *m_headingLabel = nullptr;
  QLabel *m_bodyLabel = nullptr;
  InstructionHint m_headingHint;
  QString m_emptyBody;
  bool m_emptyStateShown = false;
};
