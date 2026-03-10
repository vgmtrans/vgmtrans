#version 450

layout(location = 0) in vec4 vFillColor;
layout(location = 1) in vec2 vLocalPos;
layout(location = 2) in vec2 vRectSize;
layout(location = 3) in vec2 vScenePos;
layout(location = 4) in float vBorderEnabled;
layout(location = 5) in vec2 vNoteState;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 camera;
  vec4 noteArea;
  vec4 noteBorderColor;
  vec4 glowConfig;
};

float sdRoundBox(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + vec2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float noteCornerRadius(vec2 size) {
  return max(0.0, ((0.5 * min(size.x, size.y)) - 0.25) * 0.5);
}

float activeFlowBand(vec2 local, float t, float seed) {
  float warp = sin((local.x * 3.2) + (local.y * 1.4) - (t * 0.45) + (seed * 0.7));
  warp += 0.5 * sin((local.x * 6.1) - (t * 0.85) + seed);
  return sin((local.y * 13.0) - (t * 4.0) + (warp * 2.2) + seed);
}

void main() {
  if (vScenePos.x < noteArea.x || vScenePos.x > noteArea.z ||
      vScenePos.y < noteArea.y || vScenePos.y > noteArea.w) {
    discard;
  }

  float noteW = max(1.0, vRectSize.x);
  float noteH = max(1.0, vRectSize.y);
  vec2 noteSize = vec2(noteW, noteH);
  vec2 notePx = vLocalPos * vRectSize;
  vec2 noteHalf = 0.5 * noteSize;
  vec2 local = clamp(notePx / noteSize, 0.0, 1.0);
  float outerRadius = noteCornerRadius(noteSize);
  float outerDist = sdRoundBox(notePx - noteHalf, noteHalf, outerRadius);
  float aa = max(fwidth(outerDist), 0.60);
  float shapeMask = 1.0 - smoothstep(-aa, aa, outerDist);
  float edgeDistPx = max(0.0, -outerDist);
  if (shapeMask <= 0.001) {
    discard;
  }

  vec4 color = vFillColor;
  if (vNoteState.x > 0.5) {
    float t = max(0.0, glowConfig.y);
    float seed = vNoteState.y;
    vec3 surface = color.rgb;
    float centerBand = 1.0 - abs((local.y * 2.0) - 1.0);
    float baseLuma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    float darkBoost = 1.0 - smoothstep(0.18, 0.62, baseLuma);
    float bodyLift = 0.36 + (0.34 * centerBand) + (0.16 * darkBoost);

    surface *= 0.88 + 0.12 * sin((local.y * 16.0) + (t * 1.8));
    surface *= 0.89 + (0.24 * centerBand);

    float edge = 1.0 - smoothstep(0.0, 2.0, max(0.0, -outerDist));
    surface *= 1.0 - (edge * 0.36);

    float wispBand = activeFlowBand(local, t, seed);
    float wispLight = smoothstep(-0.55, 0.82, wispBand);
    float wispShadow = smoothstep(0.30, -0.90, wispBand);
    vec3 wispTint = mix(color.rgb, vec3(1.0), 0.14 + (0.26 * darkBoost));
    surface = mix(surface, wispTint, (0.12 + (0.16 * darkBoost)) * wispLight);
    surface *= 1.0 - ((0.14 + (0.18 * darkBoost)) * wispShadow);

    float pulse = 0.55 + 0.45 * sin((t * 10.0) + (seed * 0.35) + (local.y * 10.0));
    surface += (0.060 + (0.090 * pulse)) * mix(color.rgb, vec3(1.0), 0.22 + (0.28 * darkBoost));
    vec3 highlightTint = mix(color.rgb, vec3(1.0), 0.48 + (0.30 * darkBoost));
    surface = mix(surface, highlightTint, bodyLift);

    color.rgb = min(surface, vec3(1.0));
    color.a = min(1.0, color.a + 0.28 + (0.09 * darkBoost));
  }

  float borderMask = 0.0;
  if (vBorderEnabled > 0.5) {
    float borderPx = min(0.55, 0.30 * min(noteW, noteH));
    float borderAa = max(fwidth(outerDist), 0.45);
    float borderOuter = 1.0 - smoothstep(-borderAa, borderAa, outerDist);
    float borderInner = 1.0 - smoothstep(max(0.0, borderPx - borderAa), borderPx + borderAa, edgeDistPx);
    borderMask = borderOuter * borderInner;
  }

  color.rgb = mix(color.rgb, noteBorderColor.rgb, borderMask * noteBorderColor.a);
  color.a *= shapeMask;
  fragColor = color;
}
