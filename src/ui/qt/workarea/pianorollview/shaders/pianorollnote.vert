#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inMeta;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inState;

layout(location = 0) out vec4 vFillColor;
layout(location = 1) out vec2 vLocalPos;
layout(location = 2) out vec2 vRectSize;
layout(location = 3) out vec2 vScenePos;
layout(location = 4) out float vBorderEnabled;
layout(location = 5) out vec2 vNoteState;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 camera;
  vec4 noteArea;
  vec4 noteBorderColor;
  vec4 glowConfig;
};

void main() {
  float startTick = inMeta.x;
  float duration = max(1.0, inMeta.y);
  float key = inMeta.z;

  float x = noteArea.x + (startTick * camera.z) - camera.x;
  float y = noteArea.y + ((127.0 - key) * camera.w) - camera.y;
  float w = max(1.0, duration * camera.z);
  float h = max(1.0, camera.w - 1.0);

  vec2 pos = vec2(x, y) + (inPos * vec2(w, h));
  gl_Position = mvp * vec4(pos, 0.0, 1.0);

  vFillColor = inColor;
  vLocalPos = inPos;
  vRectSize = vec2(w, h);
  vScenePos = pos;
  vBorderEnabled = inMeta.w;
  vNoteState = inState;
}
