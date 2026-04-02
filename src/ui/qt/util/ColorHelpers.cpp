/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "ColorHelpers.h"
#include "Colors.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
// Alpha-composite a foreground color over a background color.
QColor compositeColors(const QColor &foreground, const QColor &background) {
  const qreal foregroundAlpha = foreground.alphaF();
  const qreal backgroundAlpha = background.alphaF();
  const qreal outAlpha = foregroundAlpha + backgroundAlpha * (1.0 - foregroundAlpha);
  if (outAlpha <= 0.0) {
    return {};
  }

  const auto compositeChannel = [=](qreal foregroundChannel, qreal backgroundChannel) {
    return (foregroundChannel * foregroundAlpha +
            backgroundChannel * backgroundAlpha * (1.0 - foregroundAlpha)) /
           outAlpha;
  };

  return QColor::fromRgbF(compositeChannel(foreground.redF(), background.redF()),
                          compositeChannel(foreground.greenF(), background.greenF()),
                          compositeChannel(foreground.blueF(), background.blueF()), outAlpha);
}

// Convert an sRGB channel to linear space for luminance calculations.
qreal linearizedColorComponent(qreal component) {
  return component <= 0.03928 ? component / 12.92
                              : std::pow((component + 0.055) / 1.055, 2.4);
}

// Compute the WCAG relative luminance of a color.
qreal relativeLuminance(const QColor &color) {
  return 0.2126 * linearizedColorComponent(color.redF()) +
         0.7152 * linearizedColorComponent(color.greenF()) +
         0.0722 * linearizedColorComponent(color.blueF());
}

// Compute the contrast ratio between a foreground color and a background color.
qreal contrastRatio(const QColor &foreground, const QColor &background) {
  const QColor visibleForeground = compositeColors(foreground, background);
  const qreal foregroundLuminance = relativeLuminance(visibleForeground);
  const qreal backgroundLuminance = relativeLuminance(background);
  const qreal lighter = std::max(foregroundLuminance, backgroundLuminance);
  const qreal darker = std::min(foregroundLuminance, backgroundLuminance);
  return (lighter + 0.05) / (darker + 0.05);
}
}

// Format a QColor as a CSS rgba(...) string.
QString cssColor(const QColor &color) {
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alpha());
}

// Linearly blend two colors by the supplied foreground weight.
QColor blendColors(const QColor &foreground, const QColor &background, qreal foregroundWeight) {
  const qreal backgroundWeight = 1.0 - foregroundWeight;
  return QColor::fromRgbF(foreground.redF() * foregroundWeight + background.redF() * backgroundWeight,
                          foreground.greenF() * foregroundWeight + background.greenF() * backgroundWeight,
                          foreground.blueF() * foregroundWeight + background.blueF() * backgroundWeight,
                          foreground.alphaF() * foregroundWeight + background.alphaF() * backgroundWeight);
}

// Derive the translucent item selection fill from the palette's Accent color.
QColor itemSelectionFillColor(const QPalette &palette, QPalette::ColorGroup colorGroup) {
  QColor accentColor = palette.brush(colorGroup, QPalette::Accent).color();
  accentColor.setAlpha(UIColors::ItemSelectionAccentAlpha);
  return accentColor;
}

// Choose the highest-contrast text color for a foreground shown over a background, preferring
// palette colors before falling back to black or white.
QColor contrastingTextColor(const QColor &foreground, const QColor &background, const QPalette &palette,
                            QPalette::ColorGroup colorGroup) {
  constexpr qreal kPreferredContrastRatio = 4.5;

  const QColor effectiveBackground = compositeColors(foreground, background);
  const std::array<QColor, 5> paletteCandidates{
      palette.color(colorGroup, QPalette::Text),
      palette.color(colorGroup, QPalette::WindowText),
      palette.color(colorGroup, QPalette::ButtonText),
      palette.color(colorGroup, QPalette::HighlightedText),
      palette.color(colorGroup, QPalette::BrightText),
  };

  QColor bestPaletteColor;
  qreal bestPaletteContrast = -1.0;
  for (const QColor &candidate : paletteCandidates) {
    const qreal candidateContrast = contrastRatio(candidate, effectiveBackground);
    if (candidateContrast > bestPaletteContrast) {
      bestPaletteContrast = candidateContrast;
      bestPaletteColor = candidate;
    }
  }

  const QColor darkFallback(Qt::black);
  const QColor lightFallback(Qt::white);
  const qreal darkContrast = contrastRatio(darkFallback, effectiveBackground);
  const qreal lightContrast = contrastRatio(lightFallback, effectiveBackground);
  const QColor fallbackColor = darkContrast >= lightContrast ? darkFallback : lightFallback;
  const qreal fallbackContrast = std::max(darkContrast, lightContrast);

  if (bestPaletteContrast >= kPreferredContrastRatio || bestPaletteContrast >= fallbackContrast) {
    return bestPaletteColor;
  }

  return fallbackColor;
}

// Report whether the palette's window color should be treated as dark.
bool isDarkPalette(const QPalette &palette) {
  return palette.color(QPalette::Window).lightnessF() < 0.5;
}
