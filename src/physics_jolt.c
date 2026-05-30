#include "physics.h"

#include "joltc.h"

#include <raymath.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

typedef struct PhysicsWorldImpl {
  JPH_JobSystem *jobSystem;
  JPH_ObjectLayerPairFilter *layerPairFilter;
  JPH_BroadPhaseLayerInterface *broadPhaseLayerInterfaceTable;
  JPH_ObjectVsBroadPhaseLayerFilter *objectVsBroadPhaseLayerFilter;

  JPH_PhysicsSystem *system;
  JPH_BodyInterface *bodyInterface;

  // Filters used by CharacterVirtual update
  JPH_BodyFilter  *charBodyFilter;
  JPH_ShapeFilter *charShapeFilter;
} PhysicsWorldImpl;

typedef struct PhysicsCharacterImpl {
  JPH_CharacterVirtual *handle;
  JPH_CapsuleShape      *capsule;
  float                  halfHeight;
  float                  radius;
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

PhysicsBodyID PhysicsWorldAddBody(PhysicsWorld* world, const PhysicsBody* body,
                                  PhysicsBodyType type)
{
  JPH_Shape *shape = NULL;

  switch (body->type) {
    case PST_BOX: {
      const Vector3 hExtents = Vector3Scale(body->params.extents, 0.5f);
      shape = (JPH_Shape *) JPH_BoxShape_Create((const JPH_Vec3 *) &hExtents, JPH_DEFAULT_CONVEX_RADIUS);
      break;
    }
    case PST_SPHERE:
      shape = (JPH_Shape *) JPH_SphereShape_Create(body->params.radius);
      break;
    case PST_CYLINDER:
      shape = (JPH_Shape *) JPH_CylinderShape_Create(body->params.cyl.halfLength, body->params.cyl.radius);
      break;
  }

  Vector3    position;
  Quaternion rotation;
  Vector3    scale;

  MatrixDecompose(body->transform, &position, &rotation, &scale);

  const PhysicsLayer   layer      = (type == PBT_STATIC ? PL_NON_MOVING               : PL_MOVING);
  const JPH_Activation activate   = (type == PBT_STATIC ? JPH_Activation_DontActivate : JPH_Activation_Activate);
  const JPH_MotionType motionType = (type == PBT_STATIC ? JPH_MotionType_Static       : JPH_MotionType_Dynamic);

  JPH_BodyCreationSettings *settings = JPH_BodyCreationSettings_Create3(
    shape, (const JPH_Vec3 *)&position, (const JPH_Quat *)&rotation, motionType,
    layer);

  const JPH_BodyID id = JPH_BodyInterface_CreateAndAddBody(world->impl->bodyInterface,
                                                           settings, activate);

  JPH_BodyCreationSettings_Destroy(settings);

  return id;
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

// ---------------------------------------------------------------------------
// CharacterVirtual
// ---------------------------------------------------------------------------

void PhysicsCharacterSettings_Init(PhysicsCharacterSettings *s)
{
  s->halfHeight       = 0.5f;
  s->radius           = 0.5f;
  s->mass             = 70.0f;
  s->maxSlopeAngle    = 0.785f;  // ~45 degrees
  s->maxStrength      = 100.0f;
  s->characterPadding    = 0.02f;
  s->penetrationRecoverySpeed  = 1.0f;
  s->predictiveContactDistance = 0.1f;
}

PhysicsCharacter PhysicsWorldCreateCharacter(PhysicsWorld *world,
    const PhysicsCharacterSettings *settings, Vector3 position)
{
  PhysicsCharacterImpl *ch = malloc(sizeof(PhysicsCharacterImpl));
  memset(ch, 0, sizeof(*ch));

  ch->halfHeight = settings->halfHeight;
  ch->radius     = settings->radius;

  // Create the capsule shape (centered at origin — offset done via shapeOffset)
  ch->capsule = JPH_CapsuleShape_Create(settings->halfHeight, settings->radius);

  // Zero-init settings before Init to avoid uninitialized fields
  JPH_CharacterVirtualSettings cvSettings;
  memset(&cvSettings, 0, sizeof(cvSettings));
  JPH_CharacterVirtualSettings_Init(&cvSettings);

  cvSettings.base.up              = (JPH_Vec3){0, 1, 0};
  cvSettings.base.maxSlopeAngle   = settings->maxSlopeAngle;
  cvSettings.base.shape           = (const JPH_Shape *)ch->capsule;
  cvSettings.shapeOffset          = (JPH_Vec3){0, settings->halfHeight + settings->radius, 0};
  cvSettings.mass                 = settings->mass;
  cvSettings.maxStrength          = settings->maxStrength;
  cvSettings.backFaceMode         = JPH_BackFaceMode_IgnoreBackFaces;
  cvSettings.predictiveContactDistance = settings->predictiveContactDistance;
  cvSettings.characterPadding     = settings->characterPadding;
  cvSettings.penetrationRecoverySpeed = settings->penetrationRecoverySpeed;

  JPH_RVec3 pos = {position.x, position.y, position.z};
  JPH_Quat  rot = {0, 0, 0, 1};

  ch->handle = JPH_CharacterVirtual_Create(&cvSettings, &pos, &rot, 0,
                                           world->impl->system);

  return ch;
}

void PhysicsWorldDestroyCharacter(PhysicsWorld *world, PhysicsCharacter ch)
{
  JPH_CharacterBase_Destroy((JPH_CharacterBase *)ch->handle);
  JPH_Shape_Destroy((JPH_Shape *)ch->capsule);
  free(ch);
  (void)world;
}

void PhysicsWorldUpdateCharacter(PhysicsWorld *world, PhysicsCharacter ch,
    float delta, Vector3 inMovementDirection, int inJump, float jumpSpeed)
{
  JPH_CharacterVirtual *cv = ch->handle;

  JPH_Vec3 curLinearVel;
  JPH_CharacterVirtual_GetLinearVelocity(cv, &curLinearVel);

  bool onGround = JPH_CharacterBase_IsSupported((JPH_CharacterBase *)cv);

  // Build new velocity
  JPH_Vec3 newVel;

  float speed = 5.0f;
  JPH_Vec3 desiredHoriz = {
    inMovementDirection.x * speed,
    0,
    inMovementDirection.z * speed
  };

  if (onGround) {
    // On ground: drive horizontal from input, keep Y from current (usually near 0)
    newVel.x = desiredHoriz.x;
    newVel.y = curLinearVel.y;
    newVel.z = desiredHoriz.z;

    // Jump: add upward impulse
    if (inJump) {
      newVel.y += jumpSpeed;
    }
  } else {
    // In air: drive horizontal from input, apply gravity to vertical
    JPH_Vec3 gravity;
    JPH_PhysicsSystem_GetGravity(world->impl->system, &gravity);

    newVel.x = desiredHoriz.x;
    newVel.y = curLinearVel.y + gravity.y * delta;
    newVel.z = desiredHoriz.z;
  }


  JPH_CharacterVirtual_SetLinearVelocity(cv, &newVel);

  if (Vector3Length(inMovementDirection) >= 0.01f) {
    Quaternion facingRotation = QuaternionFromVector3ToVector3((Vector3){0, 0, 1}, Vector3Normalize(inMovementDirection));
    JPH_Quat facing;
    memcpy(&facing, &facingRotation, sizeof(Quaternion));
    JPH_CharacterVirtual_SetRotation(cv, &facing);
  }

  // Update
  JPH_ExtendedUpdateSettings extSettings;
  memset(&extSettings, 0, sizeof(extSettings));

  JPH_Vec3 stepUp   = {0, 0.3f, 0};
  JPH_Vec3 stepDown = {0, 0, 0};
  extSettings.walkStairsStepUp     = stepUp;
  extSettings.stickToFloorStepDown = stepDown;

  JPH_CharacterVirtual_ExtendedUpdate(cv, delta, &extSettings,
      PL_MOVING, world->impl->system,
      world->impl->charBodyFilter, world->impl->charShapeFilter);
}

void PhysicsWorldGetCharacterTransform(PhysicsCharacter ch, Matrix *out)
{
  JPH_RMat4 worldTransform;
  JPH_CharacterVirtual_GetWorldTransform(ch->handle, &worldTransform);
  memcpy(out, &worldTransform, sizeof(Matrix));
  *out = MatrixTranspose(*out);
}

Vector3 PhysicsWorldGetCharacterVelocity(PhysicsCharacter ch)
{
  JPH_Vec3 vel;
  JPH_CharacterVirtual_GetLinearVelocity(ch->handle, &vel);
  return (Vector3){vel.x, vel.y, vel.z};
}
