#pragma once

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

#define TICK_RATE 60
#define MAX_CLIENTS 4
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
};

typedef struct {
  float sX, sY, sZ;
  NBN_ConnectionHandle handle;
} SpawnClientMessage;

typedef struct {
  float x, y, z;
  float val;
} UpdateStateMessage;

// Client state, represents a client over the network
typedef struct {
  uint32_t handle;

  float x, y, z;
  float val;
  
} ClientState;

typedef struct {
  unsigned int client_count;
  ClientState client_states[MAX_CLIENTS];
} GameStateMessage;

// Store all options from the command line
typedef struct {
  float packet_loss;
  float packet_duplication;
  float ping;
  float jitter;
} Options;

SpawnClientMessage* SpawnClientMessage_Create(void);
void SpawnClientMessage_Destroy(SpawnClientMessage*);
int SpawnClientMessage_Serialize(SpawnClientMessage*, NBN_Stream*);

UpdateStateMessage* UpdateStateMessage_Create(void);
void UpdateStateMessage_Destroy(UpdateStateMessage*);
int UpdateStateMessage_Serialize(UpdateStateMessage*, NBN_Stream*);

GameStateMessage* GameStateMessage_Create(void);
void GameStateMessage_Destroy(GameStateMessage*);
int GameStateMessage_Serialize(GameStateMessage*, NBN_Stream*);

int ReadCommandLine(int, char*[]);
Options GetOptions(void);

