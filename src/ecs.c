#include "ecs.h"
#include "physics.h"

#include <math.h>
#include <raylib.h>
#include <raymath.h>
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
    PhysicsWorldDestroyCharacter(&world->physWorld, world->charComponents[entity].character);
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
                    PhysicsShape shape)
{
  assert(!(world->masks[entity] & COMPONENT_CHARACTER) &&
         "Entity already has character component");

  world->physComponents[entity].body   = body;
  world->physComponents[entity].shape  = shape;
  world->physComponents[entity].bodyId = PhysicsWorldAddBody(&world->physWorld, &body, &shape);

  world->transforms[entity].matrix = body.transform;
  world->masks[entity] |= (COMPONENT_PHYSICS | COMPONENT_TRANSFORM);
}

void ECS_AddCharacter(ECSWorld *world, Entity entity, Vector3 position,
                      float speed, float jumpSpeed)
{
  assert(!(world->masks[entity] & COMPONENT_PHYSICS) &&
         "Entity already has physics component");

  PhysicsCharacterSettings settings = PhysicsCharacterDefaultSettings();

  settings.halfHeight = 1.0f;
  settings.radius = 0.75f;
  settings.mass = 70.0f;

  world->charComponents[entity].character = PhysicsWorldAddCharacter(&world->physWorld, &settings, position);
  world->charComponents[entity].speed     = speed;
  world->charComponents[entity].jumpSpeed = jumpSpeed;

  world->masks[entity] |= (COMPONENT_CHARACTER | COMPONENT_TRANSFORM);
}

void ECS_AddMesh(ECSWorld *world, Entity entity, uint32_t meshIndex)
{
  world->meshComponents[entity].meshIndex = meshIndex;
  world->meshComponents[entity].animIndex = MESH_ANIMATION_NONE;
  world->masks[entity] |= COMPONENT_MESH;
}

void ECS_UpdateCharacter(ECSWorld *world, Entity entity, float delta,
                         Vector3 input, bool jump)
{
  assert((world->masks[entity] & COMPONENT_CHARACTER) &&
         "Entity is not a character");

  CharacterComponent *character = &world->charComponents[entity];
  PhysicsCharacter   *physics   = &character->character;

  // Get the gravity direction, and the velocity that has already been applied
  // in that direction, so we can re-apply it after we add the desired movement vector.
  const Vector3 gravity    = PhysicsWorldGetGravity(&world->physWorld);
  const Vector3 gravityDir = Vector3Normalize(gravity);
  const float   applied    = Vector3DotProduct(physics->velocity, gravityDir);

  // Create a global velocity vector from our global input frame and player speed
  const Vector3 inMovement = Vector3Scale(input, character->speed);

  // Cancel out any player movement in the gravity direction
  const Vector3 movement = Vector3Subtract(inMovement, Vector3Scale(gravityDir, Vector3DotProduct(inMovement, gravityDir)));

  // Construct final character velocity
  Vector3 velocity = movement;

  if (physics->onGround) {
    if (jump) {
      velocity = Vector3Add(velocity, Vector3Scale(Vector3Negate(gravityDir), character->jumpSpeed));
    }
  } else {
    // Re-introduce existing gravity
    velocity = Vector3Add(movement, Vector3Scale(gravityDir, applied));

    // Add some new gravity this update
    velocity = Vector3Add(velocity, Vector3Scale(gravity, delta));
  }

  if (Vector3Length(movement) >= 0.01f) {
    Vector3 translation;
    Quaternion rotation;
    Vector3 scale;

    MatrixDecompose(physics->transform, &translation, &rotation, &scale);

    const Quaternion facing = QuaternionFromVector3ToVector3((Vector3){0, 0, 1}, Vector3Normalize(movement));

    physics->transform = MatrixCompose(translation, facing, scale);
  }

  // The only parameter we drive is the velocity of the player.
  // Everything else is calculated for us by the physics engine.
  physics->velocity = velocity;
}

void ECS_Update(ECSWorld *world, float delta)
{
  PhysicsWorldUpdate(&world->physWorld, delta);

  for (Entity i = 0; i < MAX_ENTITIES; i++) {
    if (world->masks[i] & COMPONENT_PHYSICS) {
      assert(world->masks[i] & COMPONENT_TRANSFORM);
      PhysicsWorldUpdateBody(&world->physWorld, world->physComponents[i].bodyId,
                             &world->physComponents[i].body);
      world->transforms[i].matrix = world->physComponents[i].body.transform;
    }

    if (world->masks[i] & COMPONENT_CHARACTER) {
      assert(world->masks[i] & COMPONENT_TRANSFORM);
      PhysicsWorldUpdateCharacter(&world->physWorld, &world->charComponents[i].character, delta);
      world->transforms[i].matrix = world->charComponents[i].character.transform;
    }
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
