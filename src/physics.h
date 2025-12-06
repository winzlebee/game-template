#pragma once

#include "raymath.h"
#include <stdint.h>

typedef struct JPH_JobSystem JPH_JobSystem;
typedef struct JPH_ObjectLayerPairFilter JPH_ObjectLayerPairFilter;
typedef struct JPH_BroadPhaseLayerInterface JPH_BroadPhaseLayerInterface;
typedef struct JPH_ObjectVsBroadPhaseLayerFilter JPH_ObjectVsBroadPhaseLayerFilter;
typedef struct JPH_PhysicsSystem JPH_PhysicsSystem;
typedef struct JPH_BodyInterface JPH_BodyInterface;

typedef uint32_t PhysicsBodyID;

typedef struct {
  JPH_JobSystem *jobSystem;
  JPH_ObjectLayerPairFilter *layerPairFilter;
  JPH_BroadPhaseLayerInterface *broadPhaseLayerInterfaceTable;
  JPH_ObjectVsBroadPhaseLayerFilter *objectVsBroadPhaseLayerFilter;

  JPH_PhysicsSystem *system;
  JPH_BodyInterface *bodyInterface;
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
} PhysicsBody;

int  PhysicsWorldCreate(PhysicsWorld *world);

PhysicsBodyID PhysicsWorldAddBody(PhysicsWorld* world, const PhysicsBody* body,
                                  PhysicsBodyType type);

void PhysicsWorldDestroy(PhysicsWorld *world);
