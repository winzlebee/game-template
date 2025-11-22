#include "message.h"

#include <stdio.h>
#include <time.h>

typedef struct {
  ClientState clients[MAX_CLIENTS];
  uint32_t clientCount;
} Server;

Server Server_Create(void)
{
  Server s;

  for (uint32_t i = 0; i < MAX_CLIENTS; ++i) {
    s.clients[i].handle = EMPTY_SLOT;
  }

  return s;
}

static Vector3 g_SpawnPoints[] = {
    (Vector3){0, 0, 10},
    (Vector3){10, 0, 20},
    (Vector3){20, 0, 20},
    (Vector3){10, 0, 0},
};

static void AcceptConnection(Vector3 spawn, NBN_ConnectionHandle handle)
{
  NBN_WriteStream ws;
  uint8_t data[sizeof(SpawnClientMessage)];

  NBN_WriteStream_Init(&ws, data, sizeof(SpawnClientMessage));

  SpawnClientMessage scm;
  scm.sX = spawn.x, scm.sY = spawn.y, scm.sZ = spawn.z;
  scm.handle = handle;

  SpawnClientMessage_Serialize(&scm, (NBN_Stream *) &ws);
  NBN_GameServer_AcceptIncomingConnectionWithData(data, sizeof(data));
}

static int HandleNewConnection(Server *server)
{
  TraceLog(LOG_INFO, "New connection");

  if (server->clientCount == MAX_CLIENTS) {
    TraceLog(LOG_INFO, "Connection rejected. Server %p is full.", server);
    NBN_GameServer_RejectIncomingConnectionWithCode(SERVER_FULL_CODE);
    return 0;
  }

  NBN_ConnectionHandle clientHandle;

  clientHandle = NBN_GameServer_GetIncomingConnection();

  const Vector3 spawn = g_SpawnPoints[clientHandle % MAX_CLIENTS];

  AcceptConnection(spawn, clientHandle);

  TraceLog(LOG_INFO, "Connection accepted (ID: %d)", clientHandle);

  ClientState *client = NULL;

  // Find the first empty slot and assign the client to it
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (server->clients[i].handle == EMPTY_SLOT) {
      client = &server->clients[i];
      break;
    }
  }

  *client = (ClientState){.handle = client->handle, .x = 0, .y = 0, .z = 0, .val = 0};

  server->clientCount++;

  return 0;
}

static ClientState *FindClientById(Server *server, uint32_t client_id)
{
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (server->clients[i].handle == client_id) {
      return &server->clients[i];
    }
  }

  return NULL;
}

static void DestroyClient(ClientState *client)
{
  client->handle = EMPTY_SLOT;
}

static void HandleClientDisconnection(Server *server)
{
  NBN_ConnectionHandle handle =
      NBN_GameServer_GetDisconnectedClient();

  TraceLog(LOG_INFO, "Client has disconnected (id: %d)", handle);

  ClientState *client = FindClientById(server, handle);

  DestroyClient(client);

  server->clientCount--;
}

static void HandleUpdateStateMessage(UpdateStateMessage* msg, ClientState *sender)
{
  sender->x = msg->x;
  sender->y = msg->y;
  sender->z = msg->z;

  sender->val = msg->val;

  UpdateStateMessage_Destroy(msg);
}

static void HandleReceivedMessage(Server *server)
{
  // Fetch info about the last received message
  NBN_MessageInfo msg_info = NBN_GameServer_GetMessageInfo();

  // Find the client that sent the message
  ClientState *sender = FindClientById(server, msg_info.sender);

  assert(sender != NULL && "Client that sent the message is not connected to the server");

  switch (msg_info.type) {
    case UPDATE_STATE_MESSAGE:
      // The server received a client state update
      HandleUpdateStateMessage(msg_info.data, sender);
      break;
  }
}

static int HandleGameServerEvent(Server *server, int ev)
{
  switch (ev) {
    case NBN_NEW_CONNECTION:
      // A new client has requested a connection
      if (HandleNewConnection(server) < 0)
        return -1;
      break;

    case NBN_CLIENT_DISCONNECTED:
      // A previously connected client has disconnected
      HandleClientDisconnection(server);
      break;

    case NBN_CLIENT_MESSAGE_RECEIVED:
      // A message from a client has been received
      HandleReceivedMessage(server);
      break;
  }

  return 0;
}

// Broadcasts the latest game state to all connected clients
static int BroadcastGameState(Server *server)
{
  for (int i = 0; i < MAX_CLIENTS; i++) {
    ClientState *client = &server->clients[i];

    if (client->handle == EMPTY_SLOT) {
      // Client isn't connected. No need to send them the client state.
      continue;
    }

    GameStateMessage* msg = GameStateMessage_Create();

    msg->client_count = server->clientCount;

    memcpy(msg->client_states, server->clients,
           sizeof(ClientState) * server->clientCount);

    // Unreliably send the message to all connected clients
    NBN_GameServer_SendUnreliableMessageTo(client->handle, GAME_STATE_MESSAGE,
                                           msg);

    TraceLog(LOG_DEBUG, "Sent game state to client %d (%d, %d)", client->handle, server->clientCount, i);
  }

  return 0;
}

static bool running = true;

int main(int argc, char* argv[])
{
  if (ReadCommandLine(argc, argv)) {
    printf(
        "Usage: growth-server [--packet_loss=<value>] [--packet_duplication=<value>] [--ping=<value>] \
                [--jitter=<value>]\n");

    return 1;
  }

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
      GAME_STATE_MESSAGE, (NBN_MessageBuilder)GameStateMessage_Create,
      (NBN_MessageDestructor)GameStateMessage_Destroy,
      (NBN_MessageSerializer)GameStateMessage_Serialize);

  // Network conditions simulated variables (read from the command line, default is always 0)
  NBN_GameServer_SetPing(GetOptions().ping);
  NBN_GameServer_SetJitter(GetOptions().jitter);
  NBN_GameServer_SetPacketLoss(GetOptions().packet_loss);
  NBN_GameServer_SetPacketDuplication(GetOptions().packet_duplication);

  const float delta = 1.f / TICK_RATE;

  Server server = Server_Create();

  while (running) {
    int ev;

    // Poll for server events
    while ((ev = NBN_GameServer_Poll()) != NBN_NO_EVENT) {
      if (ev < 0) {
        TraceLog(LOG_ERROR, "An occured while polling network events. Exit");

        break;
      }

      if (HandleGameServerEvent(&server, ev) < 0)
        break;
    }

    // Broadcast latest game state
    if (BroadcastGameState(&server) < 0) {
      TraceLog(LOG_ERROR, "An occured while broadcasting game states. Exit");

      break;
    }

    // Pack all enqueued messages as packets and send them
    if (NBN_GameServer_SendPackets() < 0) {
      TraceLog(LOG_ERROR, "An occured while flushing the send queue. Exit");

      break;
    }

    NBN_GameServerStats stats = NBN_GameServer_GetStats();

    TraceLog(LOG_TRACE, "Upload: %f Bps | Download: %f Bps",
             stats.upload_bandwidth, stats.download_bandwidth);

#if defined(_WIN32) || defined(_WIN64)
    Sleep(delta * 1000);
#else
    long nanos = delta * 1e9;

    struct timespec t = {.tv_sec = nanos / 999999999,
                         .tv_nsec = nanos % 999999999};

    nanosleep(&t, &t);
#endif
  }

  // Stop the server
  NBN_GameServer_Stop();

  return 0;
}
