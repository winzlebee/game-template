#include "physics.h"

#include "joltc.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// We use a 1-to-1 mapping between object layers and broadphase layers.
typedef enum {
  BPL_MOVING     = PL_MOVING,
  BPL_NON_MOVING = PL_NON_MOVING,
  BPL_COUNT      = PL_COUNT,
} BroadPhaseLayer;

int PhysicsWorldCreate(PhysicsWorld *world)
{
	if (!JPH_Init()) {
		return 1;
  }

  world->jobSystem = JPH_JobSystemThreadPool_Create(NULL);

	world->layerPairFilter = JPH_ObjectLayerPairFilterTable_Create(2);
	JPH_ObjectLayerPairFilterTable_EnableCollision(world->layerPairFilter, PL_NON_MOVING, PL_MOVING);
	JPH_ObjectLayerPairFilterTable_EnableCollision(world->layerPairFilter, PL_MOVING, PL_NON_MOVING);

	world->broadPhaseLayerInterfaceTable = JPH_BroadPhaseLayerInterfaceTable_Create(2, 2);
	JPH_BroadPhaseLayerInterfaceTable_MapObjectToBroadPhaseLayer(world->broadPhaseLayerInterfaceTable, PL_NON_MOVING, BPL_NON_MOVING);
	JPH_BroadPhaseLayerInterfaceTable_MapObjectToBroadPhaseLayer(world->broadPhaseLayerInterfaceTable, PL_MOVING, BPL_MOVING);

	world->objectVsBroadPhaseLayerFilter = JPH_ObjectVsBroadPhaseLayerFilterTable_Create(world->broadPhaseLayerInterfaceTable, 2, world->layerPairFilter, 2);

	JPH_PhysicsSystemSettings settings = {};

	settings.maxBodies = 65536;
	settings.numBodyMutexes = 0;
	settings.maxBodyPairs = 65536;
	settings.maxContactConstraints = 65536;

	settings.broadPhaseLayerInterface      = world->broadPhaseLayerInterfaceTable;
	settings.objectLayerPairFilter         = world->layerPairFilter;
	settings.objectVsBroadPhaseLayerFilter = world->objectVsBroadPhaseLayerFilter;

	world->system = JPH_PhysicsSystem_Create(&settings);
	world->bodyInterface = JPH_PhysicsSystem_GetBodyInterface(world->system);

  return 0;
}

PhysicsBodyID PhysicsWorldAddBody(PhysicsWorld* world, const PhysicsBody* body,
                                  PhysicsBodyType type)
{
  JPH_Shape *shape = NULL;

  switch (body->type) {
    case PST_BOX:
      shape = (JPH_Shape *) JPH_BoxShape_Create((const JPH_Vec3 *) &body->params.extents, JPH_DEFAULT_CONVEX_RADIUS);
      break;
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

  const PhysicsLayer   layer    = (type == PBT_STATIC ? PL_NON_MOVING               : PL_MOVING);
  const JPH_Activation activate = (type == PBT_STATIC ? JPH_Activation_DontActivate : JPH_Activation_Activate);

  JPH_BodyCreationSettings* settings = JPH_BodyCreationSettings_Create3(
    shape, (const JPH_Vec3*)&position, (const JPH_Quat*)&rotation,
    JPH_MotionType_Static, layer);

  const JPH_BodyID id = JPH_BodyInterface_CreateAndAddBody(world->bodyInterface,
                                                           settings, activate);

  JPH_BodyCreationSettings_Destroy(settings);

  return id;
}

void PhysicsWorldDestroy(PhysicsWorld *world)
{
  JPH_JobSystem_Destroy(world->jobSystem);
	JPH_PhysicsSystem_Destroy(world->system);
	JPH_Shutdown();
}
