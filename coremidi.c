/* coremidi.c  -  abstract away os specific output routines for OSX/iOS
 *
 *  Copyright 2015 Nathan Laredo (laredo@gnu.org)
 *
 * This file may be freely distributed under the terms of
 * the GNU General Public Licence (GPL).
 */

#include "playmidi.h"
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>       /* for mach_absolute_time()            */

extern int ext_dev;

/* the max size for packet buffer is 64KB, it could be smaller */
#define PKTBUF_SIZE 65536

static MIDIClientRef midiclient = 0;
static MIDIPortRef midiout = 0;
static int midi_opened = 0;
static MIDITimeStamp startstamp;
static mach_timebase_info_data_t tinfo;
static Byte pktbuffer[PKTBUF_SIZE];
extern Uint32 ticks;

static void check_err(const char *msg, OSStatus err)
{
  int i;
  Uint8 *e = (Uint8 *)&err;

  if (!err) {
    return;
  }
  /* check to see if the whole error code is four printable characters */
  for (i = 0; i < 4; i++) {
    if (e[i] < 0x20 || e[i] > 0x7e) {
      break;
    }
  }
  if (i == 4) { /* if it's four printable characters, show the code */
    fprintf(stderr, "%s: '%4.4s'\n", msg, (char *)e);
  } else { /* otherwise just show the numeric value */
    fprintf(stderr, "%s: 0x%08x\n", msg, (Uint32)err);
  }
  exit(err);
}

/* Returns an allocated char * name of a given MIDIObjectRef, need to free */
static char *getObjName(MIDIObjectRef object)
{
  CFStringRef name = nil;
  CFIndex length, maxSize;
  char *rv = NULL;
  if (noErr != MIDIObjectGetStringProperty(object, kMIDIPropertyName, &name))
    return NULL;
  if (name == nil)
    return NULL;
  length = CFStringGetLength(name);
  maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8);
  rv = malloc(maxSize);
  if (rv == NULL)
    return NULL;
  if (CFStringGetCString(name, rv, maxSize, kCFStringEncodingUTF8))
    return rv;
  free(rv);
  return NULL;
}

void show_ports(void)
{
  ItemCount i, nDests = MIDIGetNumberOfDestinations();

  printf("%-5s %s\n", "dev", "name");
  for (i = 0 ; i < nDests ; i++) {
    MIDIEndpointRef dest = MIDIGetDestination(i);
    char *name = getObjName(dest);
    if (name != NULL) {
      printf("%-5lu %s\n", i, name);
      free(name);
    } else {
      printf("%-5lu %s\n", i, "(failed getting name)");
    }
  }
  if (i == 0) {
    printf("[none]\n");
  }
}

void init_midi(void)
{
  OSStatus status;
  ItemCount nDests = MIDIGetNumberOfDestinations();
  if (midi_opened) {
    return;
  }
  if (nDests == 0) {
    return; /* nothing to write to */
  }
  if (ext_dev > nDests) {
    ext_dev = nDests - 1;
  }
  status = MIDIClientCreate(CFSTR(RELEASE), NULL, NULL, &midiclient);
  check_err("MIDIClientCreate", status);
  status = MIDIOutputPortCreate(midiclient, CFSTR("ext_dev"), &midiout);
  check_err("MIDIOutputPortCreate", status);
  if (mach_timebase_info(&tinfo) != KERN_SUCCESS) {
    tinfo.numer = 1;
    tinfo.denom = 1;
  }
  startstamp = mach_absolute_time(); 
  midi_opened++;
};

void close_midi(void)
{
  OSStatus status;
  if (!midi_opened)
    return;
  status = MIDIPortDispose(midiout);
  check_err("MIDIPortDispose", status);
  status = MIDIClientDispose(midiclient);
  check_err("MIDIClientDispose", status);
  midi_opened = 0;
}

MIDITimeStamp msec_to_ts(Uint32 t)
{
  MIDITimeStamp timestamp;
  timestamp = t;
  timestamp *= 1000000;
  timestamp *= tinfo.numer;
  timestamp /= tinfo.denom;
  return timestamp + startstamp;
}

void midi_add_pkt(struct midi_packet *p)
{
  OSStatus status;
  // convert midi ticks to nanosecond timestamp
  MIDITimeStamp timestamp = msec_to_ts(ticks);
  MIDIPacketList *packetlist = (MIDIPacketList*)pktbuffer;
  MIDIPacket *currentpacket = MIDIPacketListInit(packetlist);
  currentpacket = MIDIPacketListAdd(packetlist, sizeof(pktbuffer), 
        currentpacket, timestamp, p->len, p->data);
  status = MIDISend(midiout, MIDIGetDestination(ext_dev), packetlist);
  check_err("MIDISend", status);
}

#define SYSEX_SPLIT (PKTBUF_SIZE - 256)
void midi_send_sysex(int length, Uint8 *data, int type)
{
  OSStatus status;
  // convert midi ticks to nanosecond timestamp
  MIDITimeStamp timestamp = msec_to_ts(ticks);
  MIDIPacketList *packetlist;
  MIDIPacket *currentpacket;
  if (type == MIDI_SYSTEM_PREFIX) {
    Uint8 b = MIDI_SYSTEM_PREFIX;
    packetlist = (MIDIPacketList*)pktbuffer;
    currentpacket = MIDIPacketListInit(packetlist);
    currentpacket = MIDIPacketListAdd(packetlist, sizeof(pktbuffer), 
          currentpacket, timestamp, 1, &b);
    status = MIDISend(midiout, MIDIGetDestination(ext_dev), packetlist);
    check_err("MIDISend", status);
  }
  while (length > 0) {
    int plen = length;
    if (plen > SYSEX_SPLIT) {
      plen = SYSEX_SPLIT;  /* send in SYSEX_SPLIT byte chunks */
    }
    packetlist = (MIDIPacketList*)pktbuffer;
    currentpacket = MIDIPacketListInit(packetlist);
    currentpacket = MIDIPacketListAdd(packetlist, sizeof(pktbuffer), 
          currentpacket, timestamp, plen, data);
    status = MIDISend(midiout, MIDIGetDestination(ext_dev), packetlist);
    check_err("MIDISend", status);
    data += plen;
    length -= plen;
  }
}
