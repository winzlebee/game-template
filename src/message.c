
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

static Options g_Options = {0};

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
  NBN_SerializeUInt(stream, msg->netId, 0, UINT32_MAX);
  NBN_SerializeUInt(stream, msg->handle, 0, UINT_MAX);
  return 0;
}

PlayerInputMessage *PlayerInputMessage_Create(void)
{
  return malloc(sizeof(PlayerInputMessage));
}

void PlayerInputMessage_Destroy(PlayerInputMessage *msg)
{
  free(msg);
}

int PlayerInputMessage_Serialize(PlayerInputMessage *msg, NBN_Stream *stream)
{
  NBN_SerializeFloat(stream, msg->x, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeFloat(stream, msg->y, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeFloat(stream, msg->z, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeBool (stream, msg->jump);
  return 0;
}

PhysicsStateMessage *PhysicsStateMessage_Create(void)
{
  return malloc(sizeof(PhysicsStateMessage));
}

void PhysicsStateMessage_Destroy(PhysicsStateMessage *msg)
{
  free(msg);
}

static void SerializeMatrix(NBN_Stream *stream, Matrix *m)
{
  for (int i = 0; i < 16; i++) {
    NBN_SerializeFloat(stream, ((float *)m)[i], MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  }
}

int PhysicsStateMessage_Serialize(PhysicsStateMessage *msg, NBN_Stream *stream)
{
  NBN_SerializeUInt(stream, msg->entityCount, 0, MAX_ENTITIES);

  for (uint32_t i = 0; i < msg->entityCount; i++) {
    PhysicsEntityState *s = &msg->entities[i];

    NBN_SerializeUInt(stream, s->netId, 0, UINT_MAX);
    {
      int bodyType = (int)s->bodyType;
      NBN_SerializeInt(stream, bodyType, 0, PBT_DYNAMIC);
      s->bodyType = (PhysicsBodyType)bodyType;
    }
    {
      int shapeType = (int)s->shapeType;
      NBN_SerializeInt(stream, shapeType, 0, PST_CYLINDER);
      s->shapeType = (PhysicsShapeType)shapeType;
    }

    switch (s->shapeType) {
      case PST_BOX:
        NBN_SerializeFloat(stream, s->shapeParams.extents.x, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
        NBN_SerializeFloat(stream, s->shapeParams.extents.y, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
        NBN_SerializeFloat(stream, s->shapeParams.extents.z, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
        break;
      case PST_SPHERE:
        NBN_SerializeFloat(stream, s->shapeParams.radius, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
        break;
      case PST_CYLINDER:
        NBN_SerializeFloat(stream, s->shapeParams.cyl.radius, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
        NBN_SerializeFloat(stream, s->shapeParams.cyl.halfLength, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
        break;
    }

    NBN_SerializeUInt(stream, s->meshIndex, 0, UINT_MAX);

    SerializeMatrix(stream, &s->transform);
  }

  return 0;
}

// Parse the command line
int ReadCommandLine(int argc, char *argv[])
{
  int opt;
  int option_index;

  struct option long_options[] = {
      {"packet_loss",        required_argument, NULL, CL_OPT_PACKET_LOSS},
      {"packet_duplication", required_argument, NULL, CL_OPT_PACKET_DUPLICATION},
      {"ping",               required_argument, NULL, CL_OPT_PING},
      {"jitter",             required_argument, NULL, CL_OPT_JITTER},
  };

  while ((opt = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
    switch (opt) {
      case CL_OPT_PACKET_LOSS:
        g_Options.packet_loss = atof(optarg);
        break;

      case CL_OPT_PACKET_DUPLICATION:
        g_Options.packet_duplication = atof(optarg);
        break;

      case CL_OPT_PING:
        g_Options.ping = atof(optarg);
        break;

      case CL_OPT_JITTER:
        g_Options.jitter = atof(optarg);
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
  return g_Options;
}
