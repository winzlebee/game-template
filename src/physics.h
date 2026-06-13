#pragma once

#include "raymath.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct PhysicsWorldImpl PhysicsWorldImpl;
typedef struct PhysicsCharacterImpl PhysicsCharacterImpl;

typedef uint32_t PhysicsBodyID;
typedef uint32_t PhysicsCharacterID;

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
  PST_CAPSULE,
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
  PhysicsShapeType   type;
  PhysicsShapeParams params;
} PhysicsShape;

typedef struct
{
  Matrix  transform;
  Vector3 velocity;
  Vector3 angularVelocity;
  PhysicsBodyType type;
} PhysicsBody;

typedef struct
{
  Matrix  transform;
  Vector3 velocity;
  bool    onGround;

  PhysicsCharacterID id;
  PhysicsCharacterImpl *impl;
} PhysicsCharacter;

typedef struct
{
  float halfHeight;
  float radius;
  float mass;
  float maxSlopeAngle;
  float maxStrength;
} PhysicsCharacterSettings;

int     PhysicsWorldCreate(PhysicsWorld *world);
Vector3 PhysicsWorldGetGravity(PhysicsWorld *world);

PhysicsBodyID PhysicsWorldAddBody(PhysicsWorld *world, const PhysicsBody *body, const PhysicsShape *pShape);
void          PhysicsWorldDestroyBody(PhysicsWorld *world, PhysicsBodyID bodyId);
void          PhysicsWorldUpdate(PhysicsWorld *world, float delta);
void          PhysicsWorldUpdateBody(PhysicsWorld *world, PhysicsBodyID bodyId, PhysicsBody *body);

PhysicsCharacterSettings PhysicsCharacterDefaultSettings();
PhysicsCharacter         PhysicsWorldAddCharacter(PhysicsWorld *world, const PhysicsCharacterSettings *settings, Vector3 position);
void                     PhysicsWorldDestroyCharacter(PhysicsWorld *world, PhysicsCharacter character);
void                     PhysicsWorldUpdateCharacter(PhysicsWorld *world, PhysicsCharacter *character, float delta);

void          PhysicsWorldApplyCentralForce(PhysicsWorld *world, PhysicsBodyID bodyId, Vector3 force);
void          PhysicsWorldSetBodyVelocity(PhysicsWorld *world, PhysicsBodyID bodyId, Vector3 velocity);
Vector3       PhysicsWorldGetBodyVelocity(PhysicsWorld *world, PhysicsBodyID bodyId);

void PhysicsWorldDestroy(PhysicsWorld *world);
