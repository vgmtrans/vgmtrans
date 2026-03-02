#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vLocalPos;
layout(location = 2) in vec2 vRectSize;
layout(location = 3) in vec4 vParams;
layout(location = 4) in vec2 vScenePos;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 camera;
  vec4 noteArea;
  vec4 noteBorderColor;
};

void main() {
  if (vParams.x > 4.5) {
    vec4 grad = vColor;
    float topScale = max(0.0, vParams.y);
    float bottomScale = max(0.0, vParams.z);
    float t = clamp(vLocalPos.y, 0.0, 1.0);
    float scale = mix(topScale, bottomScale, t);
    grad.rgb = clamp(grad.rgb * scale, 0.0, 1.0);
    grad.a = 1.0;
    fragColor = grad;
    return;
  } else if (vParams.x > 3.5) {
    if (vScenePos.x < noteArea.x || vScenePos.x > noteArea.z) {
      discard;
    }
    float x = clamp(vLocalPos.x, 0.0, 1.0);
    float y = clamp(vLocalPos.y, 0.0, 1.0);
    float halfWidth = 0.5 * (1.0 - y);
    float edgeDist = halfWidth - abs(x - 0.5);
    float aa = max(0.75 * fwidth(edgeDist), 0.001);
    float triAlpha = smoothstep(0.0, aa, edgeDist);
    if (triAlpha <= 0.0) {
      discard;
    }

    float topScale = (vParams.y > 0.0) ? vParams.y : 1.0;
    float bottomScale = (vParams.z > 0.0) ? vParams.z : 1.0;
    float scale = mix(topScale, bottomScale, y);
    vec3 rgb = clamp(vColor.rgb * scale, 0.0, 1.0);
    fragColor = vec4(rgb, triAlpha * vColor.a);
    return;
  } else if (vParams.x > 1.5) {
    if (vScenePos.x < noteArea.x || vScenePos.x > noteArea.z) {
      discard;
    }

    float pixelsPerTick = max(0.0001, camera.z);
    float tick = ((vScenePos.x - noteArea.x) + camera.x) / pixelsPerTick;
    float originTick = max(0.0, vParams.w);
    float beatTicks = max(1.0, vParams.y);
    float measureTicks = max(beatTicks, vParams.z);
    float relTick = max(0.0, tick - originTick);

    float measureMod = mod(relTick, measureTicks);
    float measureDistPx = min(measureMod, measureTicks - measureMod) * pixelsPerTick;
    bool onMeasure = measureDistPx <= 0.5;

    if (vParams.x < 2.5) {
      if (!onMeasure) {
        discard;
      }
    } else {
      float beatMod = mod(relTick, beatTicks);
      float beatDistPx = min(beatMod, beatTicks - beatMod) * pixelsPerTick;
      bool onBeat = beatDistPx <= 0.5;
      if (!onBeat || onMeasure) {
        discard;
      }
    }
  } else if (vParams.x > 0.5) {
    float segment = max(1.0, vParams.y);
    float gap = max(0.0, vParams.z);
    float cycle = segment + gap;
    if (cycle > 0.0) {
      float localY = vLocalPos.y * vRectSize.y + vParams.w;
      float m = mod(localY, cycle);
      if (m > segment) {
        discard;
      }
    }
  }

  fragColor = vColor;
}
