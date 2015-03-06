/* soundfont2.h  -  defines and structures used to handle SF2 files
 *
 *  Copyright 2015 Nathan Laredo (laredo@gnu.org)
 *
 * This file may be freely distributed under the terms of
 * the GNU General Public Licence (GPL).
 */

/* source document: http://freepats.zenvoid.org/sf2/sfspec24.pdf */

/* 3.1 General RIFF File Structure */
struct __attribute__ ((__packed__)) riffChunk {
  Uint32 tag;           /* chunk id identifies the type of data in chunk */
  Uint32 len;           /* the size of chunk data in bytes */
  Uint8 data[0];        /* the data, end is aligned on 2 byte boundary */
};

/* 4.5 SoundFont 2 RIFF File Format Type Definitions */

typedef struct {
  Uint8 byLo;
  Uint8 byHi;
} rangesType;

typedef union {
  rangesType ranges;
  Sint16 shAmount;
  Uint16 wAmount;
} genAmountType;

typedef enum __attribute__ ((__packed__)) {
  monoSample            = 1,
  rightSample           = 2,
  leftSample            = 4,
  linkedSample          = 8,
  RomSample             = 0x8000,
  RomMonoSample         = 0x8001,
  RomRightSample        = 0x8002,
  RomLeftSample         = 0x8004,
  RomLinkedSample       = 0x8008
} SFSampleLink;

typedef enum __attribute__ ((__packed__)) {
  /* 8.1.3 Generator Summary, SF2 specification */
/*
 *  * Range depends on values of start, loop, and end points in sample header.
 *  ** Range has discrete values based on bit flags
 *  + This generator is only valid at the instrument level.
 *  @ This generator is designated as a non-real-time parameter. 
 */
  SFG_startAddrsOffset          =  0,  /* + smpls, def:0 */
  SFG_endAddrsOffset            =  1,  /* + smpls, def:0 */
  SFG_startloopAddrsOffset      =  2,  /* + smpls, def:0 */
  SFG_endloopAddrsOffset        =  3,  /* + smpls, def:0 */
  SFG_startAddrsCoarseOffset    =  4,  /* + 32k smpls, def:0 */
  SFG_modLfoToPitch             =  5,  /* cent fs, -12000 - 12000, def:0 */
  SFG_vibLfoToPitch             =  6,  /* cent fs, -12000 - 12000, def:0 */
  SFG_modEnvToPitch             =  7,  /* cent fs, -12000 - 12000, def:0 */
  SFG_initialFilterFc           =  8,  /* cent, 1500 - 13500, def:13500 */
  SFG_initialFilterQ            =  9,  /* cB, 0 - 960, def:0 */
  SFG_modLfoToFilterFc          = 10,  /* cent fs, -12000 - 12000, def:0 */
  SFG_modEnvToFilterFc          = 11,  /* cent fs, -12000 - 12000, def:0 */
  SFG_endAddrsCoarseOffset      = 12,  /* + 32k smpls, def:0 */
  SFG_modLfoToVolume            = 13,  /* cB fs, -960 - 960, def:0 */
  SFG_unused1                   = 14,  /* Unused, reserved. ignore */
  SFG_chorusEffectsSend         = 15,  /* 0.1%, 0 - 1000, def:0 */
  SFG_reverbEffectsSend         = 16,  /* 0.1%, 0 - 1000, def:0 */
  SFG_pan                       = 17,  /* 0.1%, -500 - 500, def:0 */
  SFG_unused2                   = 18,  /* Unused, reserved. ignore */
  SFG_unused3                   = 19,  /* Unused, reserved. ignore */
  SFG_unused4                   = 20,  /* Unused, reserved. ignore */
  SFG_delayModLFO               = 21,  /* timecent, 0 - 1000, def:0 */
  SFG_freqModLFO                = 22,  /* cent, -16000 - 4500, def:0 */
  SFG_delayVibLFO               = 23,  /* timecent, -12000 - 5000, def:-12000 */
  SFG_freqVibLFO                = 24,  /* cent, -16000 - 4500, def:0 */
  SFG_delayModEnv               = 25,  /* timecent, -12000 - 5000, def:-12000 */
  SFG_attackModEnv              = 26,  /* timecent, -12000 - 8000, def:-12000 */
  SFG_holdModEnv                = 27,  /* timecent, -12000 - 5000, def:-12000 */
  SFG_decayModEnv               = 28,  /* timecent, -12000 - 8000, def:-12000 */
  SFG_sustainModEnv             = 29,  /* -0.1%, 0 - 1000, def:0 */
  SFG_releaseModEnv             = 30,  /* timecent, -12000 - 8000, def:-12000 */
  SFG_keynumToModEnvHold        = 31,  /* tcent/key, -1200 - 1200, def:0 */
  SFG_keynumToModEnvDecay       = 32,  /* tcent/key, -1200 - 1200, def:0 */
  SFG_delayVolEnv               = 33,  /* timecent, -12000 - 5000, def:-12000 */
  SFG_attackVolEnv              = 34,  /* timecent, -12000 - 8000, def:-12000 */
  SFG_holdVolEnv                = 35,  /* timecent, -12000 - 5000, def:-12000 */
  SFG_decayVolEnv               = 36,  /* timecent, -12000 - 8000, def:-12000 */
  SFG_sustainVolEnv             = 37,  /* cB attn, 0 - 1440, def:0 */
  SFG_releaseVolEnv             = 38,  /* timecent 1 sec -12000 1 msec 8000 100sec -12000 <1 msec */
  SFG_keynumToVolEnvHold        = 39,  /* tcent/key, -1200 - 1200, def:0 */
  SFG_keynumToVolEnvDecay       = 40,  /* tcent/key, -1200 - 1200, def:0 */
  SFG_instrument                = 41,  /* index into inst chunk to use */
  SFG_reserved1                 = 42,  /* Unused, reserved. ignore */
  SFG_keyRange                  = 43,  /* @ MIDI ky#, 0 - 127, def:"0-127" */
  SFG_velRange                  = 44,  /* @ MIDI vel, 0 - 127, def:"0-127" */
  SFG_startloopAddrsCoarseOffset= 45,  /* + smpls, def:0 */
  SFG_keynum                    = 46,  /* +@ MIDI ky#, 0 - 127, def:-1 (none) */
  SFG_velocity                  = 47,  /* +@ MIDI vel, 0 - 128, def:-1 (none) */
  SFG_initialAttenuation        = 48,  /* cB, 0 - 1440, def:0 (1440=144dB) */
  SFG_reserved2                 = 49,  /* Unused, reserved. ignore */
  SFG_endloopAddrsCoarseOffset  = 50,  /* + smpls, def:0 */
  SFG_coarseTune                = 51,  /* semitone, -120 - 120, def:0 */
  SFG_fineTune                  = 52,  /* cent,  -99 - 99, def:0 */
  SFG_sampleID                  = 53,  /* index into shdr chunk to use */
  SFG_sampleModes               = 54,  /* +@ Bit Flags, Flags, def:0 */
  SFG_reserved3                 = 55,  /* Unused, reserved. ignore */
  SFG_scaleTuning               = 56,  /* @ cent/key, 0 - 1200, def:100 */
  SFG_exclusiveClass            = 57,  /* +@ arbitrary#, 0 - 127, def:0 */
  SFG_overridingRootKey         = 58,  /* +@ MIDI ky#, 0 - 127, def:-1 (none) */
  SFG_unused5                   = 59,  /* Unused, reserved. ignore */
  SFG_endOper                   = 60,  /* Unused, reserved. terminates list */
  SFG_bits = 0x7fff /* force to 16 bit enum */
} SFGenerator;

typedef enum __attribute__ ((__packed__)) {
  /* 8.2.1 Source Enumerator Controller Palettes */
  SFM_noController      = 0,  /* no controller is to be used, treated as if 1 */
  SFM_noteOnVelocity    = 2,  /* midi velocity which generated sampled sound */
  SFM_noteOnKeyNumber   = 3,  /* midi note number, generated sampled sound */
  SFM_polyPressure      = 10, /* poly-pressure amount sent from midi cmd */
  SFM_channelPressure   = 13, /* channel-pressure amount sent from midi cmd */
  SFM_pitchWheel        = 14, /* pitch wheel amount sent from midi cmd */
  SFM_wheelSensitivity  = 16, /* RPN0 amount sent from midi RPN0 cmd */
  SFM_link              = 127,/* controller source is output of another mod */
  SFM_bits = 0x7fff /* force to 16 bit enum */
} SFModulator;

typedef enum __attribute__ ((__packed__)) {
  /* 8.3 Modulator Transform Enumerators */
  SFT_linear            = 0,  /* output value fed directly into the summing */
  SFT_absoluteValue     = 1,  /* output value is abs value of the input */
  SFT_bits = 0x7fff /* force to 16 bit enum */
} SFTransform;

/* 4.4 SoundFont 2 RIFF File Format Level 3 */
/* specification to which the soundfont complies */
struct __attribute__ ((__packed__)) sfVersionTag {  /* iver */
  Uint16 wMajor;
  Uint16 wMinor;
}; 

struct __attribute__ ((__packed__)) sfPresetHeader {  /* phdr */
  char achPresetName[20];
  Uint16 wPreset;
  Uint16 wBank;
  Uint16 wPresetBagNdx;
  Uint32 dwLibrary;
  Uint32 dwGenre;
  Uint32 dwMorphology;
}; 

struct __attribute__ ((__packed__)) sfPresetBag {  /* pbag */
  Uint16 wGenNdx;
  Uint16 wModNdx;
}; 

struct __attribute__ ((__packed__)) sfModList {  /* pmod */
  SFModulator sfModSrcOper;
  SFGenerator sfModDestOper;
  Sint16 modAmount;
  SFModulator sfModAmtSrcOper;
  SFTransform sfModTransOper;
}; 

struct __attribute__ ((__packed__)) sfGenList {  /* pgen */
  SFGenerator sfGenOper;
  genAmountType genAmount;
}; 

struct __attribute__ ((__packed__)) sfInst {  /* inst */
  char achInstName[20];
  Uint16 wInstBagNdx;
};

struct __attribute__ ((__packed__)) sfInstBag {  /* ibag */
  Uint16 wInstGenNdx;
  Uint16 wInstModNdx;
};

struct __attribute__ ((__packed__)) sfInstModList {  /* imod */
  SFModulator sfModSrcOper;
  SFGenerator sfModDestOper;
  Sint16 modAmount;
  SFModulator sfModAmtSrcOper;
  SFTransform sfModTransOper;
}; 

struct __attribute__ ((__packed__)) sfInstGenList {  /* igen */
  SFGenerator sfGenOper;
  genAmountType genAmount;
};

struct __attribute__ ((__packed__)) sfSample {  /* shdr */
  char achSampleName[20];
  Uint32 dwStart;
  Uint32 dwEnd;
  Uint32 dwStartloop;
  Uint32 dwEndloop;
  Uint32 dwSampleRate;
  Uint8 byOriginalKey;
  Sint8 chCorrection;
  Uint16 wSampleLink;
  SFSampleLink sfSampleType;
};

/* this structure is filled out pointing to relevant bits in the loaded file */
/* note: the Uint32 size *MUST* directly follow each section pointer */
struct sfSFBK {
  struct sfVersionTag *ifil;    // version of the sound font riff file
  Uint32 ifil_size;             // size of ifil chunk in bytes
  char *isng;                   // target sound engine
  Uint32 isng_size;             // size of isng chunk in bytes
  char *INAM;                   // sound font bank name
  Uint32 INAM_size;             // size of INAM chunk in bytes
  char *irom;                   // sound rom name (optional)
  Uint32 irom_size;             // size of irom chunk in bytes
  struct sfVersionTag *iver;    // sound rom version (opt)
  Uint32 iver_size;             // size of iver chunk in bytes
  char *ICRD;                   // creation date of the bank
  Uint32 ICRD_size;             // size of ICRD chunk in bytes
  char *IENG;                   // sound designers and engineers for the bank
  Uint32 IENG_size;             // size of IENG chunk in bytes
  char *IPRD;                   // product for which the bank was intended
  Uint32 IPRD_size;             // size of IPRD chunk in bytes
  char *ICOP;                   // copyright message
  Uint32 ICOP_size;             // size of ICOP chunk in bytes
  char *ICMT;                   // bank comments, if any
  Uint32 ICMT_size;             // size of ICMT chunk in bytes
  char *ISFT;                   // soundfont tool used to create/edit file
  Uint32 ISFT_size;             // size of ISFT chunk in bytes
  short *smpl;                  // digital audio samples for upper 16 bits
  Uint32 smpl_size;             // size of smpl chunk in bytes
  Uint8 *sm24;                  // samples for lower 8 bits (for 24-bit, opt)
  Uint32 sm24_size;             // size of sm24 chunk in bytes
  struct sfPresetHeader *phdr;  // array of all presets within file (req)
  Uint32 phdr_size;             // size of phdr chunk in bytes
  struct sfPresetBag *pbag;     // array of all preset zones within file (req)
  Uint32 pbag_size;             // size of pbag chunk in bytes
  struct sfModList *pmod;       // all preset zone modulators in file (req)
  Uint32 pmod_size;             // size of pmod chunk in bytes
  struct sfGenList *pgen;       // all preset zone generators in file (req)
  Uint32 pgen_size;             // size of pgen chunk in bytes
  struct sfInst *inst;          // array of all instruments in file (req)
  Uint32 inst_size;             // size of inst chunk in bytes
  struct sfInstBag *ibag;       // array of all instrument zones in file (req)
  Uint32 ibag_size;             // size of ibag chunk in bytes
  struct sfModList *imod;       // array of all inst zone mods in file (req)
  Uint32 imod_size;             // size of imod chunk in bytes
  struct sfInstGenList *igen;   // all insturment zone generators in file (req)
  Uint32 igen_size;             // size of igen chunk in bytes
  struct sfSample *shdr;        // array of all samples within smpl chunk (req)
  Uint32 shdr_size;             // size of shdr chunk in bytes
};

extern struct sfSFBK sf2;  /* pointers to everything loaded go here */
extern struct riffChunk *load_riff(char *filename); /* load soundfont */
