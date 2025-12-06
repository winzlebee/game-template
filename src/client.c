#include "message.h"

#include "raylib.h"

#include <stdio.h>

#define GAME_WIDTH 1280
#define GAME_HEIGHT 800

typedef struct {
  // Current state of the client
  ClientState state;

  // State of all the other connected clients
  ClientState otherClientStates[MAX_CLIENTS - 1];
  uint32_t    otherClientCount;

  // Is the client connected to the server
  bool connected;

  // Has the client been disconnected from the server
  bool disconnected;

  // Has the client spawned on the server yet
  bool spawned;

  // Code the server closed with
  int serverCode;

} Client;


Client Client_Create(void)
{
  Client c;

  for (uint32_t i = 0; i < MAX_CLIENTS - 1; ++i) {
    c.otherClientStates[i].handle = EMPTY_SLOT;
  }

  c.connected = false;
  c.disconnected = false;
  c.spawned = false;
  c.serverCode = -1;

  return c;
}

int main(int argc, char *argv[])
{
  if (ReadCommandLine(argc, argv)) {
    printf(
        "Usage: growth-client [--packet_loss=<value>] [--packet_duplication=<value>] [--ping=<value>] \
                [--jitter=<value>]\n");

    return 1;
  }

  InitWindow(GAME_WIDTH, GAME_HEIGHT, "growth by Win Holzapfel");

  NBN_UDP_Register();

  if (NBN_GameClient_StartEx(GROWTH_PROTOCOL_NAME, "127.0.0.1",
                             GROWTH_PORT, NULL, 0) < 0) {
    TraceLog(LOG_ERROR, "Game client failed to start. Exit");
    return 1;
  }

  NBN_GameClient_RegisterMessage(
      UPDATE_STATE_MESSAGE,
      (NBN_MessageBuilder)UpdateStateMessage_Create,
      (NBN_MessageDestructor)UpdateStateMessage_Destroy,
      (NBN_MessageSerializer)UpdateStateMessage_Serialize);

  NBN_GameClient_RegisterMessage(
      GAME_STATE_MESSAGE,
      (NBN_MessageBuilder)GameStateMessage_Create,
      (NBN_MessageDestructor)GameStateMessage_Destroy,
      (NBN_MessageSerializer)GameStateMessage_Serialize);

  NBN_GameClient_SetPing(GetOptions().ping);
  NBN_GameClient_SetJitter(GetOptions().jitter);
  NBN_GameClient_SetPacketLoss(GetOptions().packet_loss);
  NBN_GameClient_SetPacketDuplication(GetOptions().packet_duplication); 

  Camera camera = {0};
  camera.position = (Vector3){10.0f, 10.0f, 10.0f};
  camera.target = (Vector3){0.0f, 0.0f, 0.0f};
  camera.up = (Vector3){0.0f, 1.0f, 0.0f};
  camera.fovy = 45.0f;
  camera.projection = CAMERA_PERSPECTIVE;

  Vector3 cubePosition = {0.0f, 1.0f, 0.0f};
  Vector3 cubeSize = {2.0f, 2.0f, 2.0f};

  Ray ray = {0};
  RayCollision collision = {0};

  SetTargetFPS(60);

  while (!WindowShouldClose()) {

    if (IsCursorHidden()) {
      UpdateCamera(&camera, CAMERA_FIRST_PERSON);
    }

    // Toggle camera controls
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
      if (IsCursorHidden())
        EnableCursor();
      else
        DisableCursor();
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        
      if (!collision.hit) {
        ray = GetScreenToWorldRay(GetMousePosition(), camera);

        // Check collision between ray and box
        collision = GetRayCollisionBox(
            ray, (BoundingBox){(Vector3){cubePosition.x - cubeSize.x / 2,
                                         cubePosition.y - cubeSize.y / 2,
                                         cubePosition.z - cubeSize.z / 2},
                               (Vector3){cubePosition.x + cubeSize.x / 2,
                                         cubePosition.y + cubeSize.y / 2,
                                         cubePosition.z + cubeSize.z / 2}});
      } else {
        collision.hit = false;
      }
    }

    BeginDrawing();

    ClearBackground(RAYWHITE);

    BeginMode3D(camera);

    if (collision.hit) {
      DrawCube(cubePosition, cubeSize.x, cubeSize.y, cubeSize.z, RED);
      DrawCubeWires(cubePosition, cubeSize.x, cubeSize.y, cubeSize.z, MAROON);

      DrawCubeWires(cubePosition, cubeSize.x + 0.2f, cubeSize.y + 0.2f,
                    cubeSize.z + 0.2f, GREEN);
    } else {
      DrawCube(cubePosition, cubeSize.x, cubeSize.y, cubeSize.z, GRAY);
      DrawCubeWires(cubePosition, cubeSize.x, cubeSize.y, cubeSize.z, DARKGRAY);
    }

    DrawRay(ray, MAROON);
    DrawGrid(10, 1.0f);

    EndMode3D();

    DrawText("Try clicking on the box with your mouse!", 240, 10, 20, DARKGRAY);

    if (collision.hit) {
      DrawText("BOX SELECTED",
               (GAME_WIDTH - MeasureText("BOX SELECTED", 30)) / 2,
               (int)(GAME_HEIGHT * 0.1f), 30, GREEN);
    }

    DrawText("Right click mouse to toggle camera controls", 10, 430, 10, GRAY);

    DrawFPS(10, 10);

    EndDrawing();
  }

  NBN_GameClient_Stop();

  CloseWindow();

  return 0;
}