/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ToastWindow.h"

#include <QtGlobal>

#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
void setToastWindowChildOf(QWidget*, QWidget*) {
}
#endif
