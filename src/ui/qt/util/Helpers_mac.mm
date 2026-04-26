#import <CoreFoundation/CoreFoundation.h>
#import <AppKit/AppKit.h>
#include <QWidget>
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
      NSURL* nsUrl = (__bridge NSURL*)cfUrl;
      [[NSWorkspace sharedWorkspace] openURL:nsUrl];
      CFRelease(cfUrl);
    } else {
      L_ERROR("Could not open bug URL: {}", encodedUrl.constData());
    }
  }
}

// On macOS, attaching the toast panel as an NSWindow child makes it track the main
// window during live drags instead of waiting for delayed Qt move notifications.
void qtSetMacWindowChildOf(QWidget* child, QWidget* parent) {
  @autoreleasepool {
    if (!child)
      return;

    NSView* childView = reinterpret_cast<NSView*>(child->winId());
    NSWindow* childWindow = childView ? childView.window : nil;
    NSWindow* parentWindow = nil;
    if (parent) {
      NSView* parentView = reinterpret_cast<NSView*>(parent->winId());
      parentWindow = parentView ? parentView.window : nil;
    }

    if (!childWindow)
      return;

    if (NSWindow* existingParent = childWindow.parentWindow; existingParent && existingParent != parentWindow)
      [existingParent removeChildWindow:childWindow];

    if (parentWindow && childWindow.parentWindow != parentWindow)
      [parentWindow addChildWindow:childWindow ordered:NSWindowAbove];
  }
}
