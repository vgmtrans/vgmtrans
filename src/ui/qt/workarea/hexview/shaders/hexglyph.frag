#version 450

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;

layout(binding = 1) uniform sampler2D glyphTex;

layout(location = 0) out vec4 fragColor;

void main() {
  float alpha = texture(glyphTex, vUv).a;
  fragColor = vec4(vColor.rgb, vColor.a * alpha);
}
