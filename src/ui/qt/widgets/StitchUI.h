/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <vector>

class QWidget;
class QAbstractButton;
class VGMColl;

namespace stitchui {
void openCollectionStitchBalloon(const std::vector<VGMColl*>& initialCollections,
                                 QWidget* parent = nullptr,
                                 QWidget* anchor = nullptr,
                                 QAbstractButton* toggleButton = nullptr);
[[nodiscard]] bool toggleCollectionStitchBalloon(const std::vector<VGMColl*>& initialCollections,
                                                 QWidget* parent = nullptr,
                                                 QWidget* anchor = nullptr,
                                                 QAbstractButton* toggleButton = nullptr);
}
