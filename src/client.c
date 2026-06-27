#include "ecs.h"
#include "message.h"
#include "mesh_manifest.h"
#include "orbit_camera.h"

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define GAME_WIDTH  1280
#define GAME_HEIGHT 800

#define ANIMATION_FRAME_RATE 60

typedef struct {
  uint32_t netId;
  bool     connected;
  bool     disconnected;
} Client;

typedef struct {
  uint32_t   netId;
  Vector3    position;
  Quaternion rotation;
  uint32_t   meshIndex;
  uint32_t   animIndex;
} RenderSlot;

#define INVALID_RENDER_SLOT UINT32_MAX

typedef struct {
  uint32_t   tick;
  RenderSlot slots[MAX_ENTITIES];
} Snapshot;

static Client g_Client;

static Snapshot g_Snapshots[SNAPSHOT_BUFFER_SIZE];
static uint32_t g_SlotNetId[MAX_ENTITIES];
static uint32_t g_LatestTick;
static double   g_LatestTickTime;

typedef struct {
  ModelAnimation *animations;
  int count;
} ModelAnimationSet;

static Model             g_Meshes[MESH_COUNT];
static ModelAnimationSet g_MeshAnimations[MESH_COUNT];
static bool              g_MeshesLoaded;

static void LoadMeshes(void)
{
  for (uint32_t i = 0; i < MESH_COUNT; i++) {
    g_Meshes[i]                    = LoadModel(g_MeshFileNames[i]);
    g_MeshAnimations[i].animations = LoadModelAnimations(g_MeshFileNames[i], &g_MeshAnimations[i].count);

    if (g_Meshes[i].meshCount == 0) {
      TraceLog(LOG_ERROR, "Failed to load: %s", g_MeshFileNames[i]);
      exit(EXIT_FAILURE);
    }
  }
}

static void UnloadMeshes(void)
{
  for (uint32_t i = 0; i < MESH_COUNT; i++) {
    UnloadModel(g_Meshes[i]);
    UnloadModelAnimations(g_MeshAnimations[i].animations, g_MeshAnimations[i].count);
  }
}

static uint32_t FindRenderSlot(uint32_t netId)
{
  if (netId == 0) {
    return INVALID_RENDER_SLOT;
  }

  for (uint32_t sIdx = 0; sIdx < MAX_ENTITIES; sIdx++) {
    if (g_SlotNetId[sIdx] == netId) {
      return sIdx;
    }
  }

  return INVALID_RENDER_SLOT;
}

static uint32_t FindOrCreateRenderSlot(uint32_t netId)
{
  if (netId == 0) {
    return INVALID_RENDER_SLOT;
  }

  uint32_t freeSlot = INVALID_RENDER_SLOT;

  for (uint32_t slotIdx = 0; slotIdx < MAX_ENTITIES; slotIdx++) {
    if (g_SlotNetId[slotIdx] == netId) {
      return slotIdx;
    }

    if (g_SlotNetId[slotIdx] == 0 && freeSlot == INVALID_RENDER_SLOT) {
      freeSlot = slotIdx;
    }
  }

  if (freeSlot != INVALID_RENDER_SLOT) {
    g_SlotNetId[freeSlot] = netId;
  }

  return freeSlot;
}

static void SnapshotsClear(void)
{
  memset(g_Snapshots, 0, sizeof(g_Snapshots));
  memset(g_SlotNetId, 0, sizeof(g_SlotNetId));
  g_LatestTick = 0;
}

static Snapshot *SnapshotAt(uint32_t tick)
{
  if (tick == 0) {
    return NULL;
  }

  Snapshot *snap = &g_Snapshots[tick % SNAPSHOT_BUFFER_SIZE];

  if (snap->tick != tick) {
    TraceLog(LOG_DEBUG, "SnapshotAt miss: want %u, slot has %u", tick, snap->tick);
    return NULL;
  }

  return snap;
}

static double CurrentRenderTick(void)
{
  const double renderTickAtMessageTime = ((double) g_LatestTick) - INTERP_DELAY_TICKS;
  const double timeSinceLastMessage    = (GetTime() - g_LatestTickTime);
  const double ticksSinceLastMessage   = (timeSinceLastMessage * TICK_RATE);
  const double interpolatedTick        = (renderTickAtMessageTime + ticksSinceLastMessage);
  return fmin(interpolatedTick, g_LatestTick);
}

static void FindBracket(double renderTick, const Snapshot **before, const Snapshot **after)
{
  *before = NULL;
  *after  = NULL;

  for (uint32_t i = 0; i < SNAPSHOT_BUFFER_SIZE; i++) {
    const Snapshot *snap = &g_Snapshots[i];

    if (snap->tick == 0) {
      continue;
    }

    if ((double)snap->tick <= renderTick) {
      if (!*before || snap->tick > (*before)->tick) {
        *before = snap;
      }
    } else {
      if (!*after || snap->tick < (*after)->tick) {
        *after = snap;
      }
    }
  }

  if (!*before) {
    *before = *after;
  }

  if (!*after) {
     *after  = *before;
  }
}

static float BracketAlpha(const Snapshot *before, const Snapshot *after, double renderTick)
{
  if (after->tick <= before->tick) {
    return 0.0f;
  }

  const double alpha = (renderTick - before->tick) / (double)(after->tick - before->tick);
  return Clamp((float)alpha, 0.0f, 1.0f);
}

static Vector3 GetPlayerPosition(void)
{
  if (!g_Client.connected || g_LatestTick == 0) {
    return Vector3Zero();
  }

  const uint32_t slotIdx = FindRenderSlot(g_Client.netId);

  if (slotIdx == INVALID_RENDER_SLOT) {
    return Vector3Zero();
  }

  const double renderTick = CurrentRenderTick();
  const Snapshot *before, *after;
  FindBracket(renderTick, &before, &after);

  if (!before) {
    return Vector3Zero();
  }

  const RenderSlot *prev = &before->slots[slotIdx];
  const RenderSlot *cur  = &after->slots[slotIdx];

  // The slot must hold the same entity in both bracketing snapshots.
  if (prev->netId != g_Client.netId || cur->netId != g_Client.netId) {
    return Vector3Zero();
  }

  return Vector3Lerp(prev->position, cur->position, BracketAlpha(before, after, renderTick));
}

static void HandleConnectionAccepted(void)
{
  uint8_t data[NBN_SERVER_DATA_MAX_SIZE];
  unsigned int len = NBN_GameClient_ReadServerData(data);

  SnapshotsClear();

  NBN_ReadStream stream;
  NBN_ReadStream_Init(&stream, data, len);

  SpawnClientMessage spawn;
  SpawnClientMessage_Serialize(&spawn, (NBN_Stream *)&stream);

  g_Client.netId     = spawn.netId;
  g_Client.connected = true;

  TraceLog(LOG_INFO, "Connected (netId=%u)", g_Client.netId);
}

static void HandleDisconnection(void)
{
  g_Client.connected    = false;
  g_Client.disconnected = true;
}

static void HandlePhysicsState(PhysicsStateMessage *msg)
{
  // Winfried Holzapfel <me@wrhol.com>
  //
  // Note for posterity here. The server keeps track of the entities in
  // the last ack'd message, then sends a full state update again if the
  // set doesn't match for that client. So don't stress reading this
  // code thinking that we'll get a PhysicsDeltaMessage coming through
  // when we haven't ack'd this state update.
  //
  // Quake 3 networking for example makes this state update message reliable
  // and ordered, but I think that's overkill. If this starts to suck, let's
  // just throttle state messages for clients that aren't responding for a while.

  if (msg->tick <= g_LatestTick) {
    PhysicsStateMessage_Destroy(msg);
    return;
  }

  // Clear NetID slots for entities which no longer exist
  for (uint32_t sIdx = 0; sIdx < MAX_ENTITIES; sIdx++) {
    if (g_SlotNetId[sIdx] == 0) {
      continue;
    }

    bool present = false;
    for (uint32_t i = 0; i < msg->entityCount; i++) {
      if (msg->entities[i].netId == g_SlotNetId[sIdx]) {
        present = true;
        break;
      }
    }

    if (!present) {
      g_SlotNetId[sIdx] = 0;
    }
  }

  Snapshot *snap = &g_Snapshots[msg->tick % SNAPSHOT_BUFFER_SIZE];

  memset(snap->slots, 0, sizeof(snap->slots));
  snap->tick = msg->tick;

  for (uint32_t i = 0; i < msg->entityCount; i++) {
    PhysicsEntityState *state = &msg->entities[i];

    const uint32_t slotIdx = FindOrCreateRenderSlot(state->netId);

    if (slotIdx == INVALID_RENDER_SLOT) {
      // The server can't generate this. The only way is if the packet was modified.
      // This check is because we consider packets untrusted.
      continue;
    }

    RenderSlot *slot = &snap->slots[slotIdx];
    slot->netId     = state->netId;
    slot->position  = state->position;
    slot->rotation  = QuaternionNormalize(state->rotation);
    slot->meshIndex = state->meshIndex;
    slot->animIndex = state->animIndex;
  }

  g_LatestTick     = msg->tick;
  g_LatestTickTime = GetTime();

  PhysicsStateMessage_Destroy(msg);
}

static void HandlePhysicsDelta(PhysicsDeltaMessage *msg)
{
  const Snapshot *baseline = SnapshotAt(msg->baselineTick);

  if (msg->tick <= g_LatestTick || !baseline) {
    PhysicsDeltaMessage_Destroy(msg);
    return;
  }

  Snapshot *snap = &g_Snapshots[msg->tick % SNAPSHOT_BUFFER_SIZE];

  if (snap != baseline) {
    memcpy(snap->slots, baseline->slots, sizeof(snap->slots));
  }

  snap->tick = msg->tick;

  for (uint32_t i = 0; i < msg->entityCount; i++) {
    const uint32_t slotIdx = FindOrCreateRenderSlot(msg->netIds[i]);

    if (slotIdx == INVALID_RENDER_SLOT) {
      continue;
    }

    RenderSlot *slot = &snap->slots[slotIdx];
    slot->netId      = msg->netIds[i];
    slot->position   = msg->positions[i];
    slot->rotation   = QuaternionNormalize(msg->rotations[i]);
    slot->animIndex  = msg->animations[i];
  }

  g_LatestTick     = msg->tick;
  g_LatestTickTime = GetTime();

  PhysicsDeltaMessage_Destroy(msg);
}

static void HandleClientEvent(int event)
{
  switch (event) {
    case NBN_NEW_CONNECTION:
      HandleConnectionAccepted();
      break;
    case NBN_CLIENT_DISCONNECTED:
      HandleDisconnection();
      break;
    case NBN_CLIENT_MESSAGE_RECEIVED: {
      NBN_MessageInfo info = NBN_GameClient_GetMessageInfo();

      if (info.type == PHYSICS_STATE_MESSAGE) {
        HandlePhysicsState(info.data);
      } else if (info.type == PHYSICS_DELTA_MESSAGE) {
        HandlePhysicsDelta(info.data);
      }

      break;
    }
    default:
      break;
  }
}

static void DrawEntity(const RenderSlot *prev, const RenderSlot *curr, float alpha)
{
  assert(prev->meshIndex == curr->meshIndex && "Cannot interpolate between different meshes");

  Vector3    position = Vector3Lerp(prev->position, curr->position, alpha);
  Quaternion rotation = QuaternionNlerp(prev->rotation, curr->rotation, alpha);
  Matrix     interp   = MatrixCompose(position, rotation, Vector3One());

  const double animationFrame = GetTime() * ANIMATION_FRAME_RATE;

  if (prev->animIndex != MESH_ANIMATION_NONE && curr->animIndex != MESH_ANIMATION_NONE) {
    const ModelAnimation *animA = &g_MeshAnimations[prev->meshIndex].animations[prev->animIndex];
    const ModelAnimation *animB = &g_MeshAnimations[curr->meshIndex].animations[curr->animIndex];

    const float frameA = (float) fmod(animationFrame, animA->keyframeCount);
    const float frameB = (float) fmod(animationFrame, animB->keyframeCount);

    // Both states have an animation. Blend between the two different animations.
    UpdateModelAnimationEx(g_Meshes[curr->meshIndex], *animA, frameA, *animB, frameB, alpha);
  } else if (curr->animIndex != MESH_ANIMATION_NONE) {
    // The next state has an animation. Just play the new animation immediately. Ideally,
    // we create an idle animation where needed so we're only ever blending between different
    // animations. But this will be the case when we first start the game, for example.
    const ModelAnimation *animation = &g_MeshAnimations[curr->meshIndex].animations[curr->animIndex];
    const float frame = (float) fmod(animationFrame, animation->keyframeCount);
    UpdateModelAnimation(g_Meshes[curr->meshIndex], *animation, frame);
  }

  rlPushMatrix();
  rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(interp));
  DrawModel(g_Meshes[curr->meshIndex], Vector3Zero(), 1.0f, WHITE);
  rlPopMatrix();
}

static void RenderAll(void)
{
  if (g_LatestTick == 0) {
    return;
  }

  const double tick = CurrentRenderTick();
  const Snapshot *before, *after;

  FindBracket(tick, &before, &after);

  if (!before) {
    return;
  }

  float alpha = BracketAlpha(before, after, tick);

  for (uint32_t sIdx = 0; sIdx < MAX_ENTITIES; sIdx++) {
    const RenderSlot *prev = &before->slots[sIdx];
    const RenderSlot *cur  = &after->slots[sIdx];

    // Draw only slots that hold the same live entity in both snapshots. A
    // mismatch means the slot is empty or was reused, so skip it this frame
    // rather than interpolate between two different entities.
    if (cur->netId == 0 || prev->netId != cur->netId) {
      continue;
    }

    DrawEntity(prev, cur, alpha);
  }
}

int main(int argc, char *argv[])
{
  if (ReadCommandLine(argc, argv)) {
    printf("Usage: growth-client [--packet_loss=...] ...\n");
    return 1;
  }

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(GAME_WIDTH, GAME_HEIGHT, "growth by Win Holzapfel");
  DisableCursor();
  SetTargetFPS(120);

  NBN_UDP_Register();
  LoadMeshes();

  if (NBN_GameClient_StartEx(GROWTH_PROTOCOL_NAME, "127.0.0.1", GROWTH_PORT, NULL, 0) < 0) {
    TraceLog(LOG_ERROR, "Game client failed to start. Exit");
    return 1;
  }

  NBN_GameClient_RegisterMessage(UPDATE_STATE_MESSAGE,
      (NBN_MessageBuilder)PlayerInputMessage_Create,
      (NBN_MessageDestructor)PlayerInputMessage_Destroy,
      (NBN_MessageSerializer)PlayerInputMessage_Serialize);

  NBN_GameClient_RegisterMessage(PHYSICS_STATE_MESSAGE,
      (NBN_MessageBuilder)PhysicsStateMessage_Create,
      (NBN_MessageDestructor)PhysicsStateMessage_Destroy,
      (NBN_MessageSerializer)PhysicsStateMessage_Serialize);

  NBN_GameClient_RegisterMessage(PHYSICS_DELTA_MESSAGE,
      (NBN_MessageBuilder)PhysicsDeltaMessage_Create,
      (NBN_MessageDestructor)PhysicsDeltaMessage_Destroy,
      (NBN_MessageSerializer)PhysicsDeltaMessage_Serialize);

  NBN_GameClient_SetPing(GetOptions().ping);
  NBN_GameClient_SetJitter(GetOptions().jitter);
  NBN_GameClient_SetPacketLoss(GetOptions().packet_loss);
  NBN_GameClient_SetPacketDuplication(GetOptions().packet_duplication);

  SnapshotsClear();
  memset(&g_Client, 0, sizeof(g_Client));

  OrbitCamera orbitCamera;
  OrbitCamera_Init(&orbitCamera);
  Camera3D camera;

  double lastInputTime = 0.0;
  bool   jumpQueued    = false;

  while (!WindowShouldClose()) {
    int netEvent;

    while ((netEvent = NBN_GameClient_Poll()) != NBN_NO_EVENT) {
      HandleClientEvent(netEvent);
    }

    if (g_Client.connected) {
      jumpQueued |= IsKeyDown(KEY_SPACE);

      if ((GetTime() - lastInputTime) >= 1.0 / TICK_RATE) {
        Matrix playerMatrix    = MatrixIdentity();
        const Snapshot *latest = SnapshotAt(g_LatestTick);
        const uint32_t slotIdx = FindRenderSlot(g_Client.netId);

        if (latest && slotIdx != INVALID_RENDER_SLOT) {
          const RenderSlot *slot = &latest->slots[slotIdx];
          playerMatrix = MatrixCompose(slot->position, slot->rotation, Vector3One());
        }

        Vector3 input = OrbitCamera_GetInput(&camera, &playerMatrix);

        PlayerInputMessage *umsg = PlayerInputMessage_Create();
        umsg->x    = input.x;
        umsg->y    = input.y;
        umsg->z    = input.z;
        umsg->jump = jumpQueued;
        umsg->lastReceivedTick = g_LatestTick;

        NBN_GameClient_SendUnreliableMessage(UPDATE_STATE_MESSAGE, umsg);

        jumpQueued    = false;
        lastInputTime = GetTime();
      }
    }

    NBN_GameClient_SendPackets();

    Vector3 playerPos = GetPlayerPosition();
    OrbitCamera_Update(&orbitCamera, playerPos);
    OrbitCamera_ToCamera3D(&orbitCamera, playerPos, &camera);

    BeginDrawing();
    ClearBackground(RAYWHITE);
    BeginMode3D(camera);

    RenderAll();
    DrawGrid(10, 1.0f);

    EndMode3D();

    DrawFPS(10, 10);
    DrawText("WASD = move | Space = jump | Mouse = orbit | Scroll = zoom",
             10, 430, 10, GRAY);

    if (g_Client.connected) {
      DrawText(TextFormat("NetID: %u", g_Client.netId), 10, 50, 16, DARKGRAY);
    }

    EndDrawing();
  }

  NBN_GameClient_Stop();
  UnloadMeshes();
  CloseWindow();
  return 0;
}
