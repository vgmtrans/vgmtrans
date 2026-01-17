#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vPos;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 p0; // scrollY, overlayAlpha, lineH, charW
  vec4 p1; // originX, strideX, bytesPerLine, unused
  ivec4 sel; // startLine, startCol, endLine, endCol
};

bool inSelection(int line, int col) {
  if (sel.x < 0 || sel.z < 0) {
    return false;
  }
  if (line < sel.x || line > sel.z) {
    return false;
  }
  if (line == sel.x && line == sel.z) {
    return col >= sel.y && col < sel.w;
  }
  if (line == sel.x) {
    return col >= sel.y;
  }
  if (line == sel.z) {
    return col < sel.w;
  }
  return true;
}

void main() {
  float scrollY = p0.x;
  float overlayAlpha = p0.y;
  float lineHeight = p0.z;
  float originX = p1.x;
  float strideX = p1.y;
  int bytesPerLine = int(p1.z + 0.5);

  float docY = vPos.y + scrollY;
  int line = int(floor(docY / lineHeight));

  float xRel = vPos.x - originX;
  int col = int(floor(xRel / strideX));

  float alpha = overlayAlpha * vColor.a;
  if (col >= 0 && col < bytesPerLine && inSelection(line, col)) {
    alpha = 0.0;
  }

  fragColor = vec4(vColor.rgb, alpha);
}
