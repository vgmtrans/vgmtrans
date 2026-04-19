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
class QBoxLayout;
class QLabel;
class QResizeEvent;
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
  void setCompactLayoutHeightThreshold(int threshold);
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
  [[nodiscard]] int compactLayoutHeightThreshold() const {
    return m_compactLayoutHeightThreshold;
  }

protected:
  void changeEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void refreshEmptyStatePresentation();
  void updateLayoutMode();

  QWidget *m_contentWidget = nullptr;
  QWidget *m_emptyView = nullptr;
  QWidget *m_emptyContent = nullptr;
  QWidget *m_textContainer = nullptr;
  QStackedLayout *m_stack = nullptr;
  QBoxLayout *m_emptyContentLayout = nullptr;
  QLabel *m_iconLabel = nullptr;
  QLabel *m_headingLabel = nullptr;
  QLabel *m_bodyLabel = nullptr;
  InstructionHint m_headingHint;
  QString m_emptyBody;
  int m_compactLayoutHeightThreshold = 0;
  bool m_compactLayoutActive = false;
  bool m_emptyStateShown = false;
};
