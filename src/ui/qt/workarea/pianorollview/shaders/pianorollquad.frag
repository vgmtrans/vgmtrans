#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vLocalPos;
layout(location = 2) in vec2 vRectSize;
layout(location = 3) in vec4 vParams;

layout(location = 0) out vec4 fragColor;

void main() {
  if (vParams.x > 0.5) {
    float segment = max(1.0, vParams.y);
    float gap = max(0.0, vParams.z);
    float cycle = segment + gap;
    if (cycle > 0.0) {
      float localY = vLocalPos.y * vRectSize.y + vParams.w;
      float m = mod(localY, cycle);
      if (m > segment) {
        discard;
      }
    }
  }

  fragColor = vColor;
}
