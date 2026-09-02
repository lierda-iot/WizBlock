#pragma once

#define isfinite(value) __builtin_isfinite(value)
#define NAN (__builtin_nanf(""))

float ceilf(float value);
float fabsf(float value);
float fmaxf(float left, float right);
float sqrtf(float value);
