#pragma once

#include "raymath.h"
#include <stdint.h>

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

void PhysicsWorldDestroy(PhysicsWorld *world);
