#pragma once

#include "raylib.h"
#include "raymath.h"

typedef struct {
  float yaw;      // horizontal orbit angle (radians)
  float pitch;    // vertical tilt (radians, clamped in [-89°, 89°])
  float distance; // orbit radius
} OrbitCamera;

// Init with defaults — starts behind the character (yaw = PI, looking +Z).
void  OrbitCamera_Init(OrbitCamera *oc);

// Update from mouse delta and scroll wheel. Target is the world position to orbit around.
void  OrbitCamera_Update(OrbitCamera *oc, Vector3 target);

// Fill a raylib Camera3D from orbit state.
void  OrbitCamera_ToCamera3D(const OrbitCamera *oc, Vector3 target, Camera3D *out);

// Camera-relative WASD input. Returns a unit-length XZ movement vector in world space for the given camera and target matrix.
Vector3 OrbitCamera_GetInput(const Camera3D *camera, const Matrix *target);
