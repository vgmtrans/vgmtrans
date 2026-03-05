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

float hash12(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float valueNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);

  float a = hash12(i);
  float b = hash12(i + vec2(1.0, 0.0));
  float c = hash12(i + vec2(0.0, 1.0));
  float d = hash12(i + vec2(1.0, 1.0));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float sdBox(vec2 p, vec2 b) {
  vec2 d = abs(p) - b;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

void main() {
  if (vParams.x > 8.5) {
    if (vScenePos.x < noteArea.x || vScenePos.x > noteArea.z ||
        vScenePos.y < noteArea.y || vScenePos.y > noteArea.w) {
      discard;
    }

    float noteW = max(1.0, vRectSize.x);
    float noteH = max(1.0, vRectSize.y);
    vec2 notePx = vLocalPos * vRectSize;
    vec2 local = clamp(notePx / vec2(noteW, noteH), 0.0, 1.0);

    vec3 surface = vColor.rgb;
    float t = max(0.0, vParams.z);
    float seed = vParams.w;

    surface *= 0.95 + 0.05 * sin((local.y * 16.0) + (t * 1.8));
    surface *= 0.94 + 0.06 * (1.0 - abs((local.y * 2.0) - 1.0));

    float edgeDist = min(min(local.x, 1.0 - local.x), min(local.y, 1.0 - local.y));
    float edge = 1.0 - smoothstep(0.0, 0.10, edgeDist);
    surface *= 1.0 - (edge * 0.09);

    float pulse = 0.55 + 0.45 * sin((t * 10.0) + (seed * 0.35) + (local.y * 10.0));
    surface += (0.012 + (0.022 * pulse)) * vColor.rgb;

    float seamLocalX = vParams.y;
    float seamActive = step(0.0, seamLocalX) * step(seamLocalX, 1.0);
    float seamGlow = 0.0;
    float seamCore = 0.0;
    float lick = 0.0;
    if (seamActive > 0.0) {
      float seamX = seamLocalX * noteW;
      float dx = abs(notePx.x - seamX);
      seamGlow = smoothstep(18.0, 0.0, dx);
      seamCore = smoothstep(2.6, 0.0, dx);
      lick = valueNoise(vec2((local.y * 24.0) + seed, (t * 12.0) + (seed * 0.63)));
    }
    vec3 seamTint = mix(vColor.rgb, vec3(1.0), 0.42);

    surface = mix(surface, (surface * 0.72) + (seamTint * 0.78), seamGlow * 0.46);
    surface += seamTint * seamCore * 0.72;
    surface += (vColor.rgb * (0.20 + (0.18 * lick))) * seamGlow * 0.30;

    fragColor = vec4(min(surface, vec3(1.55)), 1.0);
    return;
  } else if (vParams.x > 7.5) {
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
    float boxDist = sdBox(notePx - noteHalf, noteHalf);
    vec2 local = clamp(notePx / vec2(noteW, noteH), 0.0, 1.0);

    float rectMask = 1.0 - smoothstep(0.0, 1.2, boxDist);
    vec3 surface = vColor.rgb;
    float t = max(0.0, vParams.z);
    float seed = vParams.w;

    // Base note look with subtle texture so seam effects stay readable.
    surface *= 0.94 + 0.06 * sin((local.y * 16.0) + (t * 1.8));
    surface *= 0.93 + 0.07 * (1.0 - abs((local.y * 2.0) - 1.0));

    float edgeDist = min(min(local.x, 1.0 - local.x), min(local.y, 1.0 - local.y));
    float edge = 1.0 - smoothstep(0.0, 0.10, edgeDist);
    surface *= 1.0 - (edge * 0.10);

    float pulse = 0.55 + 0.45 * sin((t * 10.0) + (seed * 0.35) + (local.y * 10.0));
    surface += (0.015 + (0.030 * pulse)) * vColor.rgb;

    float seamLocalX = vParams.y;
    float seamActive = step(0.0, seamLocalX) * step(seamLocalX, 1.0);
    float seamX = 0.0;
    float dx = 0.0;
    float seamGlow = 0.0;
    float seamCore = 0.0;
    float lick = 0.0;
    float flame = 0.0;
    if (seamActive > 0.0) {
      seamX = seamLocalX * noteW;
      dx = abs(notePx.x - seamX);
      seamGlow = smoothstep(20.0, 0.0, dx);
      seamCore = smoothstep(2.6, 0.0, dx);
      lick = valueNoise(vec2((local.y * 24.0) + seed, (t * 12.0) + (seed * 0.63)));
      flame = seamGlow * (0.50 + (0.50 * lick));
    }
    vec3 seamTint = mix(vColor.rgb, vec3(1.0), 0.45);

    surface = mix(surface, (surface * 0.60) + (seamTint * 0.95), seamGlow * 0.62);
    surface += seamTint * seamCore * 0.95;
    surface += (vColor.rgb * (0.30 + (0.25 * lick))) * flame * 0.55;

    if (seamActive > 0.0) {
      float behind = smoothstep(0.0, 16.0, seamX - notePx.x);
      float scorch = behind * smoothstep(20.0, 0.0, dx);
      surface = mix(surface, surface * vec3(0.24, 0.20, 0.18), scorch * 0.95);
    }

    float auraDist = max(boxDist, 0.0);
    float aura = exp(-auraDist / auraFalloffPx);
    float auraSoft = pow(aura, 1.05);
    float auraWide = exp(-auraDist / 42.0);
    // Force alpha/color to taper out well before the padded quad edge.
    float auraGate = 1.0 - smoothstep(auraPadPx * 0.68, auraPadPx * 0.96, auraDist);
    float outsideMask = clamp(1.0 - rectMask, 0.0, 1.0);
    float auraOutside = outsideMask * ((0.62 * auraSoft) + (0.34 * auraWide)) * auraGate;
    float auraInside = (1.0 - outsideMask) * ((0.07 * auraSoft) + (0.03 * auraWide));
    vec3 auraTint = mix(vColor.rgb, vec3(1.0), 0.30);
    vec3 auraCol = (auraTint * auraOutside) + (vColor.rgb * auraInside);
    if (seamActive > 0.0) {
      float seamBloom = smoothstep(30.0, 0.0, dx) *
                        smoothstep(noteH * 0.95, 0.0, abs(notePx.y - (0.5 * noteH)));
      auraCol += seamTint * seamBloom * 0.12;
    }

    vec3 outRgb = (surface * rectMask) + auraCol;
    float auraAlpha = clamp((auraOutside * 0.72) + (auraInside * 0.14), 0.0, 1.0);
    float outAlpha = clamp(max(rectMask, auraAlpha), 0.0, 1.0);
    if (outAlpha <= 0.002) {
      discard;
    }

    if (glowConfig.x > 0.5) {
      // Light mode: emit an outside-only chromatic aura so glow stays visible
      // without turning overlapping colors into a gray haze.
      float auraWideLight = exp(-auraDist / 2.0);
      float lightRadiusGate = 1.0 - smoothstep(5.0, 15.0, auraDist); //smoothstep(20.0, 44.0, auraDist);
      float auraOutsideLight = outsideMask * ((0.58 * auraSoft) + (0.10 * auraWideLight)) * auraGate * lightRadiusGate;
      float seamHalo = 0.0;
      if (seamActive > 0.0) {
        seamHalo = smoothstep(18.0, 0.0, dx) *
                   smoothstep(noteH * 0.90, 0.0, abs(notePx.y - (0.5 * noteH)));
      }

      vec3 lightHue = clamp(vColor.rgb * (1.10 + (0.12 * seamGlow)), 0.0, 1.0);
      float lum = dot(lightHue, vec3(0.2126, 0.7152, 0.0722));
      lightHue = clamp((lightHue - vec3(lum)) * 1.18 + vec3(lum), 0.0, 1.0);

      float glowAlpha = clamp((auraOutsideLight * 0.42) + (seamHalo * 0.10), 0.0, 0.30);
      if (glowAlpha <= 0.002) {
        discard;
      }
      fragColor = vec4(lightHue, glowAlpha);
    } else {
      // Dark mode keeps screen-blended premultiplied glow.
      const float laserStrength = 0.68;
      float glowAlpha = outAlpha * laserStrength;
      vec3 glowRgb = min(outRgb, vec3(1.35)) * glowAlpha;
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
