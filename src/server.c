#include "ecs.h"
#include "message.h"
#include "mesh_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// --- Constants --------------------------------------------------------------

#define FLOOR_HALF_EXTENT 50.0f
#define PLAYER_SPEED       5.0f
#define JUMP_SPEED         6.0f

typedef struct {
  NBN_ConnectionHandle handle;
  Entity   entity;
  Vector3  input;   // latest movement input from client
  int      jump;    // latest jump flag
} ClientSlot;

static ClientSlot  g_Clients[MAX_CLIENTS];
static uint32_t    g_ClientCount;
static ECSWorld    g_World;

static Vector3 g_SpawnPoints[] = {
    (Vector3){ -FLOOR_HALF_EXTENT / 2.0f + 1.5f, 0, -FLOOR_HALF_EXTENT / 2.0f + 1.5f },
    (Vector3){  FLOOR_HALF_EXTENT / 2.0f - 1.5f, 0, -FLOOR_HALF_EXTENT / 2.0f + 1.5f },
    (Vector3){ -FLOOR_HALF_EXTENT / 2.0f + 1.5f, 0,  FLOOR_HALF_EXTENT / 2.0f - 1.5f },
    (Vector3){  FLOOR_HALF_EXTENT / 2.0f - 1.5f, 0,  FLOOR_HALF_EXTENT / 2.0f - 1.5f },
};

// --- Helpers ----------------------------------------------------------------

static void AcceptConnection(Vector3 spawn, NBN_ConnectionHandle handle, uint32_t netId)
{
  NBN_WriteStream ws;
  uint8_t data[sizeof(SpawnClientMessage)];

  NBN_WriteStream_Init(&ws, data, sizeof(SpawnClientMessage));

  SpawnClientMessage scm = {
    .sX = spawn.x, .sY = spawn.y, .sZ = spawn.z,
    .netId = netId,
    .handle = handle
  };
  SpawnClientMessage_Serialize(&scm, (NBN_Stream *)&ws);
  NBN_GameServer_AcceptIncomingConnectionWithData(data, sizeof(data));
}

static ClientSlot *FindSlotByHandle(NBN_ConnectionHandle h)
{
  for (uint32_t i = 0; i < MAX_CLIENTS; i++)
    if (g_Clients[i].handle == h) return &g_Clients[i];
  return NULL;
}

static ClientSlot *FindEmptySlot(void)
{
  for (uint32_t i = 0; i < MAX_CLIENTS; i++)
    if (g_Clients[i].handle == EMPTY_SLOT) return &g_Clients[i];
  return NULL;
}

// --- Server events ----------------------------------------------------------

static void HandleNewConnection(void)
{
  TraceLog(LOG_INFO, "New connection");

  if (g_ClientCount == MAX_CLIENTS) {
    TraceLog(LOG_INFO, "Connection rejected. Server is full.");
    NBN_GameServer_RejectIncomingConnectionWithCode(SERVER_FULL_CODE);
    return;
  }

  NBN_ConnectionHandle handle = NBN_GameServer_GetIncomingConnection();
  Vector3 spawn = Vector3Zero();//g_SpawnPoints[handle % MAX_CLIENTS];

  Entity e = ECS_CreateEntity(&g_World);
  uint32_t netId = ECS_GetNetId(&g_World, e);

  AcceptConnection(spawn, handle, netId);

  ECS_AddCharacter(&g_World, e, spawn, PLAYER_SPEED, JUMP_SPEED);

  uint32_t meshIndex = (uint32_t)(rand() % MESH_COUNT);
  ECS_AddMesh(&g_World, e, meshIndex);

  ClientSlot *slot = FindEmptySlot();
  memset(slot, 0, sizeof(*slot));
  slot->handle = handle;
  slot->entity = e;
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

// Buffer client input — do NOT step the character here. We apply all
// buffered inputs at a fixed rate in the main loop.
static void HandleReceivedMessage(void)
{
  NBN_MessageInfo info = NBN_GameServer_GetMessageInfo();
  ClientSlot *slot = FindSlotByHandle(info.sender);
  if (!slot) return;

  switch (info.type) {
    case UPDATE_STATE_MESSAGE: {
      UpdateStateMessage *msg = info.data;
      slot->input = (Vector3){msg->x, 0.0f, msg->z};
      slot->jump  = (msg->val > 0.5f) ? 1 : 0;
      UpdateStateMessage_Destroy(msg);
      break;
    }
  }
}

static int HandleGameServerEvent(int ev)
{
  switch (ev) {
    case NBN_NEW_CONNECTION:       HandleNewConnection();        break;
    case NBN_CLIENT_DISCONNECTED:  HandleClientDisconnection();  break;
    case NBN_CLIENT_MESSAGE_RECEIVED: HandleReceivedMessage();   break;
  }
  return 0;
}

// --- Tick: apply buffered input to all characters ---------------------------

static void ApplyAllCharacterInputs(float delta)
{
  for (uint32_t i = 0; i < MAX_CLIENTS; i++) {

    if (g_Clients[i].handle == EMPTY_SLOT) {
      continue;
    }

    ECS_UpdateCharacter(&g_World, g_Clients[i].entity, delta,
                        g_Clients[i].input, g_Clients[i].jump);
    // Clear one-shot jump
    g_Clients[i].jump = 0;
  }
}

// --- Snapshot broadcast -----------------------------------------------------

typedef struct {
  ECSWorld *world;
  PhysicsStateMessage *msg;
} SnapshotCtx;

static int SnapshotToMessage(Entity e, ECSWorld *world, void *userdata)
{
  SnapshotCtx *ctx = (SnapshotCtx *)userdata;

  PhysicsEntityState *s = &ctx->msg->entities[ctx->msg->entityCount++];
  s->netId     = ECS_GetNetId(world, e);
  s->meshIndex = world->meshComponents[e].meshIndex;
  s->transform = world->transforms[e].matrix;

  // Mark character entities for the client
  if (world->masks[e] & COMPONENT_CHARACTER) {
    s->shapeType = PST_CYLINDER;
    s->bodyType  = PBT_DYNAMIC;
    s->shapeParams.cyl.radius     = 0.4f;
    s->shapeParams.cyl.halfLength = 0.8f;
  } else {
    PhysicsComponent *pc = &world->physComponents[e];
    s->shapeType   = pc->body.type;
    s->bodyType    = pc->type;
    s->shapeParams = pc->body.params;
  }

  return 0;
}

static void BroadcastPhysicsState(void)
{
  for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
    if (g_Clients[i].handle == EMPTY_SLOT) continue;

    PhysicsStateMessage *msg = PhysicsStateMessage_Create();
    msg->entityCount = 0;

    SnapshotCtx ctx = {.world = &g_World, .msg = msg};
    ECS_ForEach(&g_World, COMPONENT_TRANSFORM | COMPONENT_MESH, SnapshotToMessage, &ctx);

    NBN_GameServer_SendUnreliableMessageTo(g_Clients[i].handle, PHYSICS_STATE_MESSAGE, msg);
  }
}

// --- Main loop --------------------------------------------------------------

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
      UPDATE_STATE_MESSAGE, (NBN_MessageBuilder)UpdateStateMessage_Create,
      (NBN_MessageDestructor)UpdateStateMessage_Destroy,
      (NBN_MessageSerializer)UpdateStateMessage_Serialize);
  NBN_GameServer_RegisterMessage(
      PHYSICS_STATE_MESSAGE, (NBN_MessageBuilder)PhysicsStateMessage_Create,
      (NBN_MessageDestructor)PhysicsStateMessage_Destroy,
      (NBN_MessageSerializer)PhysicsStateMessage_Serialize);

  NBN_GameServer_SetPing(GetOptions().ping);
  NBN_GameServer_SetJitter(GetOptions().jitter);
  NBN_GameServer_SetPacketLoss(GetOptions().packet_loss);
  NBN_GameServer_SetPacketDuplication(GetOptions().packet_duplication);

  const float delta = 1.f / TICK_RATE;

  ECS_Init(&g_World);

  {
    Entity floorEntity = ECS_CreateEntity(&g_World);
    PhysicsBody body = {0};
    body.type = PST_BOX;
    float floorHalfY = 0.5f;
    body.params.extents = (Vector3){FLOOR_HALF_EXTENT, floorHalfY, FLOOR_HALF_EXTENT};
    body.transform = MatrixTranslate(0.0f, -floorHalfY, 0.0f);
    ECS_AddPhysics(&g_World, floorEntity, body, PBT_STATIC);
  }

  for (uint32_t i = 0; i < MAX_CLIENTS; i++) {
    g_Clients[i].handle = EMPTY_SLOT;
  }

  while (running) {

    int ev;
    while ((ev = NBN_GameServer_Poll()) != NBN_NO_EVENT) {

      if (ev < 0) {
        running = false;
        break;
      }

      HandleGameServerEvent(ev);
    }

    ApplyAllCharacterInputs(delta);

    ECS_Update(&g_World, delta);
    BroadcastPhysicsState();
    NBN_GameServer_SendPackets();

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
