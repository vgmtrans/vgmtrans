/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ExternalUrl.h"

#include <QDesktopServices>
#include <QUrl>

#ifdef Q_OS_MAC
void openExternalUrlNative(const QByteArray& encodedUrl);
#endif

void openExternalUrl(const QUrl& url) {
  const QByteArray encoded = url.toEncoded();
#ifdef Q_OS_MAC
  // NSURL re-encodes issue-form query values. Use CFURL so the generated bug
  // report keeps the exact percent-encoded content.
  openExternalUrlNative(encoded);
#else
  QDesktopServices::openUrl(QUrl::fromEncoded(encoded));
#endif
}
