#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inRect;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vPos;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 p0; // scrollY, overlayAlpha, lineH, charW
  vec4 p1; // originX, strideX, bytesPerLine, unused
  ivec4 sel; // startLine, startCol, endLine, endCol
};

void main() {
  vec2 pos = inRect.xy + inPos * inRect.zw;
  gl_Position = mvp * vec4(pos, 0.0, 1.0);
  vPos = pos;
  vColor = inColor;
}
