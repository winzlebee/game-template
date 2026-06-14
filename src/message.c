
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

#define QUAT_SMALLEST_MAX 0.7071068f

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
  NBN_SerializeUInt (stream, msg->lastReceivedTick, 0, UINT_MAX);
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

static void SerializeQuaternion(NBN_Stream *stream, Quaternion *quaternion)
{
  // Encode: Only meaningful on write
  float comps[4] = {quaternion->x, quaternion->y, quaternion->z, quaternion->w};

  uint32_t largestCoeff = 0;

  for (int i = 1; i < 4; i++) {
    if (fabsf(comps[i]) > fabsf(comps[largestCoeff])) {
      largestCoeff = i;
    }
  }

  // Flip the whole quaternion so the dropped component is positive
  float sign = comps[largestCoeff] < 0.0f ? -1.0f : 1.0f;
  float coeffA = Clamp(sign * comps[(largestCoeff + 1) % 4], -QUAT_SMALLEST_MAX, QUAT_SMALLEST_MAX);
  float coeffB = Clamp(sign * comps[(largestCoeff + 2) % 4], -QUAT_SMALLEST_MAX, QUAT_SMALLEST_MAX);
  float coeffC = Clamp(sign * comps[(largestCoeff + 3) % 4], -QUAT_SMALLEST_MAX, QUAT_SMALLEST_MAX);

  NBN_SerializeUInt (stream, largestCoeff, 0, 3);
  NBN_SerializeFloat(stream, coeffA, -1.0f, 1.0f, 3);
  NBN_SerializeFloat(stream, coeffB, -1.0f, 1.0f, 3);
  NBN_SerializeFloat(stream, coeffC, -1.0f, 1.0f, 3);

  // Decode: Only meaningful on read. Re-derive the missing coefficient.
  const float dropped = sqrtf(fmaxf(
    0.0f, 1.0f - (coeffA * coeffA) - (coeffB * coeffB) - (coeffC * coeffC)));

  comps[largestCoeff]           = dropped;
  comps[(largestCoeff + 1) % 4] = coeffA;
  comps[(largestCoeff + 2) % 4] = coeffB;
  comps[(largestCoeff + 3) % 4] = coeffC;

  quaternion->x = comps[0];
  quaternion->y = comps[1];
  quaternion->z = comps[2];
  quaternion->w = comps[3];
}

static void SerializePose(NBN_Stream *stream, Vector3 *position, Quaternion *rotation)
{
  NBN_SerializeFloat(stream, position->x, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeFloat(stream, position->y, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
  NBN_SerializeFloat(stream, position->z, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);

  SerializeQuaternion(stream, rotation);
}

int PhysicsStateMessage_Serialize(PhysicsStateMessage *msg, NBN_Stream *stream)
{
  NBN_SerializeUInt(stream, msg->tick, 0, UINT_MAX);
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
      case PST_CAPSULE:
        NBN_SerializeFloat(stream, s->shapeParams.cyl.radius, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
        NBN_SerializeFloat(stream, s->shapeParams.cyl.halfLength, MIN_FLOAT_VAL, MAX_FLOAT_VAL, 3);
        break;
    }

    NBN_SerializeUInt(stream, s->meshIndex, 0, UINT_MAX);
    NBN_SerializeUInt(stream, s->animIndex, 0, UCHAR_MAX);

    SerializePose(stream, &s->position, &s->rotation);
  }

  return 0;
}

PhysicsDeltaMessage *PhysicsDeltaMessage_Create(void)
{
  return malloc(sizeof(PhysicsDeltaMessage));
}

void PhysicsDeltaMessage_Destroy(PhysicsDeltaMessage *msg)
{
  free(msg);
}

int PhysicsDeltaMessage_Serialize(PhysicsDeltaMessage *msg, NBN_Stream *stream)
{
  NBN_SerializeUInt(stream, msg->tick, 0, UINT_MAX);
  NBN_SerializeUInt(stream, msg->baselineTick, 0, UINT_MAX);
  NBN_SerializeUInt(stream, msg->entityCount, 0, MAX_ENTITIES);

  for (uint32_t i = 0; i < msg->entityCount; i++) {
    NBN_SerializeUInt(stream, msg->netIds[i], 0, UINT_MAX);
    NBN_SerializeUInt(stream, msg->animations[i], 0, UCHAR_MAX);
    SerializePose(stream, &msg->positions[i], &msg->rotations[i]);
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
