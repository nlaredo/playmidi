/* loadsf2.c  -  load/split a sf2 riff file into component chunks
 *
 *  Copyright 2015 Nathan Laredo (laredo@gnu.org)
 *
 * This file may be freely distributed under the terms of
 * the GNU General Public Licence (GPL).
 *
 * CFLAGS += -Wno-multichar
 */

#include "SDL2/SDL.h"
#include "SDL2/SDL_audio.h"
#include "soundfont2.h"

#include <stdio.h>
#include <signal.h>

#include "debugbreak.h"

struct sfSFBK sf2;  /* pointers to everything loaded go here */
extern int verbose;

/* for each tag value found, describe where to stick a pointer to the data */
struct fillSFBK { char *tag; void *dest; };
struct fillSFBK filldata[] = {
  { "ifil", &sf2.ifil }, { "isng", &sf2.isng }, { "INAM", &sf2.INAM },
  { "irom", &sf2.irom }, { "iver", &sf2.iver }, { "ICRD", &sf2.ICRD },
  { "IENG", &sf2.IENG }, { "IPRD", &sf2.IPRD }, { "ICOP", &sf2.ICOP },
  { "ICMT", &sf2.ICMT }, { "ISFT", &sf2.ISFT }, { "smpl", &sf2.smpl },
  { "sm24", &sf2.sm24 }, { "phdr", &sf2.phdr }, { "pbag", &sf2.pbag },
  { "pmod", &sf2.pmod }, { "pgen", &sf2.pgen }, { "inst", &sf2.inst },
  { "ibag", &sf2.ibag }, { "imod", &sf2.imod }, { "igen", &sf2.igen },
  { "shdr", &sf2.shdr }, { NULL, NULL }
};

static void fill_sf2(Uint32 tag, void *buf, Uint32 size)
{
  int i;
  for (i = 0; filldata[i].tag != NULL && filldata[i].dest != NULL; i++) {
    if (*(Uint32 *)filldata[i].tag == tag) {
      memcpy(filldata[i].dest, &buf, sizeof(void *));
      memcpy(sizeof(void *) + filldata[i].dest, &size, sizeof(Uint32));
      return;
    }
  }
  fprintf(stderr, "Unhandled %.4s in sf2 file.\n", (char *)&tag);
}

static void parse_subchunk(struct riffChunk *parent, Uint32 level)
{
  Uint32 offset = 4;  /* skip the data tag */
  struct riffChunk *buf = (struct riffChunk *)(parent->data + offset);

  if (level > 3) {
    fprintf(stderr, "ignored unexpectedly deep recursion in RIFF file\n");
    return;
  }
  while (parent->len - offset >= sizeof(struct riffChunk)) {
    buf = (struct riffChunk *)(parent->data + offset);
    if (0) {
      fprintf(stderr, "%*sTAG = %.4s LEN = %d\n", 2 + level * 2, "",
              (char *)&buf->tag, buf->len);
    }
    if (buf->tag == SDL_SwapBE32('LIST')) {
      parse_subchunk(buf, level + 1);  /* look for chunks inside this chunk */
    } else {
      fill_sf2(buf->tag, &buf->data, buf->len);
    }
    offset += sizeof(struct riffChunk) + buf->len;
  }
  return;
}

/* load soundfont2 riff file into sf2 struct, return pointer to raw riff data */
struct riffChunk *load_sf2(char *filename)
{
  SDL_RWops *rw = SDL_RWFromFile(filename, "r");
  Uint32 tag, len;
  Sint64 size;
  struct riffChunk *buf = NULL;
  int error = 0;

  if (!rw) {
    if (verbose)
      perror(filename);
    return NULL;
  }
  size = SDL_RWsize(rw);
  /* at the top level there should only be one chunk */
  /* but loop anyway in case someone concatenated riff files */
  while (size >= sizeof(struct riffChunk)) {
    tag = SDL_ReadLE32(rw);
    len = SDL_ReadLE32(rw);
    if (!(buf = malloc(sizeof(struct riffChunk) + len))) {
      perror("malloc");
      return NULL;
    }
    buf->tag = tag;
    buf->len = len;
    if (SDL_RWread(rw, buf->data, len, 1) < 1) {
      /* done reading the file, or read in error */
      free(buf);
      return NULL;
    }
    if (0) {
      fprintf(stderr, "TAG = %.4s LEN = %d\n", (char *)&buf->tag, buf->len);
    }
    if (*(Uint32 *)buf->data == SDL_SwapBE32('sfbk')) {
      parse_subchunk(buf, 0);  /* look for chunks inside this chunk */
    }
    size -= sizeof(struct riffChunk) + buf->len;
  };
  SDL_RWclose(rw);
  if (sf2.phdr_size < sizeof(struct sfPresetHeader) * 2) { error++; }
  if (sf2.pbag_size < sizeof(struct sfPresetBag) * 2) { error++; }
  if (sf2.pgen_size < sizeof(struct sfGenList) * 2) { error++; }
  if (sf2.inst_size < sizeof(struct sfInst) * 2) { error++; }
  if (sf2.igen_size < sizeof(struct sfInstGenList) * 2) { error++; }
  if (sf2.shdr_size < sizeof(struct sfSample) * 2) { error++; }
  if (error) {
    fprintf(stderr, "%s: malformed sf2 file, ignoring\n", filename);
    memset(&sf2, 0, sizeof(sf2));
    free(buf);
    return NULL;
  }
  return buf;
}

#ifdef TEST_TARGET
int verbose = 1;
/* stand alone testing of above file parsing */
int main(int argc, char **argv)
{
  if (argc > 1) {
    load_sf2(argv[1]);
    signal (SIGTRAP, SIG_IGN); // if not debugging, ignore the break
    debug_break();
  }
  exit(0);
}
#endif
