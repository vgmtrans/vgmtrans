/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QSlider>
#include <QStyleOptionSlider>
#include <QMouseEvent>

class ClickJumpSlider : public QSlider {
    Q_OBJECT
public:
    using QSlider::QSlider; // inherit ctors

protected:
    void mousePressEvent(QMouseEvent* e) override;
};