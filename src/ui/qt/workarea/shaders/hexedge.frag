#version 450

layout(location = 0) in vec2 vPos;
layout(location = 1) in vec4 vRect;
layout(location = 2) in vec4 vColor;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 edgeParams; // x = shadowRadius, y = glowRadius, z = shadowCurve, w = glowCurve
};

layout(location = 0) out vec4 fragColor;

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

  float isShadow = step(0.5, vColor.r);
  float isGlow = step(0.5, vColor.g);

  vec2 center = vRect.xy + vRect.zw * 0.5;
  vec2 halfSize = vRect.zw * 0.5;
  float dist = sdRect(vPos, center, halfSize);
  float outside = max(dist, 0.0);

  float shadowNorm = shadowRadius > 0.0001 ? clamp(pow(outside / shadowRadius, shadowCurve), 0.0, 1.0) : 1.0;
  float glowNorm = glowRadius > 0.0001 ? clamp(pow(outside / glowRadius, glowCurve), 0.0, 1.0) : 1.0;

  float r = mix(1.0, shadowNorm, isShadow);
  float g = mix(1.0, glowNorm, isGlow);
  fragColor = vec4(r, g, 1.0, 1.0);
}
