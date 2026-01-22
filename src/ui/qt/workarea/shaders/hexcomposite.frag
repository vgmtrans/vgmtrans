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
  float sel = clamp(mask.r, 0.0, 1.0);
  float play = clamp(mask.g, 0.0, 1.0);
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

  vec2 p = vUv * viewSize;
  float t = time * 1.4;
  vec2 warp = vec2(
      sin(p.y * 0.05 + t * 2.7) + sin(p.x * 0.02 - t * 1.3),
      sin(p.x * 0.04 - t * 2.1) + sin(p.y * 0.03 + t * 1.7)
    ) * 2.0;
  vec2 fp = p + warp;

  float n1 = fract(sin(dot(fp * 0.12 + t, vec2(12.9898, 78.233))) * 43758.5453);
  float n2 = fract(sin(dot(fp * 0.07 - t, vec2(93.9898, 67.345))) * 24634.6345);
  float noise = mix(n1, n2, 0.55);

  float w1 = sin(fp.x * 0.08 + t * 5.0);
  float w2 = sin(fp.y * 0.10 - t * 4.3);
  float lick = 0.7 + 0.3 * (w1 * w2);
  float flicker = 0.6 + 0.4 * noise;

  float halo = pow(playHalo, 0.65);
  float flame = clamp(halo * glowStrength * flicker * lick, 0.0, 1.0);

  vec3 fireDeep = vec3(0.7, 0.08, 0.02);
  vec3 fireMid = vec3(1.0, 0.35, 0.05);
  vec3 fireHot = vec3(1.0, 0.75, 0.15);
  vec3 fireCore = vec3(1.0, 0.95, 0.8);

  float t1 = smoothstep(0.0, 0.45, flame);
  float t2 = smoothstep(0.35, 0.75, flame);
  float t3 = smoothstep(0.65, 1.0, flame);
  vec3 flameColor = mix(fireDeep, fireMid, t1);
  flameColor = mix(flameColor, fireHot, t2);
  flameColor = mix(flameColor, fireCore, t3);
  flameColor = mix(flameColor, glowColor, 0.15);

  vec3 withGlow = clamp(withShadow + flameColor * flame, 0.0, 1.0);

  fragColor = vec4(withGlow, base.a);
}
