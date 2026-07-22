/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ToastWindow.h"

#import <AppKit/AppKit.h>

#include <QWidget>

// Attaching the toast panel as an NSWindow child makes it track the main window
// during live drags instead of waiting for delayed Qt move notifications.
void setToastWindowChildOf(QWidget* child, QWidget* parent) {
  @autoreleasepool {
    if (child == nullptr) {
      return;
    }

    NSView* childView = reinterpret_cast<NSView*>(child->winId());
    NSWindow* childWindow = childView ? childView.window : nil;
    NSWindow* parentWindow = nil;
    if (parent != nullptr) {
      NSView* parentView = reinterpret_cast<NSView*>(parent->winId());
      parentWindow = parentView ? parentView.window : nil;
    }
    if (childWindow == nil) {
      return;
    }

    if (NSWindow* existingParent = childWindow.parentWindow;
        existingParent != nil && existingParent != parentWindow) {
      [existingParent removeChildWindow:childWindow];
    }
    if (parentWindow != nil && childWindow.parentWindow != parentWindow) {
      [parentWindow addChildWindow:childWindow ordered:NSWindowAbove];
    }
  }
}
