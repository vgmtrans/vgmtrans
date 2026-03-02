#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inRect;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inParams;
layout(location = 4) in vec4 inCoordMode;
layout(location = 5) in vec2 inScrollMul;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vLocalPos;
layout(location = 2) out vec2 vRectSize;
layout(location = 3) out vec4 vParams;
layout(location = 4) out vec2 vScenePos;
layout(location = 5) out vec2 vGlyphUv;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 camera;
  vec4 noteArea;
  vec4 noteBorderColor;
};

void main() {
  vec2 pos;
  vec2 size = inRect.zw;
  if (inParams.x > 6.5) {
    // LabelText instances are already in screen space; coordMode carries UVs.
    pos = inRect.xy + (inPos * size);
    vGlyphUv = mix(inCoordMode.xy, inCoordMode.zw, inPos);
  } else {
    vec2 scroll = vec2(camera.x, camera.y);
    vec2 origin = inRect.xy;
    if (inCoordMode.x > 0.5) {
      origin.x = noteArea.x + (origin.x * camera.z);
    }
    if (inCoordMode.y > 0.5) {
      origin.y = noteArea.y + (origin.y * camera.w);
    }
    if (inCoordMode.z > 0.5) {
      size.x *= camera.z;
    }
    if (inCoordMode.w > 0.5) {
      size.y *= camera.w;
    }
    pos = origin + (inPos * size) - (inScrollMul * scroll);
    vGlyphUv = vec2(0.0);
  }

  gl_Position = mvp * vec4(pos, 0.0, 1.0);
  vColor = inColor;
  vLocalPos = inPos;
  vRectSize = size;
  vParams = inParams;
  vScenePos = pos;
}
