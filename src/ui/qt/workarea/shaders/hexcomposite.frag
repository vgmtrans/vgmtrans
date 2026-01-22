#version 450

layout(location = 0) in vec2 vUv;

layout(binding = 1) uniform sampler2D contentTex;
layout(binding = 2) uniform sampler2D maskTex;
layout(binding = 3) uniform sampler2D shadowTex;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 p0;
  vec4 p1;
  vec4 p2;
  vec4 p3;
  vec4 p4;
};

layout(location = 0) out vec4 fragColor;

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
  float time = p2.w;
  vec4 shadowColor = p3;
  vec3 glowColor = p4.rgb;
  float glowStrength = p4.a;

  float x = vUv.x * viewSize.x;
  bool inHex = (x >= hexStart) && (x < hexStart + hexWidth);
  bool inAscii = (asciiWidth > 0.0) && (x >= asciiStart) && (x < asciiStart + asciiWidth);
  float inColumns = (inHex || inAscii) ? 1.0 : 0.0;

  vec3 dimmed = mix(base.rgb, vec3(0.0), overlayOpacity * inColumns);

  vec4 mask = texture(maskTex, vUv);
  float sel = mask.r;
  float play = mask.g;
  float highlight = max(sel, play);
  vec3 restored = mix(dimmed, base.rgb, highlight);

  vec2 shadowUv = vUv + shadowOffset;
  vec4 sh = texture(shadowTex, shadowUv);
  // remove the solid interior contribution; keep only the halo
  float selHalo = max(sh.r - sel, 0.0);
  // curve it: >1 make shadow punchier near edges
  // halo = pow(clamp(halo, 0.0, 1.0), 0.8); // 0.6..0.8 is a good range

  float shadowAlpha = selHalo * shadowStrength * shadowColor.a;
  vec3 withShadow = mix(restored, shadowColor.rgb, shadowAlpha);

  float playHalo = max(sh.g - play, 0.0);

  float noise = fract(sin(dot(vUv * viewSize * 0.12 + time, vec2(12.9898, 78.233))) * 43758.5453);
  float flicker = (0.7 + 0.3 * sin(time * 6.0 + (vUv.x + vUv.y) * 30.0)) * (0.9 + 0.2 * noise);
  float waveX = sin((vUv.x * viewSize.x) * 0.08 - time * 3.4);
  float waveY = sin((vUv.y * viewSize.y) * 0.10 + time * 4.1);
  float undulate = 0.85 + 0.15 * (waveX * waveY);
  float glowAlpha = clamp(playHalo * glowStrength * flicker * undulate, 0.0, 1.0);

  vec3 withGlow = clamp(withShadow + glowColor * glowAlpha, 0.0, 1.0);

  fragColor = vec4(withGlow, base.a);
}
