#pragma once

#include "physics.h"
#include "ecs.h"

#include <raylib.h>
#include <stdint.h>

#define NBN_LogInfo(...) TraceLog(LOG_INFO, __VA_ARGS__)
#define NBN_LogError(...) TraceLog(LOG_ERROR, __VA_ARGS__)
#define NBN_LogWarning(...) TraceLog(LOG_WARNING, __VA_ARGS__)
#define NBN_LogDebug(...) TraceLog(LOG_DEBUG, __VA_ARGS__)
#define NBN_LogTrace(...) TraceLog(LOG_TRACE, __VA_ARGS__)

#include "nbnet.h"
#include "net_drivers/udp.h"

#define GROWTH_PROTOCOL_NAME "growth-net"
#define GROWTH_PORT 42042

#define TICK_RATE 30
#define MAX_CLIENTS 8
#define EMPTY_SLOT ((uint32_t) -1)
#define SERVER_FULL_CODE 42

#define MIN_FLOAT_VAL -10000
#define MAX_FLOAT_VAL  10000

struct NBN_Stream;
typedef struct NBN_Stream NBN_Stream;

enum
{
  UPDATE_STATE_MESSAGE,
  GAME_STATE_MESSAGE,
  PHYSICS_STATE_MESSAGE,
};

typedef struct {
  float sX, sY, sZ;
  uint32_t netId;
  NBN_ConnectionHandle handle;
} SpawnClientMessage;

typedef struct {
  float x, y, z;
  bool jump;
} PlayerInputMessage;

typedef struct {
  uint32_t netId;
  PhysicsShapeType shapeType;
  PhysicsBodyType bodyType;
  PhysicsShapeParams shapeParams;
  uint32_t meshIndex;
  Matrix transform;
} PhysicsEntityState;

typedef struct {
  uint32_t entityCount;
  PhysicsEntityState entities[MAX_ENTITIES];
} PhysicsStateMessage;

typedef struct {
  float packet_loss;
  float packet_duplication;
  float ping;
  float jitter;
} Options;

SpawnClientMessage* SpawnClientMessage_Create(void);
void SpawnClientMessage_Destroy(SpawnClientMessage*);
int SpawnClientMessage_Serialize(SpawnClientMessage*, NBN_Stream*);

PlayerInputMessage* PlayerInputMessage_Create(void);
void PlayerInputMessage_Destroy(PlayerInputMessage*);
int PlayerInputMessage_Serialize(PlayerInputMessage*, NBN_Stream*);

PhysicsStateMessage* PhysicsStateMessage_Create(void);
void PhysicsStateMessage_Destroy(PhysicsStateMessage*);
int PhysicsStateMessage_Serialize(PhysicsStateMessage*, NBN_Stream*);

int ReadCommandLine(int, char*[]);
Options GetOptions(void);
