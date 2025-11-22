
#include <stdlib.h>
#include <limits.h>
#include <getopt.h>

#define NBNET_IMPL 

#include "message.h"

enum
{
  CL_OPT_MESSAGES_COUNT,
  CL_OPT_PACKET_LOSS,
  CL_OPT_PACKET_DUPLICATION,
  CL_OPT_PING,
  CL_OPT_JITTER,
};

static Options options = {0};

SpawnClientMessage* SpawnClientMessage_Create(void)
{
  return malloc(sizeof(SpawnClientMessage));
}

void SpawnClientMessage_Destroy(SpawnClientMessage *msg)
{
  free(msg);
}

int SpawnClientMessage_Serialize(SpawnClientMessage *msg, NBN_Stream *stream)
{
  NBN_SerializeFloat(stream, msg->sX, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeFloat(stream, msg->sY, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeFloat(stream, msg->sZ, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeUInt(stream, msg->handle, 0, UINT_MAX);
  return 0;
}

UpdateStateMessage *UpdateStateMessage_Create(void)
{
  return malloc(sizeof(UpdateStateMessage));
}

void UpdateStateMessage_Destroy(UpdateStateMessage *msg)
{
  free(msg);
}

int UpdateStateMessage_Serialize(UpdateStateMessage *msg, NBN_Stream *stream)
{
  NBN_SerializeFloat(stream, msg->x, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeFloat(stream, msg->y, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeFloat(stream, msg->z, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeFloat(stream, msg->val, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  return 0;
}

GameStateMessage *GameStateMessage_Create(void)
{
  return malloc(sizeof(GameStateMessage));
}

void GameStateMessage_Destroy(GameStateMessage *msg)
{
    free(msg);
}

int GameStateMessage_Serialize(GameStateMessage* msg, NBN_Stream* stream)
{
  NBN_SerializeUInt(stream, msg->client_count, 0, MAX_CLIENTS);

  for (unsigned int i = 0; i < msg->client_count; i++) {
    NBN_SerializeUInt(stream, msg->client_states[i].handle, 0, UINT_MAX);

    NBN_SerializeFloat(stream, msg->client_states[i].x, MIN_FLOAT_VAL,
                       MAX_FLOAT_VAL, 3);
    NBN_SerializeFloat(stream, msg->client_states[i].y, MIN_FLOAT_VAL,
                       MAX_FLOAT_VAL, 3);
    NBN_SerializeFloat(stream, msg->client_states[i].z, MIN_FLOAT_VAL,
                       MAX_FLOAT_VAL, 3);

    NBN_SerializeFloat(stream, msg->client_states[i].val, MIN_FLOAT_VAL,
                       MAX_FLOAT_VAL, 3);
  }

  return 0;
}

// Parse the command line
int ReadCommandLine(int argc, char *argv[])
{
  int opt;
  int option_index;
  struct option long_options[] = {
      {"packet_loss", required_argument, NULL, CL_OPT_PACKET_LOSS},
      {"packet_duplication", required_argument, NULL,
       CL_OPT_PACKET_DUPLICATION},
      {"ping", required_argument, NULL, CL_OPT_PING},
      {"jitter", required_argument, NULL, CL_OPT_JITTER},
  };

  while ((opt = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
    switch (opt) {
      case CL_OPT_PACKET_LOSS:
        options.packet_loss = atof(optarg);
        break;

      case CL_OPT_PACKET_DUPLICATION:
        options.packet_duplication = atof(optarg);
        break;

      case CL_OPT_PING:
        options.ping = atof(optarg);
        break;

      case CL_OPT_JITTER:
        options.jitter = atof(optarg);
        break;

      case '?':
        return -1;

      default:
        return -1;
    }
  }

  return 0;
}

Options GetOptions(void)
{
  return options;
}
