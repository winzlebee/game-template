// Scans .glb files and writes a complete mesh_manifest.h to stdout.
// Usage: anim_scanner <output_file> <source_dir> <glb_file>...
//
// Generated header contains:
//   - MESH_* enum (one entry per .glb file)
//   - g_MeshFileNames lookup table
//   - ANIM_<MODEL>_<ANIMNAME> defines (per-model animation indices)

#include "raylib.h"

#include <stdio.h>
#include <string.h>

// Suppress INFO messages from raylib to keep the build output clean
static void SuppressRaylibLog(int msgType, const char *text, va_list args) {
  (void)text;
  (void)args;
  if (msgType == LOG_INFO || msgType == LOG_DEBUG || msgType == LOG_TRACE)
    return;
  vfprintf(stderr, text, args);
}

// Extract the base name from a file path, upper-cased, dashes → underscores.
// "res/meshes/animal-lion.glb" → "ANIMAL_LION"
static void MakeBaseName(const char *path, char *out, size_t size) {
  const char *slash  = strrchr(path, '/');
  const char *bslash = strrchr(path, '\\');
  const char *name   = slash ? slash + 1 : (bslash ? bslash + 1 : path);

  strncpy(out, name, size - 1);
  out[size - 1] = '\0';

  char *dot = strrchr(out, '.');
  if (dot) *dot = '\0';

  for (char *c = out; *c; c++) {
    if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    else if (*c == '-') *c = '_';
  }
}

// Sanitise a ModelAnimation.name for use as a C identifier suffix.
// "Walk Fast" → "WALK_FAST"
static void MakeAnimName(const char *name, char *out, size_t size) {
  strncpy(out, name, size - 1);
  out[size - 1] = '\0';

  for (char *c = out; *c; c++) {
         if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    else if (*c == ' ' || *c == '-' || *c == '.') *c = '_';
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: anim_scanner <output_file> <source_dir> <glb_file>...\n");
    return 1;
  }

  const char *output_file = argv[1];
  const char *source_dir  = argv[2];
  const int   file_count  = argc - 3;
  char       **files      = &argv[3];

  const size_t source_dir_len = strlen(source_dir);

  SetTraceLogCallback(SuppressRaylibLog);

  FILE *fp = fopen(output_file, "w");
  if (!fp) {
    fprintf(stderr, "Error: cannot open '%s' for writing\n", output_file);
    return 1;
  }

  // ------------------------------------------------------------------
  // Header
  // ------------------------------------------------------------------
  fprintf(fp, "// Auto-generated - do not edit\n");
  fprintf(fp, "#pragma once\n\n");

  // ------------------------------------------------------------------
  // MESH_* enum
  // ------------------------------------------------------------------
  fprintf(fp, "enum {\n");
  for (int i = 0; i < file_count; i++) {
    char base[256];
    MakeBaseName(files[i], base, sizeof(base));
    fprintf(fp, "    MESH_%s,\n", base);
  }
  fprintf(fp, "\n    MESH_COUNT,\n");
  fprintf(fp, "};\n\n");

  // ------------------------------------------------------------------
  // g_MeshFileNames lookup table
  // ------------------------------------------------------------------
  fprintf(fp, "static const char *g_MeshFileNames[] = {\n");
  for (int i = 0; i < file_count; i++) {
    char base[256];
    MakeBaseName(files[i], base, sizeof(base));

    // Strip the source_dir prefix so the path is relative
    const char *rel = files[i];
    if (strncmp(rel, source_dir, source_dir_len) == 0) {
      rel += source_dir_len;
      if (*rel == '/' || *rel == '\\') rel++;
    }

    fprintf(fp, "    [MESH_%s] = \"%s\",\n", base, rel);
  }
  fprintf(fp, "};\n\n");

  // ------------------------------------------------------------------
  // ANIM_* per-model animation index defines
  // ------------------------------------------------------------------
  int any_anims = 0;

  for (int i = 0; i < file_count; i++) {
    char base[256];
    MakeBaseName(files[i], base, sizeof(base));

    int              anim_count = 0;
    ModelAnimation *anims = LoadModelAnimations(files[i], &anim_count);

    if (anim_count <= 0) {
      UnloadModelAnimations(anims, anim_count);
      continue;
    }

    if (!any_anims) {
      fprintf(fp, "// Per-model animation indices - use with MeshComponent.animIndex\n");
      any_anims = 1;
    }

    for (int j = 0; j < anim_count; j++) {
      char anim_name[64];
      MakeAnimName(anims[j].name, anim_name, sizeof(anim_name));

      // Skip empty / invalid names
      if (anim_name[0] == '\0') continue;

      fprintf(fp, "#define ANIM_%s_%s %d\n", base, anim_name, j);
    }

    UnloadModelAnimations(anims, anim_count);
  }

  if (!any_anims) {
    fprintf(fp, "// No animations found in any .glb file\n");
  }

  fprintf(fp, "\n");
  fclose(fp);
  return 0;
}
