/**
* VGMTrans (c) - 2002-2021
* Licensed under the zlib license
* See the included LICENSE for more information
*/

#pragma once

#include <QObject>

class LambdaEventFilter : public QObject {
  using EventFilterFunction = std::function<bool(QObject*, QEvent*)>;

public:
  LambdaEventFilter(EventFilterFunction fn, QObject *parent = nullptr)
      : QObject(parent), m_function(fn) {}

  bool eventFilter(QObject *watched, QEvent *event) override {
    return m_function(watched, event);
  }

private:
  EventFilterFunction m_function;
};