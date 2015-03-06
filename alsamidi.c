/* alsamidi.c  -  abstract away os specific output routines for linux/alsa
 *
 *  Copyright 2015 Nathan Laredo (laredo@gnu.org)
 *
 * This file may be freely distributed under the terms of
 * the GNU General Public Licence (GPL).
 */

#include "playmidi.h"
#include <alsa/asoundlib.h>

extern int ext_dev;

static snd_seq_t *seq;
static int queue, client;
static int midi_opened = 0;
static snd_seq_addr_t port;
extern Uint32 ticks;

static void check_snd(const char *msg, int err)
{
  if (err < 0) {
    fprintf(stderr, "%s: %s\n", msg, snd_strerror(err));
    exit(1);
  }
}

static void check_mem(void *buf)
{
  if (buf == NULL) {
    fprintf(stderr, "allocation failed.\n");
    exit(1);
  }
}


void show_ports(void)
{
  int index = 0;
  snd_seq_client_info_t *cinfo;
  snd_seq_port_info_t *pinfo;

  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_alloca(&pinfo);
  check_mem(cinfo);
  check_mem(pinfo);

  printf("%-5s %-8s %-32s %s\n",
         "dev", "Port", "Client name", "Port name");

  snd_seq_client_info_set_client(cinfo, -1);
  while (snd_seq_query_next_client(seq, cinfo) >= 0) {
    int client = snd_seq_client_info_get_client(cinfo);

    snd_seq_port_info_set_client(pinfo, client);
    snd_seq_port_info_set_port(pinfo, -1);
    while (snd_seq_query_next_port(seq, pinfo) >= 0) {
      /* port must understand MIDI messages */
      if (!(snd_seq_port_info_get_type(pinfo)
            & SND_SEQ_PORT_TYPE_MIDI_GENERIC))
        continue;
      /* we need both WRITE and SUBS_WRITE */
      if ((snd_seq_port_info_get_capability(pinfo)
           & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
          != (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
        continue;
      printf("%-5d %3d:%-3d  %-32.32s %s\n", index,
             snd_seq_port_info_get_client(pinfo),
             snd_seq_port_info_get_port(pinfo),
             snd_seq_client_info_get_name(cinfo),
             snd_seq_port_info_get_name(pinfo));
      index++;
    }
  }
}

/* abstract alsa device mess with simple device index request */
static void connect_port(int dev_index)
{
  int index = 0;
  snd_seq_client_info_t *cinfo;
  snd_seq_port_info_t *pinfo;

  snd_seq_client_info_alloca(&cinfo);
  snd_seq_port_info_alloca(&pinfo);
  check_mem(cinfo);
  check_mem(pinfo);

  snd_seq_client_info_set_client(cinfo, -1);
  while (snd_seq_query_next_client(seq, cinfo) >= 0) {
    int client = snd_seq_client_info_get_client(cinfo);

    snd_seq_port_info_set_client(pinfo, client);
    snd_seq_port_info_set_port(pinfo, -1);
    while (snd_seq_query_next_port(seq, pinfo) >= 0) {
      /* port must understand MIDI messages */
      if (!(snd_seq_port_info_get_type(pinfo)
            & SND_SEQ_PORT_TYPE_MIDI_GENERIC))
        continue;
      /* we need both WRITE and SUBS_WRITE */
      if ((snd_seq_port_info_get_capability(pinfo)
           & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
          != (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
        continue;
      /* check if this device is the index requested */
      port.client = snd_seq_port_info_get_client(pinfo);
      port.port = snd_seq_port_info_get_port(pinfo);
      if (index == dev_index) {
        int err = snd_seq_connect_to(seq, 0, port.client, port.port);
        check_snd("snd_seq_connect", err);
        return;
      }
      index++;
    }
  }
}

static void create_source_port(void)
{
  snd_seq_port_info_t *pinfo;
  int err;

  snd_seq_port_info_alloca(&pinfo);
  check_mem(pinfo);

  /* the first created port is 0 anyway, but let's make sure ... */
  snd_seq_port_info_set_port(pinfo, 0);
  snd_seq_port_info_set_port_specified(pinfo, 1);

  snd_seq_port_info_set_name(pinfo, "playmidi");

  snd_seq_port_info_set_capability(pinfo, 0);
  snd_seq_port_info_set_type(pinfo, SND_SEQ_PORT_TYPE_MIDI_GENERIC |
        SND_SEQ_PORT_TYPE_APPLICATION);

  err = snd_seq_create_port(seq, pinfo);
  check_snd("snd_seq_create_port", err);
}

static void create_queue(void)
{
  queue = snd_seq_alloc_named_queue(seq, "playmidi");
  check_snd("snd_seq_alloc_named_queue", queue);
}

void init_midi(void)
{
  int err;

  if (midi_opened) {
    return;
  }
  err = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
  check_snd("snd_seq_open", err);

  err = snd_seq_set_client_name(seq, RELEASE);
  check_snd("snd_seq_set_client_name", err);

  client = snd_seq_client_id(seq);
  check_snd("snd_seq_client_id", client);
  create_source_port();
  create_queue();
  connect_port(ext_dev);
  err = snd_seq_start_queue(seq, queue, NULL);
  check_snd("snd_seq_start_queee", err);
  midi_opened++;
}

/* give last notes a chance to get flushed out, proper close */
void close_midi(void)
{
  int err;
  snd_seq_event_t ev;
  if (!midi_opened) {
    return;
  }

  /* schedule queue stop at end of playback */
  snd_seq_ev_clear(&ev);
  ev.queue = queue;
  ev.source.port = 0;
  ev.flags = SND_SEQ_TIME_STAMP_REAL | SND_SEQ_TIME_MODE_ABS;
  snd_seq_ev_set_fixed(&ev);
  ev.type = SND_SEQ_EVENT_STOP;
  ev.time.time.tv_sec = 1 + ticks / 1000;
  ev.time.time.tv_nsec = (ticks % 1000) * 1000000;
  ev.dest.client = SND_SEQ_CLIENT_SYSTEM;
  ev.dest.port = SND_SEQ_PORT_SYSTEM_TIMER;
  ev.data.queue.queue = queue;
  err = snd_seq_event_output(seq, &ev);
  check_snd("snd_seq_event_output", err);
  err = snd_seq_drain_output(seq);
  check_snd("snd_seq_drain_output", err);
  err = snd_seq_sync_output_queue(seq);
  check_snd("snd_seq_sync_output_queue", err);
  snd_seq_close(seq);
  midi_opened = 0;
}

static const unsigned char snd_cmd_type[] = {
  SND_SEQ_EVENT_NOTEOFF,        /* 0x80 */
  SND_SEQ_EVENT_NOTEON,         /* 0x90 */
  SND_SEQ_EVENT_KEYPRESS,       /* 0xa0 */
  SND_SEQ_EVENT_CONTROLLER,     /* 0xb0 */
  SND_SEQ_EVENT_PGMCHANGE,      /* 0xc0 */
  SND_SEQ_EVENT_CHANPRESS,      /* 0xd0 */
  SND_SEQ_EVENT_PITCHBEND       /* 0xe0 */
};

void midi_add_pkt(struct midi_packet *p)
{
  int err;
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  ev.queue = queue;
  ev.source.port = 0;
  ev.flags = SND_SEQ_TIME_STAMP_REAL | SND_SEQ_TIME_MODE_ABS;
  snd_seq_ev_set_fixed(&ev);
  if (p->data[0] >= 0xf0) 
    return; /* sysex and/or realtime has another venue */
  ev.type = snd_cmd_type[(p->data[0] >> 4) - 0x8];
  ev.time.time.tv_sec = ticks / 1000;
  ev.time.time.tv_nsec = (ticks % 1000) * 1000000;
  ev.dest = port;
  switch (ev.type) {
    case SND_SEQ_EVENT_NOTEOFF:
    case SND_SEQ_EVENT_NOTEON:
    case SND_SEQ_EVENT_KEYPRESS:
      ev.data.note.channel = (p->data[0] & 0xf);
      ev.data.note.note = p->data[1];
      ev.data.note.velocity = p->data[2];
      break;
    case SND_SEQ_EVENT_CONTROLLER:
      ev.data.control.channel = (p->data[0] & 0xf);
      ev.data.control.param = p->data[1];
      ev.data.control.value = p->data[2];
      break;
    case SND_SEQ_EVENT_PGMCHANGE:
    case SND_SEQ_EVENT_CHANPRESS:
      ev.data.control.channel = (p->data[0] & 0xf);
      ev.data.control.value = p->data[1];
      break;
    case SND_SEQ_EVENT_PITCHBEND:
      ev.data.control.channel = (p->data[0] & 0xf);
      ev.data.control.value = ((p->data[1]) |
        ((p->data[2]) <<7)) - 0x2000;
      break;
  }
  err = snd_seq_event_output(seq, &ev);
  check_snd("snd_seq_event_output", err);
  err = snd_seq_drain_output(seq);
  check_snd("snd_seq_drain_output", err);
}

#define SYSEX_SPLIT 32
void midi_send_sysex(int length, Uint8 *data, int type)
{
  int err;
  snd_seq_event_t ev;

  snd_seq_ev_clear(&ev);
  ev.queue = queue;
  ev.source.port = 0;
  ev.flags = SND_SEQ_TIME_STAMP_REAL | SND_SEQ_TIME_MODE_ABS;
  ev.type = SND_SEQ_EVENT_SYSEX;
  ev.time.time.tv_sec = ticks / 1000;
  ev.time.time.tv_nsec = (ticks % 1000) * 1000000;
  if (type == MIDI_SYSTEM_PREFIX) {
    Uint8 b = MIDI_SYSTEM_PREFIX;
    snd_seq_ev_set_variable(&ev, 1, &b);
    err = snd_seq_event_output(seq, &ev);
    check_snd("snd_seq_event_output", err);
    err = snd_seq_drain_output(seq);
    check_snd("snd_seq_drain_output", err);
    err = snd_seq_sync_output_queue(seq);
    check_snd("snd_seq_sync_output_queue", err);
  }
  snd_seq_ev_set_variable(&ev, length, data);
  while (length > 0) {
    if (length > SYSEX_SPLIT) {
      ev.data.ext.len = SYSEX_SPLIT;
    } else {
      ev.data.ext.len = length;
    }
    err = snd_seq_event_output(seq, &ev);
    check_snd("snd_seq_event_output", err);
    err = snd_seq_drain_output(seq);
    check_snd("snd_seq_drain_output", err);
    err = snd_seq_sync_output_queue(seq);
    check_snd("snd_seq_sync_output_queue", err);
    ev.data.ext.ptr += ev.data.ext.len;
    length -= ev.data.ext.len;
  }
}
