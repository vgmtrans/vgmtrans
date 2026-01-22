#version 450

layout(location = 0) in vec2 vUv;
layout(binding = 1) uniform sampler2D srcTex;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 p0; // p0.xy = texelStep, p0.z = sigmaPx
  vec4 p1;
  vec4 p2;
  vec4 p3;
  vec4 p4;
  vec4 p5;
  vec4 p6;
  vec4 p7;
};

layout(location = 0) out vec4 fragColor;

float gauss(float x, float invTwoSigma2) {
  return exp(-(x * x) * invTwoSigma2);
}

void main() {
  vec2 texelStep = p0.xy;
  float sigma = max(p0.z, 0.0001);

  // radius in pixels: ~3 sigma covers ~99% of the gaussian
  int radius = int(ceil(3.0 * sigma));

  // Safety clamp: avoids pathological costs if someone sets sigma huge.
  // Tune 48 up/down depending on what "max blur" you want to support.
  const int MAX_RADIUS = 48;
  radius = min(radius, MAX_RADIUS);

  float invTwoSigma2 = 0.5 / (sigma * sigma);

  vec4 center = texture(srcTex, vUv);
  vec4 sum = center;
  float wsum = 1.0;

  // Pair-sample offsets i and i+1 into one sample at a weighted offset.
  // This lets the linear sampler do some of the work.
  for (int i = 1; i <= MAX_RADIUS; i += 2) {
    if (i > radius) break;

    float w1 = gauss(float(i), invTwoSigma2);

    float w2 = 0.0;
    if (i + 1 <= radius) {
      w2 = gauss(float(i + 1), invTwoSigma2);
    }

    float w = w1 + w2;
    float offset = (w2 > 0.0)
      ? (w1 * float(i) + w2 * float(i + 1)) / w
      : float(i);

    vec2 d = texelStep * offset;

    vec4 a = texture(srcTex, vUv + d);
    vec4 b = texture(srcTex, vUv - d);

    sum += (a + b) * w;
    wsum += 2.0 * w;
  }

  vec4 v = sum / wsum;
  fragColor = v;
}
