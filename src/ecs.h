#pragma once

#include "physics.h"
#include "raymath.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// Simple cache-friendly ECS
//
// Entities are dense indices into parallel component arrays. Empty slots form
// an internal free-list so allocation and deletion are O(1). Iterating all
// entities of a given component type is a tight linear scan over the packed
// array — no sparse lookups, no pointer chasing.
// ---------------------------------------------------------------------------

#define MAX_ENTITIES 1024
#define INVALID_ENTITY UINT32_MAX

typedef uint32_t Entity;

// --- Component type flags (add new types here as the project grows) ---------

typedef enum {
  COMPONENT_NONE      = 0,
  COMPONENT_TRANSFORM = 1 << 0,
  COMPONENT_PHYSICS   = 1 << 1,
  COMPONENT_MESH      = 1 << 2,
  COMPONENT_CHARACTER = 1 << 3,
} ComponentType;

// --- Components (structs for each component kind) ---------------------------

typedef struct {
  Matrix matrix;
} TransformComponent;

typedef struct {
  PhysicsBody   body;
  PhysicsBodyID bodyId;
  PhysicsBodyType type;
} PhysicsComponent;

typedef struct {
  PhysicsCharacter handle;
  float            speed;    // movement speed
  float            jumpSpeed;
  Vector3          velocity; // cached velocity for facing direction
} CharacterComponent;

typedef struct {
  uint32_t meshIndex;
} MeshComponent;

// --- ECS World --------------------------------------------------------------

typedef struct {
  PhysicsWorld physWorld;

  // Per-entity metadata
  uint32_t masks[MAX_ENTITIES];
  uint32_t netIds[MAX_ENTITIES];
  Entity   freeListHead;
  uint32_t liveCount;
  uint32_t nextNetId;

  // Component arrays (packed by entity index)
  TransformComponent  transforms[MAX_ENTITIES];
  PhysicsComponent    physComponents[MAX_ENTITIES];
  CharacterComponent  charComponents[MAX_ENTITIES];
  MeshComponent       meshComponents[MAX_ENTITIES];

} ECSWorld;

// --- API --------------------------------------------------------------------

void   ECS_Init(ECSWorld *world);
Entity ECS_CreateEntity(ECSWorld *world);
void   ECS_DestroyEntity(ECSWorld *world, Entity e);

uint32_t ECS_GetNetId(const ECSWorld *world, Entity e);
Entity   ECS_FindByNetId(const ECSWorld *world, uint32_t netId);

void ECS_AddTransform(ECSWorld *world, Entity e, Matrix m);
void ECS_AddPhysics(ECSWorld *world, Entity e, PhysicsBody body, PhysicsBodyType type);
void ECS_AddCharacter(ECSWorld *world, Entity e, Vector3 position, float speed, float jumpSpeed);
void ECS_AddMesh(ECSWorld *world, Entity e, uint32_t meshIndex);

// Update all physics and character entities (call once per frame).
// characterInput can be NULL if no input applies.
void ECS_Update(ECSWorld *world, float delta);

// Update a single character with explicit input.
void ECS_UpdateCharacter(ECSWorld *world, Entity e, float delta, Vector3 input, int jump);

uint32_t ECS_LiveCount(const ECSWorld *world);

typedef int (*ECSIterFn)(Entity e, ECSWorld *world, void *userdata);
void ECS_ForEach(ECSWorld *world, uint32_t mask, ECSIterFn fn, void *userdata);
