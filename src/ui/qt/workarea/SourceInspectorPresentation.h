/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SourceMap.h"

#include <QColor>
#include <QIcon>
#include <QString>

namespace SourceInspectorPresentation {

[[nodiscard]] QColor color(const vgmtrans::core::SourceAnnotation& annotation);
[[nodiscard]] QColor textColor(const vgmtrans::core::SourceAnnotation& annotation);
[[nodiscard]] QIcon icon(const vgmtrans::core::SourceAnnotation& annotation);
[[nodiscard]] QString description(const vgmtrans::core::SourceAnnotation& annotation);
[[nodiscard]] QString treeText(const vgmtrans::core::SourceAnnotation& annotation, bool showDetails,
                               const QString& description);
[[nodiscard]] QString tooltipHtml(const vgmtrans::core::SourceAnnotation& annotation);

}  // namespace SourceInspectorPresentation
