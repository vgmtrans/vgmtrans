#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inGeomRect;
layout(location = 2) in vec4 inRect;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 vPos;
layout(location = 1) out vec4 vRect;
layout(location = 2) out vec4 vColor;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 shadowParams;
  vec4 scrollParams;
};

void main() {
  float scrollY = scrollParams.x; // scroll in document space
  vec2 pos = inGeomRect.xy + inPos * inGeomRect.zw;
  pos.y -= scrollY;
  gl_Position = mvp * vec4(pos, 0.0, 1.0);
  vPos = pos;
  vec4 rect = inRect;
  rect.y -= scrollY;
  vRect = rect;
  vColor = inColor;
}
