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
  vec4 shadowColor = p3;

  float x = vUv.x * viewSize.x;
  bool inHex = (x >= hexStart) && (x < hexStart + hexWidth);
  bool inAscii = (asciiWidth > 0.0) && (x >= asciiStart) && (x < asciiStart + asciiWidth);
  float inColumns = (inHex || inAscii) ? 1.0 : 0.0;

  vec3 dimmed = mix(base.rgb, vec3(0.0), overlayOpacity * inColumns);

  float sel = texture(maskTex, vUv).r;
  vec3 restored = mix(dimmed, base.rgb, sel);

  vec2 shadowUv = vUv + shadowOffset;
  float sh = texture(shadowTex, shadowUv).r;
  // remove the solid interior contribution; keep only the halo
  float halo = max(sh - sel, 0.0);
  float shadowAlpha = halo * shadowStrength * shadowColor.a;
  vec3 withShadow = mix(restored, shadowColor.rgb, shadowAlpha);

  fragColor = vec4(withShadow, base.a);
}
