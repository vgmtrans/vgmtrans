/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "main/base/ToastType.h"
#include "value/base/CoreTypes.h"

#include <functional>
#include <span>

class QWidget;
class QAbstractButton;
class QString;

namespace vgmtrans::ui {
class WorkspaceController;
}

namespace vgmtrans::core {
struct ExportRequest;
}

namespace stitchui {
using ShowToast = std::function<void(const QString&, ToastType, int)>;
using VisibilityChanged = std::function<void(bool)>;
using PlanChanged = std::function<void(std::span<const vgmtrans::core::CollectionId>)>;

struct Callbacks {
  ShowToast showToast;
  VisibilityChanged visibilityChanged;
  PlanChanged planChanged;
};

void openCollectionStitchBalloon(vgmtrans::ui::WorkspaceController& workspace,
                                 std::span<const vgmtrans::core::CollectionId> initialCollections,
                                 const vgmtrans::core::ExportRequest& request, Callbacks callbacks,
                                 QWidget* parent = nullptr, QWidget* anchor = nullptr,
                                 QAbstractButton* toggleButton = nullptr);
[[nodiscard]] bool toggleCollectionStitchBalloon(vgmtrans::ui::WorkspaceController& workspace,
                                                 std::span<const vgmtrans::core::CollectionId> initialCollections,
                                                 const vgmtrans::core::ExportRequest& request, Callbacks callbacks,
                                                 QWidget* parent = nullptr, QWidget* anchor = nullptr,
                                                 QAbstractButton* toggleButton = nullptr);
}  // namespace stitchui
