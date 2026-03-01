#version 450

layout(location = 0) in vec4 vFillColor;
layout(location = 1) in vec2 vLocalPos;
layout(location = 2) in vec2 vRectSize;
layout(location = 3) in vec2 vScenePos;
layout(location = 4) in float vBorderEnabled;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 camera;
  vec4 noteArea;
  vec4 noteBorderColor;
};

void main() {
  if (vScenePos.x < noteArea.x || vScenePos.x > noteArea.z ||
      vScenePos.y < noteArea.y || vScenePos.y > noteArea.w) {
    discard;
  }

  vec4 color = vFillColor;
  if (vBorderEnabled > 0.5) {
    float edgeNormY = min(0.5, 1.0 / max(1.0, vRectSize.y));
    if (vLocalPos.y <= edgeNormY || vLocalPos.y >= (1.0 - edgeNormY)) {
      color = noteBorderColor;
    }
  }

  fragColor = color;
}
