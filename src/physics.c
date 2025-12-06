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

void PhysicsWorldDestroy(PhysicsWorld *world)
{
  JPH_JobSystem_Destroy(world->jobSystem);
	JPH_PhysicsSystem_Destroy(world->system);
	JPH_Shutdown();
}
