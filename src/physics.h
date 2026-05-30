#pragma once

#include "raymath.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Physics world (Jolt body interface)
// ---------------------------------------------------------------------------

typedef struct PhysicsWorldImpl PhysicsWorldImpl;
typedef uint32_t PhysicsBodyID;

typedef struct {
  PhysicsWorldImpl *impl;
  float delta;
} PhysicsWorld;

typedef enum {
  PL_MOVING,
  PL_NON_MOVING,
  PL_COUNT,
} PhysicsLayer;

typedef enum {
  PST_BOX,
  PST_SPHERE,
  PST_CYLINDER,
} PhysicsShapeType;

typedef union
{
  Vector3 extents;
  float radius;
  struct { float radius; float halfLength; } cyl;
} PhysicsShapeParams;

typedef enum
{
  PBT_STATIC,
  PBT_DYNAMIC,
} PhysicsBodyType;

typedef struct
{
  PhysicsShapeParams params;
  PhysicsShapeType type;

  Matrix transform;
  
  Vector3 velocity;
  Vector3 angularVelocity;
} PhysicsBody;

int PhysicsWorldCreate(PhysicsWorld *world);

PhysicsBodyID PhysicsWorldAddBody(PhysicsWorld *world, const PhysicsBody *body, PhysicsBodyType type);
void          PhysicsWorldUpdate(PhysicsWorld *world, float delta);
void          PhysicsWorldUpdateBody(PhysicsWorld *world, PhysicsBodyID bodyId, PhysicsBody *body);
void          PhysicsWorldSetBodyVelocity(PhysicsWorld *world, PhysicsBodyID bodyId, Vector3 velocity);
Vector3       PhysicsWorldGetBodyVelocity(PhysicsWorld *world, PhysicsBodyID bodyId);

void PhysicsWorldDestroy(PhysicsWorld *world);

// ---------------------------------------------------------------------------
// Character (Jolt CharacterVirtual — capsule-based kinematic controller)
// ---------------------------------------------------------------------------

typedef struct PhysicsCharacterImpl PhysicsCharacterImpl;
typedef PhysicsCharacterImpl *PhysicsCharacter;

typedef struct {
  float halfHeight;  // half the cylinder height (top/bottom hemispheres beyond this)
  float radius;
  float mass;
  float maxSlopeAngle;  // radians
  float maxStrength;
  float characterPadding;
  float penetrationRecoverySpeed;
  float predictiveContactDistance;
} PhysicsCharacterSettings;

void PhysicsCharacterSettings_Init(PhysicsCharacterSettings *s);

PhysicsCharacter PhysicsWorldCreateCharacter(PhysicsWorld *world,
    const PhysicsCharacterSettings *settings, Vector3 position);

void PhysicsWorldDestroyCharacter(PhysicsWorld *world, PhysicsCharacter ch);

// Step the character forward by delta seconds. Gravity is applied automatically.
// inMovementDirection is a horizontal-only input vector (xz-plane).
// inJump is non-zero to request a jump.
void PhysicsWorldUpdateCharacter(PhysicsWorld *world, PhysicsCharacter ch,
    float delta, Vector3 inMovementDirection, int inJump, float jumpSpeed);

// Read back the resulting world-space matrix.
void PhysicsWorldGetCharacterTransform(PhysicsCharacter ch, Matrix *out);

// Read back the current velocity (used for facing direction).
Vector3 PhysicsWorldGetCharacterVelocity(PhysicsCharacter ch);
