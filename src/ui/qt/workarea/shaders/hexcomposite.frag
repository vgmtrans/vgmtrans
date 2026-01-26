#version 450

layout(location = 0) in vec2 vUv;

layout(binding = 1) uniform sampler2D contentTex;
layout(binding = 2) uniform sampler2D maskTex;
layout(binding = 3) uniform sampler2D edgeTex;
layout(binding = 4) uniform sampler2D itemIdTex;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 p0;
  vec4 p1;
  vec4 p2;
  vec4 p3;
  vec4 p4;
  vec4 p5;
  vec4 p6;
  vec4 p7;
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

void main() {
  vec4 base = texture(contentTex, vUv);

  float overlayOpacity = p0.x;
  float shadowStrength = p0.y;
  vec2 shadowOffset = p0.zw;

  float hexStart = p1.x;
  float hexWidth = p1.y;
  float asciiStart = p1.z;
  float asciiWidth = p1.w;

  vec2 viewSize = p2.xy;
  float dpr = max(p5.a, 1.0);
  float time = p2.w;
  vec4 shadowColor = p3;
  vec3 glowLow = p4.rgb;
  float glowStrength = p4.a;
  vec3 glowHigh = p5.rgb;
  vec4 outlineColor = p6;
  float lineHeight = p7.x;
  float scrollY = p7.y;
  float idStartLine = p7.z;
  float idHeight = p7.w;

  float x = vUv.x * viewSize.x;
  bool inHex = (x >= hexStart) && (x < hexStart + hexWidth);
  bool inAscii = (asciiWidth > 0.0) && (x >= asciiStart) && (x < asciiStart + asciiWidth);
  float inColumns = (inHex || inAscii) ? 1.0 : 0.0;

  vec3 dimmed = mix(base.rgb, vec3(0.0), overlayOpacity * inColumns);

  vec4 mask = texture(maskTex, vUv);
  float sel = clamp(mask.r, 0.0, 1.0);
  float playActiveMask = clamp(mask.g, 0.0, 1.0);
  float playFadeMask = clamp(mask.b, 0.0, 1.0);
  float playFadeAlpha = clamp(mask.a, 0.0, 1.0);
  float highlight = max(sel, max(playActiveMask, playFadeMask * playFadeAlpha));
  float outlineAlpha = outlineColor.a;
  float outlineMask = 0.0;
  if (outlineAlpha > 0.0 && inColumns > 0.0 && lineHeight > 0.0 && idHeight > 0.0) {
    const float bytesPerLine = 16.0;
    float y = vUv.y * viewSize.y;
    float lineF = floor((y + scrollY) / lineHeight);
    float lineIdx = lineF - idStartLine;
    if (lineIdx >= 0.0 && lineIdx < idHeight) {
      float localY = (y + scrollY) - lineF * lineHeight;
      float cellW = inHex ? (hexWidth / bytesPerLine) : (asciiWidth / bytesPerLine);
      float localX = inHex ? (x - hexStart) : (x - asciiStart);
      if (cellW > 0.0 && localX >= 0.0) {
        float byteF = floor(localX / cellW);
        if (byteF >= 0.0 && byteF < bytesPerLine) {
          float byteIdx = byteF;
          localX -= byteF * cellW;

          vec2 texSize = vec2(bytesPerLine, idHeight);
          vec2 uv = (vec2(byteIdx + 0.5, lineIdx + 0.5) / texSize);
          vec2 rg = texture(itemIdTex, uv).rg * 255.0 + 0.5;
          float id = rg.r + rg.g * 256.0;
          if (id < 0.5) {
            outlineMask = 0.0;
          } else {

          float edgePx = 2.0 / dpr;
          float leftEdge = step(localX, edgePx);
          float rightEdge = step(cellW - edgePx, localX);
          float topEdge = step(localY, edgePx);
          float bottomEdge = step(lineHeight - edgePx, localY);


          float leftId = id;
          if (byteIdx > 0.0) {
            vec2 luv = (vec2(byteIdx - 1.0 + 0.5, lineIdx + 0.5) / texSize);
            vec2 lrg = texture(itemIdTex, luv).rg * 255.0 + 0.5;
            leftId = lrg.r + lrg.g * 256.0;
          }
          float rightId = id;
          if (byteIdx < bytesPerLine - 1.0) {
            vec2 ruv = (vec2(byteIdx + 1.0 + 0.5, lineIdx + 0.5) / texSize);
            vec2 rrg = texture(itemIdTex, ruv).rg * 255.0 + 0.5;
            rightId = rrg.r + rrg.g * 256.0;
          }
          float upId = id;
          if (lineIdx > 0.0) {
            vec2 uuv = (vec2(byteIdx + 0.5, lineIdx - 1.0 + 0.5) / texSize);
            vec2 urg = texture(itemIdTex, uuv).rg * 255.0 + 0.5;
            upId = urg.r + urg.g * 256.0;
          }
          float downId = id;
          if (lineIdx < idHeight - 1.0) {
            vec2 duv = (vec2(byteIdx + 0.5, lineIdx + 1.0 + 0.5) / texSize);
            vec2 drg = texture(itemIdTex, duv).rg * 255.0 + 0.5;
            downId = drg.r + drg.g * 256.0;
          }

          float drawLeft = (leftId < 0.5 || id < leftId) ? 1.0 : 0.0;
          float drawRight = (rightId < 0.5 || id < rightId) ? 1.0 : 0.0;
          float drawUp = (upId < 0.5 || id < upId) ? 1.0 : 0.0;
          float drawDown = (downId < 0.5 || id < downId) ? 1.0 : 0.0;
          float edge =
              leftEdge * drawLeft * step(0.5, abs(id - leftId)) +
              rightEdge * drawRight * step(0.5, abs(id - rightId)) +
              topEdge * drawUp * step(0.5, abs(id - upId)) +
              bottomEdge * drawDown * step(0.5, abs(id - downId));
          outlineMask = clamp(edge, 0.0, 1.0);
          }
        }
      }
    }
  }

  vec3 withOutline = mix(dimmed, outlineColor.rgb, outlineMask * outlineAlpha);
  vec3 restored = mix(withOutline, base.rgb, highlight);

  vec2 shadowUv = vUv + shadowOffset;
  vec4 edgeShadow = texture(edgeTex, shadowUv);
  float selNorm = edgeShadow.r;
  float selHalo = (shadowStrength > 0.0)
      ? (1.0 - smoothstep(0.0, 1.0, selNorm)) * (1.0 - sel)
      : 0.0;

  float shadowAlpha = selHalo * shadowStrength * shadowColor.a;
  vec3 withShadow = mix(restored, shadowColor.rgb, shadowAlpha);

  vec4 edgeGlow = texture(edgeTex, vUv);
  float playNormActive = edgeGlow.g;
  float playNormFade = edgeGlow.b;
  float playMaskAny = max(playActiveMask, playFadeMask);
  float activeHalo = (1.0 - smoothstep(0.0, 1.0, playNormActive)) * (1.0 - playActiveMask);
  float fadeHalo = (1.0 - smoothstep(0.0, 1.0, playNormFade)) *
                   (1.0 - playMaskAny) * playFadeAlpha;
  float playHalo = max(activeHalo, fadeHalo);

  vec2 p = vUv * viewSize * 0.055;
  float t = time * 0.85;

  float n1 = noise2(p + vec2(0.0, t * 1.2));
  float n2 = noise2(p * 1.7 + vec2(7.4, t * 1.6));
  float n3 = noise2(p * 2.9 + vec2(-3.1, t * 2.2));

  float flicker = mix(n1, n2, 0.6);
  float lick = mix(n2, n3, 0.5);
  float turbulence = 0.65 + 0.55 * mix(flicker, lick, 0.5);

  float flame = clamp(playHalo * glowStrength * turbulence, 0.0, 1.0);

  float flameRamp = smoothstep(0.0, 1.0, flame);
  vec3 flameColor = mix(glowLow, glowHigh, flameRamp);

  vec3 withGlow = mix(withShadow, flameColor, clamp(flame, 0.0, 1.0));
  withGlow = clamp(withGlow + flameColor * flame * 0.35, 0.0, 1.0);

  fragColor = vec4(withGlow, base.a);
}
