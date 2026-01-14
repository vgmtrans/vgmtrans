#version 450

layout(location = 0) in vec2 vPos;
layout(location = 1) in vec4 vRect;
layout(location = 2) in vec4 vColor;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 shadowParams;
  vec4 scrollParams;
};

layout(location = 0) out vec4 fragColor;

float sdRoundRect(vec2 p, vec2 center, vec2 halfSize, float radius) {
  vec2 q = abs(p - center) - halfSize + vec2(radius);
  float outside = length(max(q, vec2(0.0)));
  float inside = min(max(q.x, q.y), 0.0);
  return outside + inside - radius;
}

void main() {
  float blur = shadowParams.z;
  float radius = shadowParams.w;

  vec2 center = vRect.xy + vRect.zw * 0.5;
  vec2 halfSize = vRect.zw * 0.5;
  float dist = sdRoundRect(vPos - shadowParams.xy, center, halfSize, radius);

  if (blur <= 0.0001) {
    fragColor = vec4(0.0);
    return;
  }

  float alpha = smoothstep(blur, 0.0, dist);
  alpha *= step(0.0, dist);
  fragColor = vec4(vColor.rgb, vColor.a * alpha);
}
