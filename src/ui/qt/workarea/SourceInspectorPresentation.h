/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "util/CapsuleText.h"
#include "value/model/SourceMap.h"

#include <QColor>
#include <QIcon>
#include <QString>

namespace SourceInspectorPresentation {

[[nodiscard]] QColor color(const vgmtrans::core::SourceAnnotation& annotation);
[[nodiscard]] QColor textColor(const vgmtrans::core::SourceAnnotation& annotation);
[[nodiscard]] QIcon icon(const vgmtrans::core::SourceAnnotation& annotation);
[[nodiscard]] CapsuleText description(const vgmtrans::core::SourceAnnotation& annotation);
[[nodiscard]] QString tooltipHtml(const vgmtrans::core::SourceAnnotation& annotation);
[[nodiscard]] QString fieldLabel(const vgmtrans::core::SourceField& field);
[[nodiscard]] QString fieldValue(const vgmtrans::core::SourceField& field);
[[nodiscard]] QIcon fieldIcon();
[[nodiscard]] QString tooltipHtml(const vgmtrans::core::SourceField& field);

}  // namespace SourceInspectorPresentation
