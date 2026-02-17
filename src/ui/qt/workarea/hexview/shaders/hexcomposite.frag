#version 450

layout(location = 0) in vec2 vUv;

layout(binding = 1) uniform sampler2D contentTex;
layout(binding = 2) uniform sampler2D maskTex;
layout(binding = 3) uniform sampler2D edgeTex;
layout(binding = 4) uniform sampler2D itemIdTex;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 overlayAndShadow;   // x=overlayOpacity, y=shadowStrength, z=shadowOffsetX, w=shadowOffsetY
  vec4 columnLayout;       // x=hexStart, y=hexWidth, z=asciiStart, w=asciiWidth
  vec4 viewAndTime;        // x=viewWidth, y=viewHeight, z=flipY, w=timeSeconds
  vec4 shadowColor;
  vec4 glowLowAndStrength; // rgb=glowLow, a=glowStrength
  vec4 glowHighAndDpr;     // rgb=glowHigh, a=devicePixelRatio
  vec4 outlineColor;       // rgba
  vec4 itemIdWindow;       // x=lineHeight, y=scrollY, z=itemIdStartLine, w=itemIdHeight
};

layout(location = 0) out vec4 fragColor;

float hash2(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise2(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  float a = hash2(i);
  float b = hash2(i + vec2(1.0, 0.0));
  float c = hash2(i + vec2(0.0, 1.0));
  float d = hash2(i + vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

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

  vec2 texSize = vec2(bytesPerLine, itemIdWindow.w);
  vec2 texel = vec2(1.0) / texSize;
  vec2 centerUv = vec2(byteIdx + 0.5, lineIdx + 0.5) / texSize;
  float id = decodeItemId(centerUv);
  if (id < 0.5) {
    return 0.0;
  }

  float leftId = (byteIdx > 0.0) ? decodeItemId(centerUv - vec2(texel.x, 0.0)) : id;
  float rightId = (byteIdx < bytesPerLine - 1.0) ? decodeItemId(centerUv + vec2(texel.x, 0.0)) : id;
  float upId = (lineIdx > 0.0) ? decodeItemId(centerUv - vec2(0.0, texel.y)) : id;
  float downId = (lineIdx < itemIdWindow.w - 1.0) ? decodeItemId(centerUv + vec2(0.0, texel.y)) : id;

  float edgePx = 2.0 / dpr;
  float leftEdge = step(localX, edgePx);
  float rightEdge = step(cellW - edgePx, localX);
  float topEdge = step(localY, edgePx);
  float bottomEdge = step(lineHeightPx - edgePx, localY);

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

float computeSelectionHighlight(vec4 mask) {
  float sel = clamp(mask.r, 0.0, 1.0);
  float playActiveMask = clamp(mask.g, 0.0, 1.0);
  float playFadeMask = clamp(mask.b, 0.0, 1.0);
  float playFadeAlpha = clamp(mask.a, 0.0, 1.0);
  return max(sel, max(playActiveMask, playFadeMask * playFadeAlpha));
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

float computePlaybackHalo(float activeNorm, float fadeNorm, float activeMask,
                          float fadeMask, float fadeAlpha) {
  float anyPlaybackMask = max(activeMask, fadeMask);
  float activeHalo = (1.0 - smoothstep(0.0, 1.0, activeNorm)) * (1.0 - activeMask);
  float fadeHalo = (1.0 - smoothstep(0.0, 1.0, fadeNorm)) * (1.0 - anyPlaybackMask) * fadeAlpha;
  return max(activeHalo, fadeHalo);
}

float computeGlowTurbulence(vec2 uv, vec2 viewSize, float timeSeconds) {
  vec2 p = uv * viewSize * 0.055;
  float t = timeSeconds * 0.85;

  float n1 = noise2(p + vec2(0.0, t * 1.2));
  float n2 = noise2(p * 1.7 + vec2(7.4, t * 1.6));
  float n3 = noise2(p * 2.9 + vec2(-3.1, t * 2.2));

  float flicker = mix(n1, n2, 0.6);
  float lick = mix(n2, n3, 0.5);
  return 0.65 + 0.55 * mix(flicker, lick, 0.5);
}

void main() {
  vec4 base = texture(contentTex, vUv);

  float shadowStrength = overlayAndShadow.y;
  vec2 shadowOffset = overlayAndShadow.zw;

  float hexStart = columnLayout.x;
  float hexWidth = columnLayout.y;
  float asciiStart = columnLayout.z;
  float asciiWidth = columnLayout.w;

  vec2 viewSize = viewAndTime.xy;
  float flipY = viewAndTime.z;
  float dpr = max(glowHighAndDpr.a, 1.0);
  float time = viewAndTime.w;
  vec3 glowLow = glowLowAndStrength.rgb;
  float glowStrength = glowLowAndStrength.a;
  vec3 glowHigh = glowHighAndDpr.rgb;

  float x = vUv.x * viewSize.x;
  bool inHex = (x >= hexStart) && (x < hexStart + hexWidth);
  bool inAscii = (asciiWidth > 0.0) && (x >= asciiStart) && (x < asciiStart + asciiWidth);
  float inColumns = (inHex || inAscii) ? 1.0 : 0.0;

  vec4 mask = texture(maskTex, vUv);
  float sel = clamp(mask.r, 0.0, 1.0);
  float playActiveMask = clamp(mask.g, 0.0, 1.0);
  float playFadeMask = clamp(mask.b, 0.0, 1.0);
  float playFadeAlpha = clamp(mask.a, 0.0, 1.0);
  float highlight = computeSelectionHighlight(mask);
  float logicalY = mix(1.0 - vUv.y, vUv.y, flipY);
  float y = logicalY * viewSize.y;
  float outlineMask = computeOutlineMask(x, y, inHex, inAscii, dpr);

  vec3 restored = applyDimOutlineAndHighlight(base.rgb, inColumns, highlight, outlineMask);

  vec2 shadowUv = vUv + shadowOffset;
  vec4 edgeShadow = texture(edgeTex, shadowUv);
  float selHalo = computeShadowHalo(edgeShadow.r, sel);

  float shadowAlpha = selHalo * shadowStrength * shadowColor.a;
  vec3 withShadow = mix(restored, shadowColor.rgb, shadowAlpha);

  vec4 edgeGlow = texture(edgeTex, vUv);
  float playHalo = computePlaybackHalo(edgeGlow.g, edgeGlow.b, playActiveMask, playFadeMask,
                                       playFadeAlpha);
  float turbulence = computeGlowTurbulence(vUv, viewSize, time);

  float flame = clamp(playHalo * glowStrength * turbulence, 0.0, 1.0);

  float flameRamp = smoothstep(0.0, 1.0, flame);
  vec3 flameColor = mix(glowLow, glowHigh, flameRamp);

  vec3 withGlow = mix(withShadow, flameColor, clamp(flame, 0.0, 1.0));
  withGlow = clamp(withGlow + flameColor * flame * 0.35, 0.0, 1.0);

  fragColor = vec4(withGlow, base.a);
}
