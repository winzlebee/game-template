#include "ecs.h"
#include "physics.h"

#include <raylib.h>
#include <string.h>
#include <assert.h>

void ECS_Init(ECSWorld *world)
{
  memset(world, 0, sizeof(*world));

  for (Entity i = 0; i < MAX_ENTITIES - 1; i++) {
    // Every entity refers to the next entity in the free-list initially. As entities are allocated
    //  and de-allocated, this will naturally end up being the lowest free entity slot.
    world->masks[i] = (uint32_t)(i + 1) << 16;
  }

  world->masks[MAX_ENTITIES - 1] = INVALID_ENTITY << 16;
  world->freeListHead = 0;
  world->nextNetId = 1;

  PhysicsWorldCreate(&world->physWorld);
}

Entity ECS_CreateEntity(ECSWorld *world)
{
  if (world->freeListHead == INVALID_ENTITY) {
    return INVALID_ENTITY;
  }

  Entity entity = world->freeListHead;
  world->freeListHead = world->masks[entity] >> 16;
  world->masks[entity] = COMPONENT_NONE;
  world->netIds[entity] = world->nextNetId++;
  world->liveCount++;

  return entity;
}

void ECS_DestroyEntity(ECSWorld *world, Entity entity)
{
  if (world->masks[entity] & COMPONENT_PHYSICS) {
    PhysicsWorldDestroyBody(&world->physWorld, world->physComponents[entity].bodyId);
  }

  if (world->masks[entity] & COMPONENT_CHARACTER) {
    PhysicsWorldDestroyCharacter(&world->physWorld, world->charComponents[entity].handle);
  }

  world->masks[entity]  = (world->freeListHead << 16) | COMPONENT_NONE;
  world->netIds[entity] = 0;
  world->freeListHead   = entity;
  world->liveCount--;
}

uint32_t ECS_GetNetId(const ECSWorld *world, Entity entity)
{
  return world->netIds[entity];
}

Entity ECS_FindByNetId(const ECSWorld *world, uint32_t netId)
{
  for (Entity i = 0; i < MAX_ENTITIES; i++) {
    if (world->netIds[i] == netId) {
      return i;
    }
  }

  return INVALID_ENTITY;
}

void ECS_AddTransform(ECSWorld *world, Entity entity, Matrix transform)
{
  world->transforms[entity].matrix = transform;
  world->masks[entity] |= COMPONENT_TRANSFORM;
}

void ECS_AddPhysics(ECSWorld *world, Entity entity, PhysicsBody body,
                    PhysicsBodyType type)
{
  assert(!(world->masks[entity] & COMPONENT_CHARACTER) &&
         "Entity already has character component");

  world->physComponents[entity].body   = body;
  world->physComponents[entity].type   = type;
  world->physComponents[entity].bodyId = PhysicsWorldAddBody(&world->physWorld, &body, type);

  world->transforms[entity].matrix = body.transform;
  world->masks[entity] |= (COMPONENT_PHYSICS | COMPONENT_TRANSFORM);
}

void ECS_AddCharacter(ECSWorld *world, Entity entity, Vector3 position,
                      float speed, float jumpSpeed)
{
  assert(!(world->masks[entity] & COMPONENT_PHYSICS) &&
         "Entity already has physics component");

  PhysicsCharacterSettings settings;
  PhysicsCharacterSettings_Init(&settings);

  settings.halfHeight = 0.8f;
  settings.radius = 0.4f;
  settings.mass = 70.0f;

  world->charComponents[entity].handle    = PhysicsWorldCreateCharacter(&world->physWorld, &settings, position);
  world->charComponents[entity].speed     = speed;
  world->charComponents[entity].jumpSpeed = jumpSpeed;
  world->charComponents[entity].velocity  = (Vector3){0, 0, 0};

  PhysicsWorldGetCharacterTransform(world->charComponents[entity].handle,
                                    &world->transforms[entity].matrix);

  world->masks[entity] |= (COMPONENT_CHARACTER | COMPONENT_TRANSFORM);
}

void ECS_AddMesh(ECSWorld *world, Entity entity, uint32_t meshIndex)
{
  world->meshComponents[entity].meshIndex = meshIndex;
  world->masks[entity] |= COMPONENT_MESH;
}

void ECS_UpdateCharacter(ECSWorld *world, Entity entity, float delta, Vector3 input,
                         int jump)
{
  assert((world->masks[entity] & COMPONENT_CHARACTER) &&
         "Entity is not a character");

  CharacterComponent *character = &world->charComponents[entity];

  PhysicsWorldUpdateCharacter(&world->physWorld, character->handle, delta, input, jump, character->jumpSpeed);
  PhysicsWorldGetCharacterTransform(character->handle, &world->transforms[entity].matrix);

  character->velocity = PhysicsWorldGetCharacterVelocity(character->handle);
}

void ECS_Update(ECSWorld *world, float delta)
{
  PhysicsWorldUpdate(&world->physWorld, delta);

  for (Entity i = 0; i < MAX_ENTITIES; i++) {

    if (!(world->masks[i] & COMPONENT_PHYSICS)) {
      continue;
    }

    PhysicsWorldUpdateBody(&world->physWorld, world->physComponents[i].bodyId,
                           &world->physComponents[i].body);

    world->transforms[i].matrix = world->physComponents[i].body.transform;
  }
}

uint32_t ECS_LiveCount(const ECSWorld *world)
{
  return world->liveCount;
}

void ECS_ForEach(ECSWorld *world, uint32_t mask, ECSIterFn function, void *userdata)
{
  for (Entity i = 0; i < MAX_ENTITIES; i++) {
    if ((world->masks[i] & mask) == mask) {
      if (function(i, world, userdata) != 0) {
        break;
      }
    }
  }
}
