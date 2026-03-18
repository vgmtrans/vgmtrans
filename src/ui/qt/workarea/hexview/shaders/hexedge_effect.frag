#version 450

layout(location = 0) in vec2 vPos;
layout(location = 1) in vec4 vRect;
layout(location = 2) in vec4 vFlags;
layout(location = 3) in vec4 vTint;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 edgeParams; // x = shadowRadius, y = glowRadius, z = shadowCurve, w = glowCurve
};

layout(location = 0) out vec4 maskField;
layout(location = 1) out vec4 edgeField;
layout(location = 2) out vec4 playbackColorField;

float sdRect(vec2 p, vec2 center, vec2 halfSize) {
  vec2 d = abs(p - center) - halfSize;
  vec2 dOut = max(d, vec2(0.0));
  float outside = length(dOut);
  float inside = min(max(d.x, d.y), 0.0);
  return outside + inside;
}

void main() {
  float shadowRadius = edgeParams.x;
  float glowRadius = edgeParams.y;
  float shadowCurve = max(edgeParams.z, 0.0001);
  float glowCurve = max(edgeParams.w, 0.0001);
  float isShadow = step(0.5, vFlags.r);
  float isGlowActive = step(0.5, vFlags.g);
  float isGlowFade = step(0.5, vFlags.b);

  vec2 center = vRect.xy + vRect.zw * 0.5;
  vec2 halfSize = vRect.zw * 0.5;
  float dist = sdRect(vPos, center, halfSize);
  float outside = max(dist, 0.0);

  float shadowNorm = shadowRadius > 0.0001
      ? clamp(pow(outside / shadowRadius, shadowCurve), 0.0, 1.0)
      : 1.0;
  float glowNorm = glowRadius > 0.0001 ? clamp(pow(outside / glowRadius, glowCurve), 0.0, 1.0)
                                       : 1.0;

  float shadowHalo = 1.0 - smoothstep(0.0, 1.0, shadowNorm);
  float glowHalo = 1.0 - smoothstep(0.0, 1.0, glowNorm);

  maskField = vec4(0.0);
  edgeField = vec4(shadowHalo * isShadow,
                   glowHalo * isGlowActive,
                   glowHalo * isGlowFade,
                   0.0);
  playbackColorField = vec4(vTint.rgb, vTint.a * max(isGlowActive, isGlowFade));
}
