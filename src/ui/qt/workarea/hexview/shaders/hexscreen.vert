#version 450

layout(location = 0) in vec2 inPos;
layout(location = 0) out vec2 vUv;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 overlayAndShadow;   // x=overlayOpacity
  vec4 columnLayout;       // x=hexStart, y=hexWidth, z=asciiStart, w=asciiWidth
  vec4 viewInfo;           // x=viewWidth, y=viewHeight, z=flipY
};

void main() {
  vec2 pos = inPos * 2.0 - 1.0;
  gl_Position = mvp * vec4(pos, 0.0, 1.0);

  float flipY = viewInfo.z; // 0 = no flip (OpenGL), 1 = flip (Metal/D3D/Vulkan)
  vUv = vec2(inPos.x, mix(inPos.y, 1.0 - inPos.y, flipY));
}
