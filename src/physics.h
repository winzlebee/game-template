#pragma once

typedef struct JPH_JobSystem JPH_JobSystem;
typedef struct JPH_ObjectLayerPairFilter JPH_ObjectLayerPairFilter;
typedef struct JPH_BroadPhaseLayerInterface JPH_BroadPhaseLayerInterface;
typedef struct JPH_ObjectVsBroadPhaseLayerFilter JPH_ObjectVsBroadPhaseLayerFilter;
typedef struct JPH_PhysicsSystem JPH_PhysicsSystem;
typedef struct JPH_BodyInterface JPH_BodyInterface;

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

extern PhysicsWorld g_PhysicsWorld;

int  PhysicsWorldCreate(PhysicsWorld *world);
void PhysicsWorldDestroy(PhysicsWorld *world);
