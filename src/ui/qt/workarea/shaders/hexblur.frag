#version 450

layout(location = 0) in vec2 vUv;

layout(binding = 1) uniform sampler2D srcTex;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 p0;
  vec4 p1;
  vec4 p2;
  vec4 p3;
};

layout(location = 0) out vec4 fragColor;

void main() {
  vec2 texelStep = p0.xy;
  float radius = max(p0.z, 0.0);
  if (radius <= 0.0001) {
    float v = texture(srcTex, vUv).r;
    fragColor = vec4(v, v, v, v);
    return;
  }

  vec2 step = texelStep * radius;
  float w0 = 0.227027;
  float w1 = 0.316216;
  float w2 = 0.070270;
  float sum = texture(srcTex, vUv).r * w0;
  sum += texture(srcTex, vUv + step).r * w1;
  sum += texture(srcTex, vUv - step).r * w1;
  sum += texture(srcTex, vUv + step * 2.0).r * w2;
  sum += texture(srcTex, vUv - step * 2.0).r * w2;
  fragColor = vec4(sum, sum, sum, sum);
}
