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

typedef struct {
  Matrix   matrix;
  Matrix   prevMatrix;
  uint32_t meshIndex;
  bool     alive;
} RenderSlot;

static RenderSlot g_Render[MAX_ENTITIES];
static Model      g_Meshes[MESH_COUNT];
static bool       g_MeshesLoaded;

static bool LoadMeshes(void)
{
  for (uint32_t i = 0; i < MESH_COUNT; i++) {
    g_Meshes[i] = LoadModel(g_MeshFileNames[i]);
    if (g_Meshes[i].meshCount == 0) {
      TraceLog(LOG_WARNING, "Failed to load: %s", g_MeshFileNames[i]);
      g_Meshes[i] = LoadModelFromMesh(GenMeshCube(0.5f, 0.5f, 0.5f));
    }
  }
  return true;
}

static void UnloadMeshes(void)
{
  for (uint32_t i = 0; i < MESH_COUNT; i++) {
    UnloadModel(g_Meshes[i]);
  }
}

static void RenderSlotsClear(void)
{
  memset(g_Render, 0, sizeof(g_Render));
}

typedef struct {
  uint32_t netId;
  bool     connected;
  bool     disconnected;
} Client;

static Client g_Client;
static double g_LastTime;

static Vector3 GetPlayerPosition(void)
{
  if (!g_Client.connected) {
    return Vector3Zero();
  }

  RenderSlot *slot = &g_Render[g_Client.netId];

  if (!slot->alive) {
    return Vector3Zero();
  }

  const Vector3 previous = (Vector3){slot->prevMatrix.m12, slot->prevMatrix.m13, slot->prevMatrix.m14};
  const Vector3 current  = (Vector3){slot->matrix.m12, slot->matrix.m13, slot->matrix.m14};
  const float elapsed    = (float) (GetTime() - g_LastTime);
  const float singleTick = 1.0f / (float) TICK_RATE;

  return Vector3Lerp(previous, current, Clamp(elapsed / singleTick, 0.0f, 1.0f));
}

static void HandleConnectionAccepted(void)
{
  uint8_t data[NBN_SERVER_DATA_MAX_SIZE];
  unsigned int len = NBN_GameClient_ReadServerData(data);

  RenderSlotsClear();

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
  for (uint32_t i = 0; i < msg->entityCount; i++) {
    PhysicsEntityState *state = &msg->entities[i];
    RenderSlot *slot = &g_Render[state->netId];

    slot->prevMatrix = slot->matrix;
    slot->matrix     = state->transform;
    slot->meshIndex  = state->meshIndex;
    slot->alive      = true;
  }

  g_LastTime = GetTime();

  PhysicsStateMessage_Destroy(msg);
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
      }

      break;
    }
    default:
      break;
  }
}

static Matrix MatrixLerp(Matrix matA, Matrix matB, float t)
{
  Vector3 translationA, scaleA;
  Vector3 translationB, scaleB;

  Quaternion rotationA;
  Quaternion rotationB;

  MatrixDecompose(matA, &translationA, &rotationA, &scaleA);
  MatrixDecompose(matB, &translationB, &rotationB, &scaleB);

  Vector3 translation = Vector3Lerp(translationA, translationB, t);
  Quaternion rotation = QuaternionNlerp(rotationA, rotationB, t);
  Vector3 scale       = Vector3Lerp(scaleA, scaleB, t);

  return MatrixCompose(translation, rotation, scale);
}

static void DrawEntity(uint32_t netId, RenderSlot *slot, float alpha)
{
  const Matrix interp = MatrixLerp(slot->prevMatrix, slot->matrix, alpha);

  rlPushMatrix();
  rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(interp));
  DrawModel(g_Meshes[slot->meshIndex], Vector3Zero(), 1.0f, WHITE);
  rlPopMatrix();
}

static float InterpolationAlpha(void)
{
  double now        = GetTime();
  float  serverTick = 1.0f / (float)TICK_RATE;
  double elapsed    = now - g_LastTime;
  return fminf((float)(elapsed / serverTick), 1.0f);
}

static void RenderAll(void)
{
  float alpha = InterpolationAlpha();

  for (uint32_t i = 0; i < MAX_ENTITIES; i++) {
    if (!g_Render[i].alive) {
      continue;
    }

    DrawEntity(i, &g_Render[i], alpha);
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
  g_MeshesLoaded = LoadMeshes();

  if (NBN_GameClient_StartEx(GROWTH_PROTOCOL_NAME, "127.0.0.1", GROWTH_PORT, NULL, 0) < 0) {
    TraceLog(LOG_ERROR, "Game client failed to start. Exit");
    return 1;
  }

  NBN_GameClient_RegisterMessage(UPDATE_STATE_MESSAGE,
      (NBN_MessageBuilder)UpdateStateMessage_Create,
      (NBN_MessageDestructor)UpdateStateMessage_Destroy,
      (NBN_MessageSerializer)UpdateStateMessage_Serialize);
  NBN_GameClient_RegisterMessage(PHYSICS_STATE_MESSAGE,
      (NBN_MessageBuilder)PhysicsStateMessage_Create,
      (NBN_MessageDestructor)PhysicsStateMessage_Destroy,
      (NBN_MessageSerializer)PhysicsStateMessage_Serialize);

  NBN_GameClient_SetPing(GetOptions().ping);
  NBN_GameClient_SetJitter(GetOptions().jitter);
  NBN_GameClient_SetPacketLoss(GetOptions().packet_loss);
  NBN_GameClient_SetPacketDuplication(GetOptions().packet_duplication);

  RenderSlotsClear();
  memset(&g_Client, 0, sizeof(g_Client));

  OrbitCamera oc;
  OrbitCamera_Init(&oc);
  Camera3D camera;

  g_LastTime = GetTime();

  while (!WindowShouldClose()) {
    int ev;

    while ((ev = NBN_GameClient_Poll()) != NBN_NO_EVENT) {
      HandleClientEvent(ev);
    }

    if (g_Client.connected) {
      Vector3 input = OrbitCamera_GetInput(&camera, &g_Render[g_Client.netId].matrix);

      UpdateStateMessage *umsg = UpdateStateMessage_Create();
      umsg->x   = input.x;
      umsg->y   = input.y;
      umsg->z   = input.z;
      umsg->val = IsKeyDown(KEY_SPACE) ? 1.0f : 0.0f;

      NBN_GameClient_SendUnreliableMessage(UPDATE_STATE_MESSAGE, umsg);
    }

    NBN_GameClient_SendPackets();

    Vector3 playerPos = GetPlayerPosition();
    OrbitCamera_Update(&oc, playerPos);
    OrbitCamera_ToCamera3D(&oc, playerPos, &camera);

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
