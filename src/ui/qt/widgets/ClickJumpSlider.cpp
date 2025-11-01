/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ClickJumpSlider.h"

void ClickJumpSlider::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        QStyleOptionSlider opt;
        initStyleOption(&opt);

        // Where is the handle? If we clicked it, fall back to normal dragging.
        const QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &opt,
                                                         QStyle::SC_SliderHandle, this);
        if (!handleRect.contains(e->pos())) {
            // Compute a value from the click position using style metrics.
            const int sliderLength = style()->pixelMetric(QStyle::PM_SliderLength, &opt, this);
            const int halfLen      = sliderLength / 2;

            int pos, span;
            if (orientation() == Qt::Horizontal) {
                pos  = std::clamp(int(e->position().x()), halfLen, width() - halfLen);
                span = width() - sliderLength;
            } else {
                pos  = std::clamp(int(e->position().y()), halfLen, height() - halfLen);
                span = height() - sliderLength;
            }

            const int newVal = QStyle::sliderValueFromPosition(
                minimum(), maximum(), pos - halfLen, std::max(1, span), opt.upsideDown);

            // Set the value and start a drag from the new spot (feels natural).
            setValue(newVal);

            // Mark as handled so base class won’t also treat it as a page step.
            // But still pass to base afterwards to let it grab the handle for dragging.
        }
    }

    // Let QSlider do its normal press/drag handling (now at the new position).
    QSlider::mousePressEvent(e);
}