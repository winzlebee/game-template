#include "ecs.h"

#include <raylib.h>
#include <string.h>

void ECS_Init(ECSWorld *world)
{
  memset(world, 0, sizeof(*world));

  for (Entity i = 0; i < MAX_ENTITIES - 1; i++) {
    world->masks[i] = (uint32_t)(i + 1) << 16;
  }
  world->masks[MAX_ENTITIES - 1] = INVALID_ENTITY << 16;
  world->freeListHead = 0;
  world->nextNetId = 1;

  PhysicsWorldCreate(&world->physWorld);
}

Entity ECS_CreateEntity(ECSWorld *world)
{
  if (world->freeListHead == INVALID_ENTITY)
    return INVALID_ENTITY;

  Entity e = world->freeListHead;
  world->freeListHead = world->masks[e] >> 16;
  world->masks[e] = COMPONENT_NONE;
  world->netIds[e] = world->nextNetId++;
  world->liveCount++;
  return e;
}

void ECS_DestroyEntity(ECSWorld *world, Entity e)
{
  if (world->masks[e] == COMPONENT_NONE)
    return;

  // Clean up character if it has one
  if (world->masks[e] & COMPONENT_CHARACTER)
    PhysicsWorldDestroyCharacter(&world->physWorld, world->charComponents[e].handle);

  world->masks[e] = (world->freeListHead << 16) | COMPONENT_NONE;
  world->netIds[e] = 0;
  world->freeListHead = e;
  world->liveCount--;
}

uint32_t ECS_GetNetId(const ECSWorld *world, Entity e)
{
  return world->netIds[e];
}

Entity ECS_FindByNetId(const ECSWorld *world, uint32_t netId)
{
  for (Entity i = 0; i < MAX_ENTITIES; i++) {
    if (world->masks[i] != COMPONENT_NONE && world->netIds[i] == netId)
      return i;
  }
  return INVALID_ENTITY;
}

void ECS_AddTransform(ECSWorld *world, Entity e, Matrix m)
{
  world->transforms[e].matrix = m;
  world->masks[e] |= COMPONENT_TRANSFORM;
}

void ECS_AddPhysics(ECSWorld *world, Entity e, PhysicsBody body, PhysicsBodyType type)
{
  world->physComponents[e].body   = body;
  world->physComponents[e].type   = type;
  world->physComponents[e].bodyId = PhysicsWorldAddBody(&world->physWorld, &body, type);
  world->transforms[e].matrix = body.transform;
  world->masks[e] |= COMPONENT_PHYSICS | COMPONENT_TRANSFORM;
}

void ECS_AddCharacter(ECSWorld *world, Entity e, Vector3 position, float speed, float jumpSpeed)
{
  PhysicsCharacterSettings settings;
  PhysicsCharacterSettings_Init(&settings);
  // Use larger capsule for players
  settings.halfHeight = 0.8f;
  settings.radius     = 0.4f;
  settings.mass       = 70.0f;

  world->charComponents[e].handle    = PhysicsWorldCreateCharacter(&world->physWorld, &settings, position);
  world->charComponents[e].speed     = speed;
  world->charComponents[e].jumpSpeed = jumpSpeed;
  world->charComponents[e].velocity  = (Vector3){0, 0, 0};

  // Initialize transform from character
  PhysicsWorldGetCharacterTransform(world->charComponents[e].handle,
                                    &world->transforms[e].matrix);
  world->masks[e] |= COMPONENT_CHARACTER | COMPONENT_TRANSFORM;
}

void ECS_AddMesh(ECSWorld *world, Entity e, uint32_t meshIndex)
{
  world->meshComponents[e].meshIndex = meshIndex;
  world->masks[e] |= COMPONENT_MESH;
}

void ECS_UpdateCharacter(ECSWorld *world, Entity e, float delta, Vector3 input, int jump)
{
  if (!(world->masks[e] & COMPONENT_CHARACTER))
    return;

  CharacterComponent *cc = &world->charComponents[e];

  PhysicsWorldUpdateCharacter(&world->physWorld, cc->handle, delta, input, jump, cc->jumpSpeed);

  PhysicsWorldGetCharacterTransform(cc->handle, &world->transforms[e].matrix);

  cc->velocity = PhysicsWorldGetCharacterVelocity(cc->handle);
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

void ECS_ForEach(ECSWorld *world, uint32_t mask, ECSIterFn fn, void *userdata)
{
  for (Entity i = 0; i < MAX_ENTITIES; i++) {
    if ((world->masks[i] & mask) == mask) {
      if (fn(i, world, userdata) != 0)
        break;
    }
  }
}
