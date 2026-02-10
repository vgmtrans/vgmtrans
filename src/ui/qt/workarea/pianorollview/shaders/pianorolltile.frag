#version 450

layout(location = 0) in vec2 vUv;

layout(binding = 1) uniform sampler2D tileTex;

layout(location = 0) out vec4 fragColor;

void main() {
  fragColor = texture(tileTex, vUv);
}
