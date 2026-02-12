#version 450

layout(location = 0) in vec2 vUv;

layout(binding = 1) uniform sampler2D contentTex;
layout(binding = 2) uniform sampler2D maskTex;
layout(binding = 3) uniform sampler2D itemIdTex;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 overlayAndShadow;   // x=overlayOpacity
  vec4 columnLayout;       // x=hexStart, y=hexWidth, z=asciiStart, w=asciiWidth
  vec4 viewInfo;           // x=viewWidth, y=viewHeight, z=flipY, w=devicePixelRatio
  vec4 outlineColor;       // rgba
  vec4 itemIdWindow;       // x=lineHeight, y=scrollY, z=itemIdStartLine, w=itemIdHeight
};

layout(location = 0) out vec4 fragColor;

float decodeItemId(vec2 uv) {
  // Item ids are packed into RG8 (little-endian) by the renderer.
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

  // Keep border thickness visually consistent across scale factors.
  float edgePx = 2.0 / max(dpr, 1.0);
  float leftEdge = step(localX, edgePx);
  float rightEdge = step(cellW - edgePx, localX);
  float topEdge = step(localY, edgePx);
  float bottomEdge = step(lineHeightPx - edgePx, localY);

  // Ownership rule: draw each shared boundary from one side only to avoid doubled seams.
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

void main() {
  // Base pass result and selection mask produced by earlier render passes.
  vec4 base = texture(contentTex, vUv);
  float selected = clamp(texture(maskTex, vUv).r, 0.0, 1.0);

  float x = vUv.x * viewInfo.x;
  float logicalY = mix(1.0 - vUv.y, vUv.y, viewInfo.z);
  float y = logicalY * viewInfo.y;
  bool inHex = (x >= columnLayout.x) && (x < columnLayout.x + columnLayout.y);
  bool inAscii = (columnLayout.w > 0.0) && (x >= columnLayout.z) && (x < columnLayout.z + columnLayout.w);
  float inColumns = (inHex || inAscii) ? 1.0 : 0.0;
  float outlineMask = computeOutlineMask(x, y, inHex, inAscii, viewInfo.w);

  // Composite order: dim columns, add modifier outline, then restore selected pixels.
  vec3 dimmed = mix(base.rgb, vec3(0.0), overlayAndShadow.x * inColumns);
  vec3 withOutline = mix(dimmed, outlineColor.rgb, outlineMask * outlineColor.a);
  vec3 restored = mix(withOutline, base.rgb, selected);
  fragColor = vec4(restored, base.a);
}
