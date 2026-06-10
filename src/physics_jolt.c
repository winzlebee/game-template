#include "physics.h"

#include "joltc.h"

#include <raymath.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "raymath.h"
#include "raylib.h"

typedef struct PhysicsWorldImpl {
  JPH_JobSystem *jobSystem;
  JPH_ObjectLayerPairFilter *layerPairFilter;
  JPH_BroadPhaseLayerInterface *broadPhaseLayerInterfaceTable;
  JPH_ObjectVsBroadPhaseLayerFilter *objectVsBroadPhaseLayerFilter;

  JPH_PhysicsSystem *system;
  JPH_BodyInterface *bodyInterface;

  JPH_BodyFilter  *charBodyFilter;
  JPH_ShapeFilter *charShapeFilter;
} PhysicsWorldImpl;

typedef struct PhysicsCharacterImpl {
  JPH_CharacterVirtual *handle;
  JPH_Shape *shape;
} PhysicsCharacterImpl;

// We use a 1-to-1 mapping between object layers and broadphase layers.
typedef enum {
  BPL_MOVING     = PL_MOVING,
  BPL_NON_MOVING = PL_NON_MOVING,
  BPL_COUNT      = PL_COUNT,
} BroadPhaseLayer;

static PhysicsWorldImpl g_Impl;

int PhysicsWorldCreate(PhysicsWorld *world)
{
	if (!JPH_Init()) {
		return 1;
  }

  world->impl = &g_Impl;
  world->impl->jobSystem = JPH_JobSystemThreadPool_Create(NULL);

	world->impl->layerPairFilter = JPH_ObjectLayerPairFilterTable_Create(2);
	JPH_ObjectLayerPairFilterTable_EnableCollision(world->impl->layerPairFilter, PL_NON_MOVING, PL_MOVING);
	JPH_ObjectLayerPairFilterTable_EnableCollision(world->impl->layerPairFilter, PL_MOVING, PL_NON_MOVING);
	JPH_ObjectLayerPairFilterTable_EnableCollision(world->impl->layerPairFilter, PL_MOVING, PL_MOVING);

	world->impl->broadPhaseLayerInterfaceTable = JPH_BroadPhaseLayerInterfaceTable_Create(2, 2);
	JPH_BroadPhaseLayerInterfaceTable_MapObjectToBroadPhaseLayer(world->impl->broadPhaseLayerInterfaceTable, PL_NON_MOVING, BPL_NON_MOVING);
	JPH_BroadPhaseLayerInterfaceTable_MapObjectToBroadPhaseLayer(world->impl->broadPhaseLayerInterfaceTable, PL_MOVING, BPL_MOVING);

	world->impl->objectVsBroadPhaseLayerFilter = JPH_ObjectVsBroadPhaseLayerFilterTable_Create(world->impl->broadPhaseLayerInterfaceTable, 2, world->impl->layerPairFilter, 2);

	JPH_PhysicsSystemSettings settings = {};

	settings.maxBodies = UINT16_MAX;
	settings.numBodyMutexes = 0;
	settings.maxBodyPairs = UINT16_MAX;
	settings.maxContactConstraints = UINT16_MAX;

	settings.broadPhaseLayerInterface      = world->impl->broadPhaseLayerInterfaceTable;
	settings.objectLayerPairFilter         = world->impl->layerPairFilter;
	settings.objectVsBroadPhaseLayerFilter = world->impl->objectVsBroadPhaseLayerFilter;

	world->impl->system = JPH_PhysicsSystem_Create(&settings);
	world->impl->bodyInterface = JPH_PhysicsSystem_GetBodyInterface(world->impl->system);

  {
    JPH_Vec3 gravity = {0, -9.81f, 0};
    JPH_PhysicsSystem_SetGravity(world->impl->system, &gravity);
  }

  // Create pass-through filters for CharacterVirtual
  world->impl->charBodyFilter  = JPH_BodyFilter_Create(NULL);
  world->impl->charShapeFilter = JPH_ShapeFilter_Create(NULL);

  world->delta = 1.0f / 60.0f;

  return 0;
}

Vector3 PhysicsWorldGetGravity(PhysicsWorld *world)
{
  Vector3 gravity;
  JPH_PhysicsSystem_GetGravity(world->impl->system, (JPH_Vec3 *) &gravity);
  return gravity;
}

PhysicsBodyID PhysicsWorldAddBody(PhysicsWorld *world, const PhysicsBody *body,
                                  const PhysicsShape *pShape)
{
  JPH_Shape *shape = NULL;

  switch (pShape->type) {
    case PST_BOX: {
      const Vector3 hExtents = Vector3Scale(pShape->params.extents, 0.5f);
      shape = (JPH_Shape *) JPH_BoxShape_Create((const JPH_Vec3 *) &hExtents, JPH_DEFAULT_CONVEX_RADIUS);
      break;
    }
    case PST_SPHERE:
      shape = (JPH_Shape *) JPH_SphereShape_Create(pShape->params.radius);
      break;
    case PST_CYLINDER:
      shape = (JPH_Shape *) JPH_CylinderShape_Create(pShape->params.cyl.halfLength, pShape->params.cyl.radius);
      break;
    case PST_CAPSULE:
      shape = (JPH_Shape *) JPH_CapsuleShape_Create(pShape->params.cyl.halfLength, pShape->params.cyl.radius);
      break;
  }

  Vector3    position;
  Quaternion rotation;
  Vector3    scale;

  MatrixDecompose(body->transform, &position, &rotation, &scale);

  const PhysicsLayer   layer      = (body->type == PBT_STATIC ? PL_NON_MOVING               : PL_MOVING);
  const JPH_Activation activate   = (body->type == PBT_STATIC ? JPH_Activation_DontActivate : JPH_Activation_Activate);
  const JPH_MotionType motionType = (body->type == PBT_STATIC ? JPH_MotionType_Static       : JPH_MotionType_Dynamic);

  JPH_BodyCreationSettings *settings = JPH_BodyCreationSettings_Create3(
    shape, (const JPH_Vec3 *)&position, (const JPH_Quat *)&rotation, motionType,
    layer);

  const PhysicsBodyID bodyId = JPH_BodyInterface_CreateAndAddBody(
    world->impl->bodyInterface, settings, activate);

  JPH_BodyCreationSettings_Destroy(settings);

  return bodyId;
}

void PhysicsWorldDestroyBody(PhysicsWorld *world, PhysicsBodyID bodyId)
{
  JPH_BodyInterface_RemoveAndDestroyBody(world->impl->bodyInterface, bodyId);
}

void PhysicsWorldUpdate(PhysicsWorld *world, float delta)
{
  // If you take larger steps than 1 / 60th of a second you need to do multiple
  // collision steps in order to keep the simulation stable. Do 1 collision step per 1 / 60th of a second (round up).
  // const int cCollisionSteps = ceilf(delta / world->delta);
  const int cCollisionSteps = 1;

  JPH_PhysicsSystem_Update(world->impl->system, world->delta, cCollisionSteps, world->impl->jobSystem);
}

void PhysicsWorldUpdateBody(PhysicsWorld *world, PhysicsBodyID bodyId, PhysicsBody *body)
{
  if (!JPH_BodyInterface_IsActive(world->impl->bodyInterface, bodyId)) {
    return;
  }

  JPH_RMat4 worldTransform;
  JPH_BodyInterface_GetWorldTransform(world->impl->bodyInterface, bodyId,
                                      &worldTransform);

  memcpy(&body->transform, &worldTransform, sizeof(JPH_RMat4));
  body->transform = MatrixTranspose(body->transform);

  JPH_BodyInterface_GetLinearAndAngularVelocity(
    world->impl->bodyInterface, bodyId, (JPH_Vec3 *)&body->velocity,
    (JPH_Vec3 *)&body->angularVelocity);
}

void PhysicsWorldSetBodyVelocity(PhysicsWorld *world, PhysicsBodyID bodyId, Vector3 velocity)
{
  JPH_BodyInterface_SetLinearVelocity(world->impl->bodyInterface, bodyId,
                                       (const JPH_Vec3 *)&velocity);
}

Vector3 PhysicsWorldGetBodyVelocity(PhysicsWorld *world, PhysicsBodyID bodyId)
{
  Vector3 vel = {0};
  JPH_BodyInterface_GetLinearVelocity(world->impl->bodyInterface, bodyId,
                                       (JPH_Vec3 *)&vel);
  return vel;
}

void PhysicsWorldDestroy(PhysicsWorld *world)
{
  JPH_BodyFilter_Destroy(world->impl->charBodyFilter);
  JPH_ShapeFilter_Destroy(world->impl->charShapeFilter);
  JPH_JobSystem_Destroy(world->impl->jobSystem);
	JPH_PhysicsSystem_Destroy(world->impl->system);
	JPH_Shutdown();
}

PhysicsCharacterSettings PhysicsCharacterDefaultSettings()
{
  PhysicsCharacterSettings character;

  character.halfHeight    = 0.5f;
  character.radius        = 0.5f;
  character.mass          = 70.0f;
  character.maxSlopeAngle = 45.0f * DEG2RAD;
  character.maxStrength   = 100.0f;

  return character;
}

PhysicsCharacter PhysicsWorldAddCharacter(PhysicsWorld *world, const PhysicsCharacterSettings *settings, Vector3 position)
{
  JPH_CapsuleShape *capsule = JPH_CapsuleShape_Create(settings->halfHeight, settings->radius);

  JPH_CharacterVirtualSettings cvSettings = {0};
  JPH_CharacterVirtualSettings_Init(&cvSettings);

  cvSettings.base.up            = (JPH_Vec3){0, 1, 0};
  cvSettings.base.maxSlopeAngle = settings->maxSlopeAngle;
  cvSettings.base.shape         = (const JPH_Shape *) capsule;
  cvSettings.shapeOffset        = (JPH_Vec3){0, settings->halfHeight + settings->radius, 0};
  cvSettings.mass               = settings->mass;
  cvSettings.maxStrength        = settings->maxStrength;
  cvSettings.backFaceMode       = JPH_BackFaceMode_IgnoreBackFaces;

  const JPH_Quat  rot = {0, 0, 0, 1};
  const JPH_RVec3 pos = *((const JPH_Vec3 *) &position);

  JPH_CharacterVirtual *handle = JPH_CharacterVirtual_Create(
    &cvSettings, &pos, &rot, 0, world->impl->system);

  PhysicsCharacter character;

  character.transform = MatrixTranslate(position.x, position.y, position.z);
  character.velocity  = Vector3Zero();

  character.impl = malloc(sizeof(PhysicsCharacterImpl));
  character.id           = JPH_CharacterVirtual_GetID(handle);
  character.impl->handle = handle;
  character.impl->shape  = (JPH_Shape *) capsule;

  return character;
}


void PhysicsWorldUpdateCharacter(PhysicsWorld *world, PhysicsCharacter *character, float delta)
{
  Vector3    position;
  Quaternion rotation;
  Vector3    scale;

  MatrixDecompose(character->transform, &position, &rotation, &scale);

  JPH_CharacterVirtual_SetLinearVelocity(character->impl->handle, (const JPH_Vec3 *) &character->velocity);
  JPH_CharacterVirtual_SetRotation      (character->impl->handle, (const JPH_Quat *) &rotation);

  JPH_ExtendedUpdateSettings extSettings = {0};
  extSettings.walkStairsStepUp     = (JPH_Vec3){0, 0.3f, 0};
  extSettings.stickToFloorStepDown = (JPH_Vec3){0, 0, 0};

  JPH_CharacterVirtual_ExtendedUpdate(
    character->impl->handle, delta, &extSettings, PL_MOVING,
    world->impl->system, world->impl->charBodyFilter,
    world->impl->charShapeFilter);

  JPH_RMat4 worldTransform;
  JPH_CharacterVirtual_GetWorldTransform(character->impl->handle, &worldTransform);
  memcpy(&character->transform, &worldTransform, sizeof(Matrix));

  character->transform = MatrixTranspose(character->transform);

  character->onGround =
    JPH_CharacterBase_IsSupported((JPH_CharacterBase *)character->impl->handle);
}

void PhysicsWorldDestroyCharacter(PhysicsWorld *world, PhysicsCharacter character)
{
  JPH_CharacterBase_Destroy((JPH_CharacterBase *) character.impl->handle);
  JPH_Shape_Destroy(character.impl->shape);
  free(character.impl);
}
