#version 450

layout(location = 0) in vec2 vUv;

layout(binding = 1) uniform sampler2D contentTex;
layout(binding = 2) uniform sampler2D maskTex;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 overlayAndShadow;   // x=overlayOpacity
  vec4 columnLayout;       // x=hexStart, y=hexWidth, z=asciiStart, w=asciiWidth
  vec4 viewInfo;           // x=viewWidth, y=viewHeight, z=flipY
};

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 base = texture(contentTex, vUv);
  float selected = clamp(texture(maskTex, vUv).r, 0.0, 1.0);

  float x = vUv.x * viewInfo.x;
  bool inHex = (x >= columnLayout.x) && (x < columnLayout.x + columnLayout.y);
  bool inAscii = (columnLayout.w > 0.0) && (x >= columnLayout.z) && (x < columnLayout.z + columnLayout.w);
  float inColumns = (inHex || inAscii) ? 1.0 : 0.0;

  vec3 dimmed = mix(base.rgb, vec3(0.0), overlayAndShadow.x * inColumns);
  vec3 restored = mix(dimmed, base.rgb, selected);
  fragColor = vec4(restored, base.a);
}
