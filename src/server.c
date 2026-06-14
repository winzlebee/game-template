#include "ecs.h"
#include "message.h"
#include "mesh_manifest.h"

#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define FLOOR_HALF_EXTENT 50.0f
#define PLAYER_SPEED       5.0f
#define JUMP_SPEED         6.0f
#define DELTA_EPSILON      1e-3f

typedef struct {
  NBN_ConnectionHandle handle;
  Entity   entity;
  Vector3  input;
  bool     jump;
} ClientSlot;

typedef struct {
  uint32_t ackTick;
} ClientTracking;

// Complete snapshot of every entity.
typedef struct {
  // Zero represents an empty snapshot slot
  uint32_t tick;
  uint32_t entityCount;
  PhysicsEntityState entities[MAX_ENTITIES];
} Snapshot;

static ClientSlot  g_Clients[MAX_CLIENTS];
static uint32_t    g_ClientCount;
static ECSWorld    g_World;

static Vector3 g_SpawnPoints[] = {
    (Vector3){ -FLOOR_HALF_EXTENT / 2.0f + 1.5f, 0, -FLOOR_HALF_EXTENT / 2.0f + 1.5f },
    (Vector3){  FLOOR_HALF_EXTENT / 2.0f - 1.5f, 0, -FLOOR_HALF_EXTENT / 2.0f + 1.5f },
    (Vector3){ -FLOOR_HALF_EXTENT / 2.0f + 1.5f, 0,  FLOOR_HALF_EXTENT / 2.0f - 1.5f },
    (Vector3){  FLOOR_HALF_EXTENT / 2.0f - 1.5f, 0,  FLOOR_HALF_EXTENT / 2.0f - 1.5f },
};

// Information about which was the last snapshot each client received
static ClientTracking g_ClientTracking[MAX_CLIENTS];

// Ring buffer of *complete* snapshots for the last SNAPSHOT_BUFFER_SIZE ticks
static Snapshot g_Snapshots[SNAPSHOT_BUFFER_SIZE];

// This starts at one because zero-ticks are used to represent unallocated
// snapshot slots in both the server and client.
static uint32_t g_Tick = 1;

static void AcceptConnection(Vector3 spawn, NBN_ConnectionHandle handle, uint32_t netId)
{
  NBN_WriteStream writeStream;
  uint8_t data[sizeof(SpawnClientMessage)];

  NBN_WriteStream_Init(&writeStream, data, sizeof(SpawnClientMessage));

  SpawnClientMessage scm = {
    .sX = spawn.x, .sY = spawn.y, .sZ = spawn.z,
    .netId = netId,
    .handle = handle
  };

  SpawnClientMessage_Serialize(&scm, (NBN_Stream *)&writeStream);
  NBN_GameServer_AcceptIncomingConnectionWithData(data, sizeof(data));
}

static ClientSlot *FindSlotByHandle(NBN_ConnectionHandle handle)
{
  for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
    if (g_Clients[i].handle == handle) {
      return &g_Clients[i];
    }
  }

  return NULL;
}

static ClientSlot *FindEmptySlot(void)
{
  for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
    if (g_Clients[i].handle == EMPTY_SLOT) {
      return &g_Clients[i];
    }
  }
  return NULL;
}

static ClientTracking *FindTrackingByHandle(NBN_ConnectionHandle handle)
{
  for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
    if (g_Clients[i].handle == handle) {
      return &g_ClientTracking[i];
    }
  }

  return NULL;
}

static bool PoseChanged(const PhysicsEntityState *seA, const PhysicsEntityState *seB)
{
  return fabsf(seA->position.x - seB->position.x) > DELTA_EPSILON
      || fabsf(seA->position.y - seB->position.y) > DELTA_EPSILON
      || fabsf(seA->position.z - seB->position.z) > DELTA_EPSILON
      || fabsf(seA->rotation.x - seB->rotation.x) > DELTA_EPSILON
      || fabsf(seA->rotation.y - seB->rotation.y) > DELTA_EPSILON
      || fabsf(seA->rotation.z - seB->rotation.z) > DELTA_EPSILON
      || fabsf(seA->rotation.w - seB->rotation.w) > DELTA_EPSILON;
}

static void HandleNewConnection(void)
{
  TraceLog(LOG_INFO, "New connection");

  if (g_ClientCount == MAX_CLIENTS) {
    TraceLog(LOG_INFO, "Connection rejected. Server is full.");
    NBN_GameServer_RejectIncomingConnectionWithCode(SERVER_FULL_CODE);
    return;
  }

  NBN_ConnectionHandle handle = NBN_GameServer_GetIncomingConnection();

  const Vector3 spawnPoint = g_SpawnPoints[handle % MAX_CLIENTS];
  const uint32_t meshIndex = (uint32_t)(rand() % MESH_COUNT);

  const Entity entityId = ECS_CreateEntity(&g_World);
  const uint32_t netId  = ECS_GetNetId(&g_World, entityId);

  AcceptConnection(spawnPoint, handle, netId);

  ECS_AddCharacter(&g_World, entityId, spawnPoint, PLAYER_SPEED, JUMP_SPEED);
  ECS_AddMesh(&g_World, entityId, meshIndex);

  ClientSlot *slot = FindEmptySlot();
  memset(slot, 0, sizeof(*slot));
  slot->handle = handle;
  slot->entity = entityId;

  // Reset tracking for this client slot so the first message is a full send
  memset(&g_ClientTracking[slot - g_Clients], 0, sizeof(ClientTracking));

  g_ClientCount++;

  TraceLog(LOG_INFO, "Player connected (handle: %d, netId: %u, mesh: %u)",
           handle, netId, meshIndex);
}

static void HandleClientDisconnection(void)
{
  NBN_ConnectionHandle handle = NBN_GameServer_GetDisconnectedClient();
  TraceLog(LOG_INFO, "Client disconnected (handle: %d)", handle);

  ClientSlot *slot = FindSlotByHandle(handle);
  if (slot) {
    ECS_DestroyEntity(&g_World, slot->entity);
    slot->handle = EMPTY_SLOT;
    slot->entity = INVALID_ENTITY;
    g_ClientCount--;
  }
}

static void HandleReceivedMessage(void)
{
  NBN_MessageInfo info = NBN_GameServer_GetMessageInfo();
  ClientSlot *slot = FindSlotByHandle(info.sender);

  if (!slot) {
    // The client sent a message for a slot that doesn't exist.
    // Either we have a bug, or they're an untrusted client...
    return;
  }

  switch (info.type) {
    case UPDATE_STATE_MESSAGE: {
      PlayerInputMessage *msg = info.data;
      slot->input = (Vector3){msg->x, 0.0f, msg->z};
      slot->jump  = msg->jump;

      // Record the snapshot ack. Reject ticks we have not produced yet:
      // either we have a bug, or they're an untrusted client...
      ClientTracking *track = FindTrackingByHandle(info.sender);

      assert(msg->lastReceivedTick < g_Tick &&
             "Client sent an ACK newer than the server's latest one");

      if (msg->lastReceivedTick > track->ackTick && msg->lastReceivedTick < g_Tick) {
        track->ackTick = msg->lastReceivedTick;
      }

      PlayerInputMessage_Destroy(msg);
      break;
    }
    default:
      assert(false && "Unknown message from client");
  }
}

static int HandleGameServerEvent(int event)
{
  switch (event) {
    case NBN_NEW_CONNECTION:       HandleNewConnection();        break;
    case NBN_CLIENT_DISCONNECTED:  HandleClientDisconnection();  break;
    case NBN_CLIENT_MESSAGE_RECEIVED: HandleReceivedMessage();   break;
    default: assert(false && "Unknown network event");
  }
  return 0;
}

static void ApplyAllCharacterInputs(float delta)
{
  for (uint32_t i = 0; i < MAX_CLIENTS; i++) {

    if (g_Clients[i].handle == EMPTY_SLOT) {
      continue;
    }

    ECS_UpdateCharacter(&g_World, g_Clients[i].entity, delta,
                        g_Clients[i].input, g_Clients[i].jump);
  }
}

static Snapshot *TakeSnapshot(void)
{
  Snapshot *snap = &g_Snapshots[g_Tick % SNAPSHOT_BUFFER_SIZE];

  snap->tick = g_Tick;
  snap->entityCount = 0;

  for (Entity entityId = 0; entityId < MAX_ENTITIES; entityId++) {
    if ((g_World.masks[entityId] & RENDERABLE_MASK) != RENDERABLE_MASK) {
      continue;
    }

    PhysicsEntityState *snapshot = &snap->entities[snap->entityCount++];
    Vector3 scale;

    snapshot->netId     = g_World.netIds[entityId];
    snapshot->meshIndex = g_World.meshComponents[entityId].meshIndex;
    snapshot->animIndex = g_World.meshComponents[entityId].animIndex;

    MatrixDecompose(g_World.transforms[entityId].matrix,
                    &snapshot->position, &snapshot->rotation,
                    &scale);

    // Explicitly mark character entities for the client
    if (g_World.masks[entityId] & COMPONENT_CHARACTER) {
      snapshot->shapeType = PST_CYLINDER;
      snapshot->bodyType  = PBT_DYNAMIC;
      snapshot->shapeParams.cyl.radius     = 0.75f;
      snapshot->shapeParams.cyl.halfLength = 1.0f;
    } else {
      PhysicsComponent *physics = &g_World.physComponents[entityId];
      snapshot->shapeType   = physics->shape.type;
      snapshot->bodyType    = physics->body.type;
      snapshot->shapeParams = physics->shape.params;
    }
  }

  return snap;
}

static const Snapshot *FindBaseline(const ClientTracking *track)
{
  if (track->ackTick == 0 || g_Tick - track->ackTick >= SNAPSHOT_BUFFER_SIZE) {
    return NULL;
  }

  const Snapshot *snap = &g_Snapshots[track->ackTick % SNAPSHOT_BUFFER_SIZE];
  return snap->tick == track->ackTick ? snap : NULL;
}

// Snapshots list entities in ECS order, so an unchanged entity set yields
// identical lists and entities at equal indices correspond to each other.
static bool SameEntitySet(const Snapshot *snapA, const Snapshot *snapB)
{
  if (snapA->entityCount != snapB->entityCount) {
    return false;
  }

  for (uint32_t i = 0; i < snapA->entityCount; i++) {
    if (snapA->entities[i].netId != snapB->entities[i].netId) {
      return false;
    }
  }

  return true;
}

static void SendFullState(NBN_ConnectionHandle handle, const Snapshot *current)
{
  PhysicsStateMessage *msg = PhysicsStateMessage_Create();

  msg->tick = current->tick;
  msg->entityCount = current->entityCount;

  memcpy(msg->entities, current->entities, sizeof(current->entities));

  NBN_GameServer_SendUnreliableMessageTo(handle, PHYSICS_STATE_MESSAGE, msg);
}

static void SendDelta(NBN_ConnectionHandle handle, const Snapshot *current, const Snapshot *baseline)
{
  PhysicsDeltaMessage *msg = PhysicsDeltaMessage_Create();

  msg->tick         = current->tick;
  msg->baselineTick = baseline->tick;
  msg->entityCount  = 0;

  for (uint32_t i = 0; i < current->entityCount; i++) {
    if (!PoseChanged(&current->entities[i], &baseline->entities[i])) {
      continue;
    }

    uint32_t seIdx = msg->entityCount++;
    msg->netIds[seIdx]     = current->entities[i].netId;
    msg->animations[seIdx] = current->entities[i].animIndex;
    msg->positions[seIdx]  = current->entities[i].position;
    msg->rotations[seIdx]  = current->entities[i].rotation;
  }

  NBN_GameServer_SendUnreliableMessageTo(handle, PHYSICS_DELTA_MESSAGE, msg);
}

static void BroadcastPhysicsState(const Snapshot *current)
{
  for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
    if (g_Clients[i].handle == EMPTY_SLOT) {
      continue;
    }

    const Snapshot *baseline = FindBaseline(&g_ClientTracking[i]);

    // Spawned/destroyed entities need creation data or removal, both of
    // which only the full snapshot carries.
    if (baseline && SameEntitySet(current, baseline)) {
      SendDelta(g_Clients[i].handle, current, baseline);
    } else {
      SendFullState(g_Clients[i].handle, current);
    }
  }
}

static bool running = true;

int main(int argc, char *argv[])
{
  if (ReadCommandLine(argc, argv)) {
    printf("Usage: growth-server [--packet_loss=<value>] ...\n");
    return 1;
  }

  srand((unsigned int)time(NULL));
  SetTraceLogLevel(LOG_DEBUG);

  NBN_UDP_Register();

  if (NBN_GameServer_StartEx(GROWTH_PROTOCOL_NAME, GROWTH_PORT) < 0) {
    TraceLog(LOG_ERROR, "Game server failed to start. Exit");
    return 1;
  }

  NBN_GameServer_RegisterMessage(
      UPDATE_STATE_MESSAGE, (NBN_MessageBuilder)PlayerInputMessage_Create,
      (NBN_MessageDestructor)PlayerInputMessage_Destroy,
      (NBN_MessageSerializer)PlayerInputMessage_Serialize);
  NBN_GameServer_RegisterMessage(
      PHYSICS_STATE_MESSAGE, (NBN_MessageBuilder)PhysicsStateMessage_Create,
      (NBN_MessageDestructor)PhysicsStateMessage_Destroy,
      (NBN_MessageSerializer)PhysicsStateMessage_Serialize);
  NBN_GameServer_RegisterMessage(
      PHYSICS_DELTA_MESSAGE, (NBN_MessageBuilder)PhysicsDeltaMessage_Create,
      (NBN_MessageDestructor)PhysicsDeltaMessage_Destroy,
      (NBN_MessageSerializer)PhysicsDeltaMessage_Serialize);

  NBN_GameServer_SetPing(GetOptions().ping);
  NBN_GameServer_SetJitter(GetOptions().jitter);
  NBN_GameServer_SetPacketLoss(GetOptions().packet_loss);
  NBN_GameServer_SetPacketDuplication(GetOptions().packet_duplication);

  const float delta = 1.f / TICK_RATE;

  ECS_Init(&g_World);

  {
    Entity floorEntity = ECS_CreateEntity(&g_World);

    PhysicsBody body = {0};
    body.type = PBT_STATIC;

    PhysicsShape shape = {0};
    shape.params.extents = (Vector3){FLOOR_HALF_EXTENT, 0.5f, FLOOR_HALF_EXTENT};
    body.transform = MatrixTranslate(0.0f, -0.5f, 0.0f);

    ECS_AddPhysics(&g_World, floorEntity, body, shape);
  }

  for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
    g_Clients[i].handle = EMPTY_SLOT;
    memset(&g_ClientTracking[i], 0, sizeof(ClientTracking));
  }

  while (running) {

    int networkEvent;

    while ((networkEvent = NBN_GameServer_Poll()) != NBN_NO_EVENT) {

      if (networkEvent < 0) {
        running = false;
        break;
      }

      HandleGameServerEvent(networkEvent);
    }

    ApplyAllCharacterInputs(delta);

    ECS_Update(&g_World, delta);
    BroadcastPhysicsState(TakeSnapshot());
    NBN_GameServer_SendPackets();

    g_Tick++;

#if defined(_WIN32) || defined(_WIN64)
    Sleep((DWORD)(delta * 1000));
#else
    {
      long nanos = (long)(delta * 1e9);
      struct timespec t = {.tv_sec = nanos / 999999999, .tv_nsec = nanos % 999999999};
      nanosleep(&t, &t);
    }
#endif
  }

  NBN_GameServer_Stop();
  return 0;
}
