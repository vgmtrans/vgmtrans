#import <CoreFoundation/CoreFoundation.h>
#import <AppKit/AppKit.h>
#include <QUrl>

#include "LogManager.h"

void qtOpenUrlNative(const QByteArray& encodedUrl) {
  @autoreleasepool {
    CFURLRef cfUrl = CFURLCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(encodedUrl.constData()),
        encodedUrl.size(),
        kCFStringEncodingUTF8,
        nullptr  // no base URL
    );

    if (cfUrl) {
      NSURL* nsUrl = (__bridge_transfer NSURL*)cfUrl;
      [[NSWorkspace sharedWorkspace] openURL:nsUrl];
    } else {
      L_ERROR("Could not open bug URL: {}", encodedUrl.constData());
    }
  }
}
