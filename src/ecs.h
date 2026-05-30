#pragma once

#include "physics.h"
#include "raymath.h"

#include <stdint.h>
#include <stdbool.h>

#define MAX_ENTITIES   1024
#define INVALID_ENTITY UINT16_MAX

typedef uint32_t Entity;

typedef enum {
  COMPONENT_NONE      = 0,
  COMPONENT_TRANSFORM = 0x01,
  COMPONENT_PHYSICS   = 0x02,
  COMPONENT_MESH      = 0x04,
  COMPONENT_CHARACTER = 0x08,
} ComponentType;

typedef struct {
  Matrix matrix;
} TransformComponent;

typedef struct {
  PhysicsBody   body;
  PhysicsBodyID bodyId;
  PhysicsShape  shape;
} PhysicsComponent;

typedef struct {
  PhysicsCharacter character;
  float            speed;
  float            jumpSpeed;
} CharacterComponent;

typedef struct {
  uint32_t meshIndex;
} MeshComponent;

typedef struct {
  PhysicsWorld physWorld;

  /**
   * Bit-mask of the components attached to each entity
   */
  uint32_t masks[MAX_ENTITIES];

  /**
   * Network ID corresponding to each entity. The server and client
   * agree on this ID, it is sent with world update messages.
   */
  uint32_t netIds[MAX_ENTITIES];

  /**
   * ID of the next free entity slot
   */
  Entity freeListHead;

  /**
   * Number of current active entities
   */
  uint32_t liveCount;

  /**
   * ID of the next free Network ID
   */
  uint32_t nextNetId;

  TransformComponent  transforms[MAX_ENTITIES];
  PhysicsComponent    physComponents[MAX_ENTITIES];
  CharacterComponent  charComponents[MAX_ENTITIES];
  MeshComponent       meshComponents[MAX_ENTITIES];
} ECSWorld;

void   ECS_Init(ECSWorld *world);
Entity ECS_CreateEntity(ECSWorld *world);
void   ECS_DestroyEntity(ECSWorld *world, Entity entity);

uint32_t ECS_GetNetId(const ECSWorld *world, Entity entity);
Entity   ECS_FindByNetId(const ECSWorld *world, uint32_t netId);

void ECS_AddTransform(ECSWorld *world, Entity entity, Matrix transform);
void ECS_AddPhysics(ECSWorld *world, Entity entity, PhysicsBody body, PhysicsShape shape);
void ECS_AddCharacter(ECSWorld *world, Entity entity, Vector3 position, float speed, float jumpSpeed);
void ECS_AddMesh(ECSWorld *world, Entity entity, uint32_t meshIndex);

void ECS_Update(ECSWorld *world, float delta);
void ECS_UpdateCharacter(ECSWorld *world, Entity entity, float delta, Vector3 input, bool jump);

uint32_t ECS_LiveCount(const ECSWorld *world);

typedef int (*ECSIterFn)(Entity entity, ECSWorld *world, void *userdata);
void ECS_ForEach(ECSWorld *world, uint32_t mask, ECSIterFn function, void *userdata);
