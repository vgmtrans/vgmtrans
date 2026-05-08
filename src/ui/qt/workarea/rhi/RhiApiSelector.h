/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QRhiWidget>

#if defined(Q_OS_LINUX) && QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#include <QVulkanInstance>

#include <rhi/qrhi_platform.h>
#endif

namespace QtUi {

inline QRhiWidget::Api preferredRhiWidgetApi() {
  // Keep the decision stable so the startup primer and every RHI widget
  // attach to the same top-level QRhi backend.
  static const QRhiWidget::Api api = [] {
#if defined(Q_OS_LINUX)
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    QVulkanInstance vkInstance;
    vkInstance.setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
    if (vkInstance.create()) {
      return QRhiWidget::Api::Vulkan;
    }
#endif
#if QT_CONFIG(opengl)
    return QRhiWidget::Api::OpenGL;
#else
    return QRhiWidget::Api::Null;
#endif
#elif defined(Q_OS_WIN)
    return QRhiWidget::Api::Direct3D11;
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    return QRhiWidget::Api::Metal;
#elif QT_CONFIG(opengl)
    return QRhiWidget::Api::OpenGL;
#else
    return QRhiWidget::Api::Null;
#endif
  }();

  return api;
}

}  // namespace QtUi
