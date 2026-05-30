#include "orbit_camera.h"

#include <math.h>
#include <raylib.h>
#include <raymath.h>

#define DEFAULT_DISTANCE  8.0f
#define MIN_PITCH_RAD (-89.0f * DEG2RAD)
#define MAX_PITCH_RAD ( 89.0f * DEG2RAD)
#define MIN_DISTANCE  3.0f
#define MAX_DISTANCE 20.0f

void OrbitCamera_Init(OrbitCamera *oc)
{
  oc->yaw      = PI;
  oc->pitch    = 0.35f;
  oc->distance = DEFAULT_DISTANCE;
}

void OrbitCamera_Update(OrbitCamera *oc, Vector3 target)
{
  (void)target; // caller uses target separately

  // Mouse orbit
  Vector2 md = GetMouseDelta();
  float sensitivity = 0.003f;
  oc->yaw   -= md.x * sensitivity;
  oc->pitch += md.y * sensitivity;

  if (oc->pitch > MAX_PITCH_RAD) oc->pitch = MAX_PITCH_RAD;
  if (oc->pitch < MIN_PITCH_RAD) oc->pitch = MIN_PITCH_RAD;

  // Scroll zoom
  float scroll = GetMouseWheelMove();
  oc->distance -= scroll * 1.5f;
  if (oc->distance < MIN_DISTANCE) oc->distance = MIN_DISTANCE;
  if (oc->distance > MAX_DISTANCE) oc->distance = MAX_DISTANCE;
}

void OrbitCamera_ToCamera3D(const OrbitCamera *oc, Vector3 target, Camera3D *out)
{
  Vector3 offset = {
    oc->distance * cosf(oc->pitch) * sinf(oc->yaw),
    oc->distance * sinf(oc->pitch),
    oc->distance * cosf(oc->pitch) * cosf(oc->yaw),
  };

  out->position   = Vector3Add(target, offset);
  out->target     = target;
  out->up         = (Vector3){0, 1, 0};
  out->fovy       = 45.0f;
  out->projection = CAMERA_PERSPECTIVE;
}

Vector3 OrbitCamera_GetInput(const Camera3D *camera, const Matrix *target)
{
  const Vector3 cameraForward = Vector3Subtract(camera->target, camera->position);
  const Vector3 cameraRight   = Vector3CrossProduct(cameraForward, camera->up);
  const Vector3 playerUp      = (Vector3){target->m4, target->m5, target->m6};
  const Vector3 playerRight   = (Vector3){target->m0, target->m1, target->m2};

  const Vector3 forward = Vector3Normalize(Vector3Subtract(cameraForward, Vector3Scale(playerUp, Vector3DotProduct(cameraForward, playerUp))));
  const Vector3 right   = Vector3Normalize(cameraRight);

  float forwardBack = 0.0f;
  float leftRight   = 0.0f;

  forwardBack += GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
  leftRight   += GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);

  forwardBack += (float)(IsKeyDown(KEY_W) - IsKeyDown(KEY_S));
  leftRight   += (float)(IsKeyDown(KEY_D) - IsKeyDown(KEY_A));

  Vector3 result = Vector3Zero();
  result = Vector3Add(result, Vector3Scale(forward, forwardBack));
  result = Vector3Add(result, Vector3Scale(right,   leftRight));

  const float mag = Vector3Length(result);

  if (mag > 1.0f) {
    result = Vector3Scale(result, 1.0f / mag);
  }

  return result;
}
