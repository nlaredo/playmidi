/* playmidi.h  -  defines and structures used to play midi files
 *
 *  Copyright 2015 Nathan Laredo (laredo@gnu.org)
 *
 * This file may be freely distributed under the terms of
 * the GNU General Public Licence (GPL).
 */

#define RELEASE "Playmidi 2.9"

#include "SDL2/SDL.h"
#include "SDL2/SDL_audio.h"

#include "soundfont2.h"

/* change this if you have really outrageous midi files > 128 tracks */
/* FIXME: should probably make this a malloc during midi file load */
#define MAXTRKS		128

/* many of these constant names were previously used from linux includes */
/* now expanded vs linux system defines with new GM2 standard controllers */
enum midi_controller_numbers {
  CTL_BANK_SELECT       = 0x00,  /* not reset on "reset all controllers" */
  CTL_MODWHEEL          = 0x01,  /* 0(default)-127(max) modulation */
  CTL_BREATH            = 0x02,
  CTL_LFO_RATE          = 0x03,  /* minimoog voyager: adjust LFO frequency */
  CTL_FOOT              = 0x04,
  CTL_PORTAMENTO_TIME   = 0x05,
  CTL_DATA_ENTRY        = 0x06,
  CTL_MAIN_VOLUME       = 0x07,  /* not reset on "reset all controllers" */
  CTL_BALANCE           = 0x08,
  CTL_PAN               = 0x0a,  /* 0(l)-127(r), default=64 (not reset) */
  CTL_EXPRESSION        = 0x0b,  /* channel 0(0%) - 16384(100%, default) */
  CTL_MOTIONAL_CTL1     = 0x0c,  /* roland integra 7, Part L-R param */
  CTL_MOTIONAL_CTL2     = 0x0d,  /* roland integra 7, Part F-B param */
  CTL_MOTIONAL_CTL3     = 0x0e,  /* roland integra 7, Part ambience level */
  CTL_GENERAL_PURPOSE1  = 0x10,
  CTL_TONE_MODIFY1      = 0x10,  /* roland name */
  CTL_GENERAL_PURPOSE2  = 0x11,
  CTL_TONE_MODIFY2      = 0x11,  /* roland name */
  CTL_GENERAL_PURPOSE3  = 0x12,
  CTL_GENERAL_PURPOSE4  = 0x13,
  CTL_MOTIONAL_EXT1     = 0x1c,  /* roland integra 7, PartEx L-R param */
  CTL_MOTIONAL_EXT2     = 0x1d,  /* roland integra 7, PartEx F-B param */
  CTL_MOTIONAL_EXT3     = 0x1e,  /* roland integra 7, PartEx amb level */
  CTL_LSB               = 0x20,  /* above all have LSB at (CTL_* + CTL_LSB) */
  /* controllers #64 to #69 (0x40 to 0x45) are on/off switches. */
  CTL_DAMPER_PEDAL      = 0x40,  /* 0(default)-63 = off, 64 - 127 = on */
  CTL_SUSTAIN           = 0x40,
  CTL_HOLD              = 0x40,  /* 0-63 = off(default), 64-127 sustain all */
  CTL_HOLD1             = 0x40,
  CTL_PORTAMENTO        = 0x41,  /* 0(default)-63 = off, 64 - 127 = on */
  CTL_SOSTENUTO         = 0x42,  /* 0(default)-63 = off, 64 - 127 = on */
  CTL_SOFT_PEDAL        = 0x43,  /* 0(default)-63 = off, 64 - 127 = on */
  CTL_LEGATO            = 0x44,  /* 0(default)-63 = off, 64 - 127 = on */
  CTL_HOLD2             = 0x45,
  /* controllers #70 - #79 are not reset on "reset all controllers" */
  CTL_SOUND_VARIATION   = 0x46,  /* RP-021: sound controller 1 */
  CTL_RESONANCE         = 0x47,  /* sc2: timbre/harmonic intensity */
  CTL_RELEASE_TIME      = 0x48,  /* RP-021: sound controller 3 */
  CTL_ATTACK_TIME       = 0x49,  /* RP-021: sound controller 4 */
  CTL_CUTOFF            = 0x4a,  /* RP-021: sound controller 5 */
  CTL_BRIGHTNESS        = 0x4a,  /* RP-021: sound controller 5 */
  CTL_DECAY_TIME        = 0x4b,  /* RP-021: sound controller 6 */
  CTL_VIBRATO_RATE      = 0x4c,  /* RP-021: sound controller 7 */
  CTL_VIBRATO_DEPTH     = 0x4d,  /* RP-021: sound controller 8 */
  CTL_VIBRATO_DELAY     = 0x4e,  /* RP-021: sound controller 9 */
  CTL_SOUND_CONTROLLER10= 0x4f,  /* source: MMA recommended practice RP-021 */
  CTL_GENERAL_PURPOSE5  = 0x50,
  CTL_TONE_VARIATION1   = 0x50,
  CTL_GENERAL_PURPOSE6  = 0x51,
  CTL_TONE_VARIATION2   = 0x51,
  CTL_GENERAL_PURPOSE7  = 0x52,
  CTL_TONE_VARIATION3   = 0x52,
  CTL_GENERAL_PURPOSE8  = 0x53,
  CTL_TONE_VARIATION4   = 0x53,
  CTL_PORTAMENTO_CTRL   = 0x54,
  CTL_VELOCITY_LSB      = 0x58,  /* MMA CA-031: high resolution velocity lsb */
  /* controllers #91 - #95 are not reset on "reset all controllers" */
  CTL_REVERB_DEPTH      = 0x5b,  /* MMA recommended practice RP-023 renamed */
  CTL_TREMOLO_DEPTH     = 0x5c,
  CTL_CHORUS_DEPTH      = 0x5d,  /* MMA recommended practice RP-023 renamed */
  CTL_DETUNE_DEPTH      = 0x5e,
  CTL_CELESTE_DEPTH     = 0x5e,
  CTL_PHASER_DEPTH      = 0x5f,
  CTL_DATA_INCREMENT    = 0x60,
  CTL_DATA_DECREMENT    = 0x61,
  CTL_NRPN_LSB          = 0x62, /* default = 127 (null value) */
  CTL_NRPN_MSB          = 0x63, /* default = 127 (null value) */
  CTL_RPN_LSB           = 0x64, /* default = 127 (null value) */
  CTL_RPN_MSB           = 0x65, /* default = 127 (null value) */
  /* controllers #120 - #127 are not reset on "reset all controllers" */
  CTL_ALL_SOUNDS_OFF    = 0x78,
  CTL_RESET_ALL_CONTROLLERS = 0x79, /* 3rd byte 0x00 */
  CTL_LOCAL             = 0x7a,  /* 0 = off, 127 = on */
  CTL_ALL_NOTES_OFF     = 0x7b,  /* 3rd byte 0x00 */
  CTL_OMNI_OFF          = 0x7c,  /* 3rd byte 0x00 */
  CTL_OMNI_ON           = 0x7d,  /* 3rd byte 0x00 */
  CTL_MONO              = 0x7e,  /* 3rd byte 0x00 - 0x10 (mono number) */
  CTL_POLY              = 0x7f,  /* 3rd byte 0x00 */
};

enum midi_rpn_destinations {
  RPN_PITCH_BEND_RANGE  = 0x00,  /* mm = semitones, ll = cents, default=mm=2 */
  /* old: master fine is adjusted, new gm2: only channel fine is adjusted */
  RPN_CHN_FINE_TUNE     = 0x01,  /* -8192*50/8192 - 0 - +8192*50/8192 cent */
  RPN_CHN_COARSE_TUNE   = 0x02,  /* -48 - 0 - +48 semitones, ll = ignored */
  RPN_TUNING_PGM_SEL    = 0x03,
  RPN_TUNING_BANK_SEL   = 0x04,
  RPN_MOD_DEPTH_RANGE   = 0x05,
  RPN_AZIMUTH_ANGLE     = 0x3d00,  /* see RP-049 for all 3D sound controllers */
  RPN_ELEVATION_ANGLE   = 0x3d01,
  RPN_GAIN              = 0x3d02,
  RPN_DISTANCE_RATIO    = 0x3d03,
  RPN_MAX_DISTANCE      = 0x3d04,
  RPN_GAIN_AT_MAX_DISTANCE= 0x3d05,
  RPN_REF_DISTANCE_RATIO= 0x3d06,
  RPN_PAN_SPREAD_ANGLE  = 0x3d07,
  RPN_ROLL_ANGLE        = 0x3d08,
  RPN_NULL              = 0x7f7f,
};

enum midi_nrpn_destinations {
  NRPN_GS_VIBRATO_RATE  = 0x0108,  /* -64 - 0(0x40) - +63 (relative) */
  NRPN_GS_VIBRATO_DEPTH = 0x0109,  /* -64 - 0(0x40) - +63 (relative) */
  NRPN_GS_VIBRATO_DELAY = 0x010a,  /* -64 - 0(0x40) - +63 (relative) */
  /* TVF = Time Variant Filter */
  NRPN_GS_TVF_CUTOFF_FREQ = 0x0120,  /* -64 - 0(0x40) - +63 (relative) */
  NRPN_GS_TVF_RESONANCE = 0x0121,  /* -64 - 0(0x40) - +63 (relative) */
  /* TVA = Time Variant Amplifier */
  NRPN_GS_TVFTVA_ATTACK = 0x0163,  /* -64 - 0(0x40) - +63 (relative) */
  NRPN_GS_TVFTVA_DECAY  = 0x0164,  /* -64 - 0(0x40) - +63 (relative) */
  NRPN_GS_TVFTVA_RELEASE= 0x0166,  /* -64 - 0(0x40) - +63 (relative) */
  NRPN_GS_RHYTHM_PITCH_C= 0x1800,  /* 0x18rr rr = note number (abs) */
  NRPN_GS_RHYTHM_TVA_LVL= 0x1a00,  /* 0x1arr rr = note number (abs) */
  /* note: panpot -64 = "random", -63(L) - 0(Center) - +63(R) */
  NRPN_GS_RHYTHM_PANPOT = 0x1c00,  /* 0x1crr rr = note number (abs) */
  NRPN_GS_RHYTHM_REVERB = 0x1d00,  /* 0x1drr rr = note number (abs) */
  NRPN_GS_RHYTHM_CHORUS = 0x1e00,  /* 0x1err rr = note number (abs) */
  NRPN_NULL             = 0x7f7f,
};

enum midi_command_prefixes {
  MIDI_NOTEOFF          = 0x80,  /* 0x8n 0xkk 0xvv n=ch, kk=key, vv=vel */
  MIDI_NOTEON           = 0x90,  /* 0x9n 0xkk 0xvv n=ch, kk=key, vv=vel */
  MIDI_KEY_PRESSURE     = 0xa0,  /* 0xan 0xkk 0xvv n=ch, kk=key, vv=value */
  MIDI_CTL_CHANGE       = 0xb0,  /* 0xbn 0xll 0xnn n=ch, nn=ctl#, mm=value */
  MIDI_PGM_CHANGE       = 0xc0,  /* 0xcn 0xpp n=ch, pp=program number */
  MIDI_CHN_PRESSURE     = 0xd0,  /* 0xdn 0xvv n=ch, vv=value */
  MIDI_PITCH_BEND       = 0xe0,  /* 0xen 0xll 0xmm n=ch, ll=lsb mm=msb */
  MIDI_SYSTEM_PREFIX    = 0xf0,
};

enum midi_system_messages {
  MIDI_SYSTEM_EXCLUSIVE = 0xf0,
  MIDI_TIME_CODE_QF     = 0xf1,
  MIDI_SONG_POSITION    = 0xf2,
  MIDI_SONG_SELECT      = 0xf3,
  MIDI_TUNE_REQUEST     = 0xf6,
  MIDI_SYSEX_END        = 0xf7,
  MIDI_TIMING_CLOCK     = 0xf8,
  MIDI_START            = 0xfa,
  MIDI_CONTINUE         = 0xfb,
  MIDI_STOP             = 0xfc,
  MIDI_ACTIVE_SENSING   = 0xfe,
  MIDI_RESET            = 0xff,
};

#define CMD(x) ((x) & 0xf0)
#define CHN(x) ((x) & 0x0f)

#define ISPERC(x)	(perc & (1 << (x)))
#define ISMIDI(x)	(play_ext & (1 << (x)))
#define ISPLAYING(x)	(chanmask & (1 << (x)))
#define NO_EXIT		100

struct lfostate {
  float r;              // value to add to timebase each sample
  float t;              // timebase for this generator, 0 - 2pi
  float v;              // output value for this generator
};

struct chanstate {
  float bender_mult;    // value to multiply per tone 'r' by for pitchbend
  float mod_mult;       // value to multiply lfo per tone 'r' by for modwheel
  int program;          // midi program to play for this channel, < 0 = use math
  int bender;           // midi pitch bend value in effect, default = 8192
  int bender_range;     // pitchbend range in cents, default = 200
  int controller[256];  // up to 14-bit midi controller data values, see CTL_*
  int pressure;         // 0(default)-127, has no effect by default
  struct lfostate mod;  // modulation lfo generator
  struct lfostate vib;  // vibrato lfo generator
};

struct sf2gen {
  Uint32 dwStart;       // final generated starting absolute sample in sdata
  Uint32 dwEnd;         // final generated ending absolute sample in sdata
  Uint32 dwStartloop;   // final generated loop start point sample in sdata
  Uint32 dwEndloop;     // final generated loop end point sample in sdata
  Uint32 sampleModes;   // 0 = no loop, 1 = loop, 2 = no loop, 3 = loop+finish
};

struct voice_env {
  float a, h, d, r;     // attack, hold, decay, release in units of samples
  float s;              // velocity at which to sustain note

};

struct voicestate {
  float f;              // frequency for this voice
  float r;              // value to add to timebase each sample
  float v;              // velocity 0.0 - 1.0 (see also CTL_VELOCITY_LSB)
  float t;              // timebase for this note, math 0 - 2pi, or sample pos
  float pan;            // pan, 0.0(l) - 1.0(r), 0.5 = center
  int note;             // midi note number being played
  int vel;              // velocity from midi note on event
  int channel;          // midi channel for this note 0-15
  int sustain;          // if note off is deferred by CTL_SUSTAIN, sustain=1
  int exclusive_class;  // if > 0, new notes terminate others of same ch+class
  Uint64 endstamp;      // event end: sample position at note off plus release
  Uint64 timestamp;     // event start: global running sample count position
  struct voice_env env; // volume envelope, adsr timed in sample units
  struct sf2gen s;      // sf2 sample data, dwStart == dwEnd means no samples

  // sf2 access tracking, used at note-on time only to initialize voice
  int phdr;             // index into phdr chunk
  int pbag, pbag_max;   // current and last index into pbag chunk
  int pgen, pgen_max;   // current and last index into pgen chunk
  int pmod, pmod_max;   // current and last index into pmod chunk (unused)
  int inst;             // current index into inst chunk
  int ibag, ibag_max;   // current and last index into ibag chunk
  int igen, igen_max;   // current and last index into igen chunk
  int imod, imod_max;   // current and last index into imod chunk (unused)
  int shdr;             // current index into shdr chunk
};

struct midi_packet {
  Uint64 timestamp;     // event start in samples
  Uint16 len;           // length of event data in bytes
  Uint8 data[0];        // data for event
};

/* Non-standard MIDI file formats */
#define RIFF   0x52494646
#define CTMF   0x43544d46
/* Standard MIDI file format definitions */
#define MThd   0x4d546864
#define MTrk   0x4d54726b

enum smf_meta_events {
  SEQUENCE_NUMBER       = 0x00,
  TEXT_EVENT            = 0x01,
  COPYRIGHT_NOTICE      = 0x02,
  SEQUENCE_NAME         = 0x03,
  INSTRUMENT_NAME       = 0x04,
  LYRIC                 = 0x05,
  MARKER                = 0x06,
  CUE_POINT             = 0x07,
  PROGRAM_NAME          = 0x08,  /* added: MMA recommended practice RP-019 */
  DEVICE_NAME           = 0x09,  /* added: MMA recommended practice RP-019 */
  CHANNEL_PREFIX        = 0x20,
  END_OF_TRACK          = 0x2f,
  SET_TEMPO             = 0x51,
  SMPTE_OFFSET          = 0x54,
  TIME_SIGNATURE        = 0x58,
  KEY_SIGNATURE         = 0x59,
  SEQUENCER_SPECIFIC    = 0x74,
  META_EVENT            = 0xff   /* prefixes all the above in the midi file */
};

struct miditrack {
   Uint8 *data;  /* data of midi track */
   Uint32 length; /* length of track data */
   Uint32 index; /* current byte in track */
   Uint32 ticks; /* current midi tick count */
   Uint8 running_st; /* running status byte */
};

/* hardware specific midi access abstracted by the following */
extern void init_midi(void);
extern void close_midi(void);
extern void show_ports(void);
extern void midi_add_pkt(struct midi_packet *p);
extern void midi_send_sysex(int length, Uint8 *data, int type);
