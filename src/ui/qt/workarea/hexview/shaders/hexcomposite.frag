#version 450

layout(location = 0) in vec2 vUv;

layout(binding = 1) uniform sampler2D contentTex;
layout(binding = 2) uniform sampler2D maskTex;
layout(binding = 3) uniform sampler2D edgeTex;
layout(binding = 4) uniform sampler2D playbackColorTex;
layout(binding = 5) uniform sampler2D itemIdTex;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 overlayAndShadow;   // x=overlayOpacity, y=shadowStrength, z=shadowOffsetX, w=shadowOffsetY
  vec4 columnLayout;       // x=hexStart, y=hexWidth, z=asciiStart, w=asciiWidth
  vec4 viewInfo;           // x=viewWidth, y=viewHeight, z=flipY, w=devicePixelRatio
  vec4 effectInfo;         // x=glowStrength
  vec4 outlineColor;       // rgba
  vec4 itemIdWindow;       // x=lineHeight, y=scrollY, z=itemIdStartLine, w=itemIdHeight
};

layout(location = 0) out vec4 fragColor;

const vec3 GLOW_LOW_RGB = vec3(40.0 / 255.0);
const vec3 GLOW_HIGH_RGB = vec3(230.0 / 255.0);

float decodeItemId(vec2 uv) {
  vec2 rg = texture(itemIdTex, uv).rg * 255.0 + 0.5;
  return rg.r + rg.g * 256.0;
}

float computeOutlineMask(float xPx, float yPx, bool inHex, bool inAscii, float dpr) {
  float lineHeightPx = itemIdWindow.x;
  if (outlineColor.a <= 0.0 || lineHeightPx <= 0.0 || itemIdWindow.w <= 0.0) {
    return 0.0;
  }
  if (!(inHex || inAscii)) {
    return 0.0;
  }

  const float bytesPerLine = 16.0;
  float lineF = floor((yPx + itemIdWindow.y) / lineHeightPx);
  float lineIdx = lineF - itemIdWindow.z;
  if (lineIdx < 0.0 || lineIdx >= itemIdWindow.w) {
    return 0.0;
  }

  float localY = (yPx + itemIdWindow.y) - lineF * lineHeightPx;
  float cellW = inHex ? (columnLayout.y / bytesPerLine) : (columnLayout.w / bytesPerLine);
  float localX = inHex ? (xPx - columnLayout.x) : (xPx - columnLayout.z);
  if (cellW <= 0.0 || localX < 0.0) {
    return 0.0;
  }

  float byteIdx = floor(localX / cellW);
  if (byteIdx < 0.0 || byteIdx >= bytesPerLine) {
    return 0.0;
  }
  localX -= byteIdx * cellW;

  float edgePx = 2.0 / dpr;
  float leftEdge = step(localX, edgePx);
  float rightEdge = step(cellW - edgePx, localX);
  float topEdge = step(localY, edgePx);
  float bottomEdge = step(lineHeightPx - edgePx, localY);
  // Interior pixels can skip all item-id texture sampling.
  if (leftEdge + rightEdge + topEdge + bottomEdge <= 0.0) {
    return 0.0;
  }

  vec2 texSize = vec2(bytesPerLine, itemIdWindow.w);
  vec2 texel = vec2(1.0) / texSize;
  vec2 centerUv = vec2(byteIdx + 0.5, lineIdx + 0.5) / texSize;
  float id = decodeItemId(centerUv);
  if (id < 0.5) {
    return 0.0;
  }

  float leftId = id;
  if (leftEdge > 0.0 && byteIdx > 0.0) {
    leftId = decodeItemId(centerUv - vec2(texel.x, 0.0));
  }
  float rightId = id;
  if (rightEdge > 0.0 && byteIdx < bytesPerLine - 1.0) {
    rightId = decodeItemId(centerUv + vec2(texel.x, 0.0));
  }
  float upId = id;
  if (topEdge > 0.0 && lineIdx > 0.0) {
    upId = decodeItemId(centerUv - vec2(0.0, texel.y));
  }
  float downId = id;
  if (bottomEdge > 0.0 && lineIdx < itemIdWindow.w - 1.0) {
    downId = decodeItemId(centerUv + vec2(0.0, texel.y));
  }

  float drawLeft = (leftId < 0.5 || id < leftId) ? 1.0 : 0.0;
  float drawRight = (rightId < 0.5 || id < rightId) ? 1.0 : 0.0;
  float drawUp = (upId < 0.5 || id < upId) ? 1.0 : 0.0;
  float drawDown = (downId < 0.5 || id < downId) ? 1.0 : 0.0;

  float edge = leftEdge * drawLeft * step(0.5, abs(id - leftId)) +
               rightEdge * drawRight * step(0.5, abs(id - rightId)) +
               topEdge * drawUp * step(0.5, abs(id - upId)) +
               bottomEdge * drawDown * step(0.5, abs(id - downId));
  return clamp(edge, 0.0, 1.0);
}

vec3 applyDimOutlineAndHighlight(vec3 baseRgb, float inColumns, float highlight, float outlineMask) {
  vec3 dimmed = mix(baseRgb, vec3(0.0), overlayAndShadow.x * inColumns);
  vec3 withOutline = mix(dimmed, outlineColor.rgb, outlineMask * outlineColor.a);
  return mix(withOutline, baseRgb, highlight);
}

float computeShadowHalo(float selectionNorm, float selectedMask) {
  if (overlayAndShadow.y <= 0.0) {
    return 0.0;
  }
  return (1.0 - smoothstep(0.0, 1.0, selectionNorm)) * (1.0 - selectedMask);
}

vec2 computePlaybackHalos(float activeNorm, float fadeNorm, float activeMask,
                          float fadeMask, float fadeAlpha) {
  float anyPlaybackMask = max(activeMask, fadeMask);
  float activeHalo = (1.0 - smoothstep(0.0, 1.0, activeNorm)) * (1.0 - activeMask);
  float fadeHalo = (1.0 - smoothstep(0.0, 1.0, fadeNorm)) * (1.0 - anyPlaybackMask) * fadeAlpha;
  return vec2(activeHalo, fadeHalo);
}

void main() {
  vec4 base = texture(contentTex, vUv);

  float shadowStrength = overlayAndShadow.y;
  vec2 shadowOffset = overlayAndShadow.zw;

  float hexStart = columnLayout.x;
  float hexWidth = columnLayout.y;
  float asciiStart = columnLayout.z;
  float asciiWidth = columnLayout.w;

  vec2 viewSize = viewInfo.xy;
  float flipY = viewInfo.z;
  float dpr = max(viewInfo.w, 1.0);
  vec3 glowLow = GLOW_LOW_RGB;
  float glowStrength = effectInfo.x;
  vec3 glowHighRgb = GLOW_HIGH_RGB;

  float x = vUv.x * viewSize.x;
  bool inHex = (x >= hexStart) && (x < hexStart + hexWidth);
  bool inAscii = (asciiWidth > 0.0) && (x >= asciiStart) && (x < asciiStart + asciiWidth);
  float inColumns = (inHex || inAscii) ? 1.0 : 0.0;

  vec4 mask = texture(maskTex, vUv);
  float sel = clamp(mask.r, 0.0, 1.0);
  float playActiveMask = clamp(mask.g, 0.0, 1.0);
  float playFadeMask = clamp(mask.b, 0.0, 1.0);
  float playFadeAlpha = clamp(mask.a, 0.0, 1.0);
  float highlight = max(sel, max(playActiveMask, playFadeMask * playFadeAlpha));
  float logicalY = mix(1.0 - vUv.y, vUv.y, flipY);
  float y = logicalY * viewSize.y;
  float outlineMask = computeOutlineMask(x, y, inHex, inAscii, dpr);

  vec3 restored = applyDimOutlineAndHighlight(base.rgb, inColumns, highlight, outlineMask);

  vec2 shadowUv = vUv + shadowOffset;
  vec4 edgeShadow = texture(edgeTex, shadowUv);
  float selHalo = computeShadowHalo(edgeShadow.r, sel);

  float shadowAlpha = selHalo * shadowStrength;
  vec3 withShadow = restored * (1.0 - shadowAlpha);

  vec4 edgeGlow = texture(edgeTex, vUv);
  vec4 playbackColorSample = texture(playbackColorTex, vUv);
  vec2 playHalos = computePlaybackHalos(edgeGlow.g, edgeGlow.b, playActiveMask, playFadeMask,
                                        playFadeAlpha);
  float activeGlow = clamp(playHalos.x * glowStrength, 0.0, 1.0);
  float fadeGlow = clamp(playHalos.y * glowStrength, 0.0, 1.0);
  float glowValue = max(activeGlow, fadeGlow);

  vec3 trackGlowBase = playbackColorSample.rgb;
  float hasTrackGlowColor = step(0.001, playbackColorSample.a);
  glowLow = mix(glowLow, trackGlowBase * 0.34, hasTrackGlowColor);
  vec3 glowHighTint = mix(glowHighRgb,
                          min(trackGlowBase * 1.18 + vec3(0.06), vec3(1.0)),
                          hasTrackGlowColor);

  vec3 glowColor = mix(glowLow, glowHighTint, smoothstep(0.0, 1.0, glowValue));

  vec3 withGlow = mix(withShadow, glowColor, clamp(glowValue * 1.1, 0.0, 1.0));
  withGlow = clamp(withGlow + glowColor * glowValue * 0.50, 0.0, 1.0);

  fragColor = vec4(withGlow, base.a);
}
