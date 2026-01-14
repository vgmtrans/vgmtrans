#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inRect;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 vColor;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 params;
};

void main() {
  float scrollY = params.x; // scroll in document space
  vec2 pos = inRect.xy + inPos * inRect.zw;
  pos.y -= scrollY;
  gl_Position = mvp * vec4(pos, 0.0, 1.0);
  vColor = inColor;
}
