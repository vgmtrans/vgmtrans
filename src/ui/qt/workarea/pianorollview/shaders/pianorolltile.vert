#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inRect;
layout(location = 2) in vec4 inUvRect;

layout(location = 0) out vec2 vUv;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 camera;
};

void main() {
  vec2 pos = inRect.xy + inPos * inRect.zw;
  gl_Position = mvp * vec4(pos, 0.0, 1.0);
  vUv = inUvRect.xy + inPos * inUvRect.zw;
}
