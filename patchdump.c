/* patchdump.c  -  split apart a patch dump from roland sysex reply
 *
 *  Copyright 2015 Nathan Laredo (laredo@gnu.org)
 *
 * This file may be freely distributed under the terms of
 * the GNU General Public Licence (GPL).
 * 
 */

#include "SDL2/SDL.h"
#include "SDL2/SDL_audio.h"

#include <stdio.h>
#include <signal.h>

#include "debugbreak.h"

/* parse dump from 0xf0 to 0xf7, return pointer to just after last byte */
/*
  patches: 0c 00 01
  0000000: f0 41 10 42 12 0c 00 01 00 01 00 00 50 69 61 6e  .A.B........Pian
  0000010: 6f 20 31 20 20 20 20 20 00 01 01 00 50 69 61 6e  o 1     ....Pian
  drums: 0c 00 02
  0000000: f0 41 10 42 12 0c 00 02 00 01 00 00 53 54 41 4e  .A.B........STAN
  0000010: 44 41 52 44 20 20 20 20 00 01 08 00 52 4f 4f 4d  DARD    ....ROOM
  druminst: 0c 00 03
  *todo*
 */

Uint8 patch_prefix[] = { 0xf0, 0x41, 0x10, 0x42, 0x12, 0x0c, 0x00, 0x01 };
Uint8 drums_prefix[] = { 0xf0, 0x41, 0x10, 0x42, 0x12, 0x0c, 0x00, 0x02 };
Uint8 dinst_prefix[] = { 0xf0, 0x41, 0x10, 0x42, 0x12, 0x0c, 0x00, 0x03 };
typedef enum {
  DtUnknown,
  DtPatch,
  DtDrums,
  DtDInst,
} dump_type;

Uint8 *parse_dump(Uint8 *buf)
{
  dump_type type = DtUnknown;
  if (memcmp(buf, patch_prefix, sizeof(patch_prefix)) == 0) {
    type = DtPatch;
    buf += sizeof(patch_prefix);
  } else if (memcmp(buf, drums_prefix, sizeof(drums_prefix)) == 0) {
    type = DtDrums;
    buf += sizeof(drums_prefix);
  } else if (memcmp(buf, dinst_prefix, sizeof(dinst_prefix)) == 0) {
    type = DtDInst;
    buf += sizeof(dinst_prefix);
  } else {
    fprintf(stderr, "magic bytes not found\n");
    exit(1);
  }
  while (buf[1] == 1 || buf[1] == 2) {  /* only 2 valid maps */
    int cc0 = buf[0], map = buf[1], pc = buf[2], key = buf[3];
    char *name = (char *)&buf[4];
    buf += 16;
    if (map == 1)
      cc0 += 128;
    printf("{ %3d /*pgm*/, %3d /*bank*/, %3d /*key*/, "
           "\"%.12s\" },\n", pc, cc0, key, name);
  }
  return buf + 2;
}

/* load patchdump into memory */
void load_dump(char *filename)
{
  SDL_RWops *rw = SDL_RWFromFile(filename, "r");
  Uint8 *buf = NULL, *cur;
  Sint64 size;

  if (!rw) {
    perror(filename);
    return;
  }
  size = SDL_RWsize(rw);
  if (!(buf = malloc(size))) {
    perror("malloc");
    return;
  }
  if (SDL_RWread(rw, buf, size, 1) < 1) {
    /* done reading the file, or read in error */
    free(buf);
    return;
  }
  SDL_RWclose(rw);
  cur = buf;
  while (cur < buf + size) {
    cur = parse_dump(cur);
  }
}

#ifdef TEST_TARGET
/* stand alone testing of above file parsing */
int main(int argc, char **argv)
{
  if (argc > 1) {
    load_dump(argv[1]);
    //signal (SIGTRAP, SIG_IGN); // if not debugging, ignore the break
    //debug_break();
  }
  exit(0);
}
#endif
