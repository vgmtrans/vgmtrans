#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inRect;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inParams;
layout(location = 4) in vec2 inScrollMul;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vLocalPos;
layout(location = 2) out vec2 vRectSize;
layout(location = 3) out vec4 vParams;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 camera;
};

void main() {
  vec2 scroll = vec2(camera.x, camera.y);
  vec2 pos = inRect.xy + inPos * inRect.zw - (inScrollMul * scroll);
  gl_Position = mvp * vec4(pos, 0.0, 1.0);
  vColor = inColor;
  vLocalPos = inPos;
  vRectSize = inRect.zw;
  vParams = inParams;
}
