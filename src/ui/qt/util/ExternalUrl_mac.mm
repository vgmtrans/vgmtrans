/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>

#include <QByteArray>
#include <QDebug>

void openExternalUrlNative(const QByteArray& encodedUrl) {
  @autoreleasepool {
    CFURLRef url = CFURLCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(encodedUrl.constData()),
        encodedUrl.size(), kCFStringEncodingUTF8, nullptr);
    if (url == nullptr) {
      qWarning() << "Could not open bug URL:" << encodedUrl;
      return;
    }
    [[NSWorkspace sharedWorkspace] openURL:(__bridge NSURL*)url];
    CFRelease(url);
  }
}
