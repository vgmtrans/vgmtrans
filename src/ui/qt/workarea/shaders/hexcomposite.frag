#version 450

layout(location = 0) in vec2 vUv;

layout(binding = 1) uniform sampler2D contentTex;
layout(binding = 2) uniform sampler2D maskTex;
layout(binding = 3) uniform sampler2D edgeTex;

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
  float time = p2.w;
  vec4 shadowColor = p3;
  vec3 fireDeep = p4.rgb;
  float glowStrength = p4.a;
  vec3 fireMid = p5.rgb;
  vec3 fireHot = p6.rgb;
  vec3 fireCore = p7.rgb;

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
  vec4 edgeShadow = texture(edgeTex, shadowUv);
  float selNorm = edgeShadow.r;
  float selHalo = (shadowStrength > 0.0)
      ? (1.0 - smoothstep(0.0, 1.0, selNorm)) * (1.0 - sel)
      : 0.0;

  float shadowAlpha = selHalo * shadowStrength * shadowColor.a;
  vec3 withShadow = mix(restored, shadowColor.rgb, shadowAlpha);

  vec4 edgeGlow = texture(edgeTex, vUv);
  float playNorm = edgeGlow.g;
  float playHalo = (1.0 - smoothstep(0.0, 1.0, playNorm)) * (1.0 - play);

  vec2 p = vUv * viewSize * 0.055;
  float t = time * 0.85;

  float n1 = noise2(p + vec2(0.0, t * 1.2));
  float n2 = noise2(p * 1.7 + vec2(7.4, t * 1.6));
  float n3 = noise2(p * 2.9 + vec2(-3.1, t * 2.2));

  float flicker = mix(n1, n2, 0.6);
  float lick = mix(n2, n3, 0.5);
  float turbulence = 0.65 + 0.55 * mix(flicker, lick, 0.5);

  float flame = clamp(playHalo * glowStrength * turbulence, 0.0, 1.0);

  float t1 = smoothstep(0.0, 0.45, flame);
  float t2 = smoothstep(0.35, 0.75, flame);
  float t3 = smoothstep(0.65, 1.0, flame);
  vec3 flameColor = mix(fireDeep, fireMid, t1);
  flameColor = mix(flameColor, fireHot, t2);
  flameColor = mix(flameColor, fireCore, t3);

  vec3 withGlow = mix(withShadow, flameColor, clamp(flame, 0.0, 1.0));
  withGlow = clamp(withGlow + flameColor * flame * 0.35, 0.0, 1.0);

  fragColor = vec4(withGlow, base.a);
}
