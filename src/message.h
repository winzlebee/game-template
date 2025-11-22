#pragma once

#include <raylib.h>

#include <cstdint>

#define TICK_RATE 60

#define MIN_FLOAT_VAL -5  // Minimum value of networked client float value
#define MAX_FLOAT_VAL 5   // Maximum value of networked client float value

#define MAX_CLIENTS 4

#define SERVER_FULL_CODE 42

struct NBN_Stream;
typedef struct NBN_Stream NBN_Stream;

enum
{
  UPDATE_STATE_MESSAGE,
  GAME_STATE_MESSAGE,
};

typedef struct {
  Vector3 position;
  float val;
} UpdateStateMessage;

// Client state, represents a client over the network
typedef struct {
  uint32_t client_id;
  int x;
  int y;
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

UpdateStateMessage* UpdateStateMessage_Create(void);
void UpdateStateMessage_Destroy(UpdateStateMessage*);
int UpdateStateMessage_Serialize(UpdateStateMessage*, NBN_Stream*);

GameStateMessage* GameStateMessage_Create(void);
void GameStateMessage_Destroy(GameStateMessage*);
int GameStateMessage_Serialize(GameStateMessage*, NBN_Stream*);

int ReadCommandLine(int, char*[]);
Options GetOptions(void);

