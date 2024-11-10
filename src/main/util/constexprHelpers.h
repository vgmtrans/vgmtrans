#pragma once

#include <cmath>

// Custom constexpr absolute value function
constexpr double constexpr_abs(double value) {
  return (value < 0) ? -value : value;
}

constexpr double constexpr_pow(double base, double exp) {
  // Uses a basic Taylor series for approximation, sufficient for small exponents
  if (exp == 0.0) return 1.0;
  if (base == 0.0) return 0.0;

  // Approximate log and exp calculations
  double result = 1.0;
  double term = 1.0;
  double log_base = (base - 1.0) / (base + 1.0);
  double exp_term = exp * log_base;

  for (int i = 1; i < 20; ++i) {  // Iterate for sufficient precision
    term *= exp_term / i;
    result += term;
  }

  return result;
}

constexpr double constexpr_sqrt(double value, double precision = 1e-10) {
  if (value < 0.0) return 0.0;  // Return 0 for negative values to prevent invalid inputs
  double x = value;
  double result = value;

  while (constexpr_abs(result * result - x) > precision) {
    result = (result + x / result) / 2.0;
  }
  return result;
}