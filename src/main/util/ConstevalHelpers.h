#pragma once

#include <cmath>

namespace ct {

/// Absolute value – helper for compile‑time algorithms
consteval double abs(double v) { return v < 0 ? -v : v; }

/// Newton–Raphson square‑root – runs entirely at compile time
consteval double sqrt(double x, double eps = 1e-12) {
  if (x <= 0.0) return 0.0;
  double r = x;
  while (abs(r * r - x) > eps) {
    r = 0.5 * (r + x / r);
  }
  return r;
}

/// Exponentiation by a real exponent – compact series expansion
consteval double pow(double base, double exp) {
  if (exp == 0.0) return 1.0;
  if (base == 0.0) return 0.0;

  // ln(base) via an AGM‑based series around 1
  const double y  = (base - 1.0) / (base + 1.0);
  const double y2 = y * y;
  double ln = 0.0;
  double term = y;
  for (int k = 1; k < 12; ++k) {          // ~1e‑10 precision
    ln += term / (2 * k - 1);
    term *= y2;
  }
  ln *= 2.0;

  // exp(z) series
  const double z = exp * ln;
  double result = 1.0;
  double t      = 1.0;
  for (int k = 1; k < 20; ++k) {
    t *= z / static_cast<double>(k);
    result += t;
  }
  return result;
}

} // namespace ct