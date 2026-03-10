#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vLocalPos;
layout(location = 2) in vec2 vRectSize;
layout(location = 3) in vec4 vParams;
layout(location = 4) in vec2 vScenePos;
layout(location = 5) in vec2 vGlyphUv;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Ubuf {
  mat4 mvp;
  vec4 camera;
  vec4 noteArea;
  vec4 noteBorderColor;
  vec4 glowConfig;
};

layout(binding = 1) uniform sampler2D glyphAtlas;

float sdRoundBox(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + vec2(r);
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float noteCornerRadius(vec2 size) {
  return max(0.0, ((0.5 * min(size.x, size.y)) - 0.25) * 0.5);
}

void main() {
  if (vParams.x > 7.5) {
    if (vScenePos.x < noteArea.x || vScenePos.x > noteArea.z ||
        vScenePos.y < noteArea.y || vScenePos.y > noteArea.w) {
      discard;
    }

    const float auraPadPx = 72.0;
    const float auraFalloffPx = 13.0;

    float noteW = max(1.0, vRectSize.x - (2.0 * auraPadPx));
    float noteH = max(1.0, vRectSize.y - (2.0 * auraPadPx));
    vec2 notePx = (vLocalPos * vRectSize) - vec2(auraPadPx);
    vec2 noteHalf = 0.5 * vec2(noteW, noteH);
    float boxDist = sdRoundBox(notePx - noteHalf, noteHalf, noteCornerRadius(vec2(noteW, noteH)));
    float outsideMask = smoothstep(0.0, 1.2, boxDist);
    float auraDist = max(boxDist, 0.0);
    float auraSoft = pow(exp(-auraDist / auraFalloffPx), 1.05);
    float auraWide = exp(-auraDist / 34.0);
    // Force alpha/color to taper out well before the padded quad edge.
    float auraGate = 1.0 - smoothstep(auraPadPx * 0.60, auraPadPx * 0.88, auraDist);
    float auraAlpha = outsideMask * auraGate * ((0.54 * auraSoft) + (0.22 * auraWide));
    if (auraAlpha <= 0.002) {
      discard;
    }

    if (glowConfig.x > 0.5) {
      // Light mode: emit an outside-only chromatic aura so glow stays visible
      // without turning overlapping colors into a gray haze.
      float auraWideLight = exp(-auraDist / 2.0);
      float lightRadiusGate = 1.0 - smoothstep(5.0, 15.0, auraDist);
      float auraOutsideLight =
          outsideMask * ((0.46 * auraSoft) + (0.06 * auraWideLight)) * auraGate * lightRadiusGate;

      vec3 lightHue = clamp(vColor.rgb * 1.10, 0.0, 1.0);
      float lum = dot(lightHue, vec3(0.2126, 0.7152, 0.0722));
      lightHue = clamp((lightHue - vec3(lum)) * 1.18 + vec3(lum), 0.0, 1.0);

      float glowAlpha = clamp(auraOutsideLight * 0.31, 0.0, 0.22);
      if (glowAlpha <= 0.002) {
        discard;
      }
      fragColor = vec4(lightHue, glowAlpha);
    } else {
      // Dark mode keeps screen-blended premultiplied glow.
      const float laserStrength = 0.68;
      float glowAlpha = clamp(auraAlpha * 0.52, 0.0, 1.0) * laserStrength;
      if (glowAlpha <= 0.002) {
        discard;
      }
      vec3 glowBase = mix(vColor.rgb, vec3(1.0), 0.24);
      vec3 glowRgb = min(glowBase * auraAlpha, vec3(1.35)) * glowAlpha;
      fragColor = vec4(glowRgb, glowAlpha);
    }
    return;
  } else if (vParams.x > 6.5) {
    // Sample glyph alpha from the shared label atlas.
    float alpha = texture(glyphAtlas, vGlyphUv).a;
    if (alpha <= 0.001) {
      discard;
    }
    fragColor = vec4(vColor.rgb, vColor.a * alpha);
    return;
  } else if (vParams.x > 5.5) {
    vec4 grad = vColor;
    float leftScale = max(0.0, vParams.y);
    float rightScale = max(0.0, vParams.z);
    float t = clamp(vLocalPos.x, 0.0, 1.0);
    float curve = max(1.0, vParams.w);
    float ramp = pow(t, curve);
    float scale = mix(leftScale, rightScale, ramp);
    grad.rgb = clamp(grad.rgb * scale, 0.0, 1.0);
    grad.a = 1.0;
    fragColor = grad;
    return;
  } else if (vParams.x > 4.5) {
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
