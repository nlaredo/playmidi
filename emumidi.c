/* emumidi.c  -  software synth engine with optional hardware midi output
 * CoreMIDI is the output assumed here, api emulated if needed for
 * cross-platform support.
 *
 *  Copyright 2015 Nathan Laredo (laredo@gnu.org)
 *
 * This file may be freely distributed under the terms of
 * the GNU General Public Licence (GPL).
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <math.h>

#include "playmidi.h"

/* fixme: these things should move inside a structure and include */
extern int play_ext;
extern int chanmask, perc, dochan, MT32;
extern Uint32 ticks;
extern int useprog[16];
extern char *sf2_filename;
#define CHANNEL (dochan ? chn : 0)

#define SAMPLELEN 512
#define SAMPLERATE 96000
#define PACKET_LIST_BYTES 65536
#define POLYMAX 128

static float rate = SAMPLERATE;
int channels = 2;
static SDL_AudioDeviceID sdl_dev = 0;

static Uint8 pdata[PACKET_LIST_BYTES];  // space for queued midi events
struct midi_packet *tseq = (void *)pdata;  // queued midi events to play
struct midi_packet *tseqh = (void *)pdata;  // enqueue position in above
struct midi_packet *tseqt = (void *)pdata;  // dequeue position in above

struct voicestate voice[POLYMAX];  // active voices, samples = 0 = inactive
struct chanstate channel[16];  // presently active channel state
Uint64 samplepos = 0;  // current position in the sample output
float atune = 440.0;  // this will affect all midi note conversions

// convert a negative cB value to linear 0 - 1.0
float cB_to_linear(float cB)
{
  if (cB > 0.0) {
    return 1.0;
  }
  return pow(10, cB / 200.0);
}

// convert a floating point frequency to intger midi note number
Uint8 freq_to_note(float freq)
{
  float d = 69 + 12 * log2(freq / atune);
  return (Uint8) d;
}

// convert a midi note number to floating point frequency
float note_to_freq(Uint8 note, Uint16 centsperkey)
{
  float freq = pow(2, ((float)(note) - 69.0) *
        (float) centsperkey / 1200.0) * atune;
  return freq;
}

// convert a controller scaled cents value to frequency multiplier
float cents_to_freqmult(float cents, Uint16 num, Uint16 den)
{
  return pow(2, (float)num * (cents / 1200.0) / (float)den);
}

// convert a midi pitchbend to a frequency multiplier
float pitchbend_to_freqmult(Uint16 pitchbend, Uint16 rpn0)
{
  float mult, range = rpn0;

  if (range > 12) {  // roland ignores ranges larger than one octave
    range = 2;  // and seems to produce the default range instead
  }

  mult = pow(2, ((float)(pitchbend) - 8192.0) * (range / 12.0) / 8192.0);
  return mult;
}

void apply_generators(int min, int max, void *g, int j)
{
  // static values are reinitialized after applying the final generators
  static int newnote = -1;
  static int coarseTune = 0, fineTune = 0, scaleTuning = 100;
  static int sOff = 0, eOff = 0, sLoopOff = 0, eLoopOff = 0;
  struct sfGenList *gen = g;
  int p;

  for (p = min; p < max; p++) {
    switch (gen[p].sfGenOper) {
      case SFG_startAddrsOffset:
        sOff += gen[p].genAmount.shAmount;
        break;
      case SFG_endAddrsOffset:
        eOff += gen[p].genAmount.shAmount;
        break;
      case SFG_startloopAddrsOffset:
        sLoopOff += gen[p].genAmount.shAmount;
        break;
      case SFG_endloopAddrsOffset:
        eLoopOff += gen[p].genAmount.shAmount;
        break;
      case SFG_startAddrsCoarseOffset:
        sOff += gen[p].genAmount.shAmount * 32768;
        break;
      case SFG_modLfoToPitch:
        break;
      case SFG_vibLfoToPitch:
        break;
      case SFG_modEnvToPitch:
        break;
      case SFG_initialFilterFc:
        break;
      case SFG_initialFilterQ:
        break;
      case SFG_modLfoToFilterFc:
        break;
      case SFG_endAddrsCoarseOffset:
        eOff += gen[p].genAmount.shAmount * 32768;
        break;
      case SFG_modLfoToVolume:
        break;
      case SFG_chorusEffectsSend:
        break;
      case SFG_reverbEffectsSend:
        break;
      case SFG_pan:
        {
          float pan = gen[p].genAmount.shAmount;
          pan /= 500.0;     // new range -1.0 to 1.0
          pan += 1.0;       // new range  0.0 to 2.0
          pan /= 2.0;       // new range  0.0 to 1.0
          voice[j].pan = pan;
        }
        break;
      case SFG_delayModLFO:
        break;
      case SFG_freqModLFO:
        break;
      case SFG_delayVibLFO:
        break;
      case SFG_freqVibLFO:
        break;
      case SFG_delayModEnv:
        break;
      case SFG_attackModEnv:
        break;
      case SFG_holdModEnv:
        break;
      case SFG_decayModEnv:
        break;
      case SFG_sustainModEnv:
        break;
      case SFG_releaseModEnv:
        break;
      case SFG_keynumToModEnvHold:
        break;
      case SFG_keynumToModEnvDecay:
        break;
      case SFG_delayVolEnv:
        voice[j].timestamp += rate *
          cents_to_freqmult(gen[p].genAmount.shAmount, 1, 1);
        break;
      case SFG_attackVolEnv:
        voice[j].env.a = rate *
          cents_to_freqmult(gen[p].genAmount.shAmount, 1, 1);
        break;
      case SFG_holdVolEnv:
        voice[j].env.h = rate *
          cents_to_freqmult(gen[p].genAmount.shAmount, 1, 1);
        break;
      case SFG_decayVolEnv:
        voice[j].env.d = rate *
          cents_to_freqmult(gen[p].genAmount.shAmount, 1, 1);
        break;
      case SFG_sustainVolEnv:
        if (gen[p].genAmount.shAmount > 1440) {
          gen[p].genAmount.shAmount = 1440;
        }
        // values less than zero are effectively zero with no decay time
        if (gen[p].genAmount.shAmount <= 0) {
          voice[j].env.d = 0;
          voice[j].env.s = 1.0;
        } else {
          voice[j].env.s = cB_to_linear(0 - (float)gen[p].genAmount.shAmount);
        }
        break;
      case SFG_releaseVolEnv:
        voice[j].env.r = rate *
          cents_to_freqmult(gen[p].genAmount.shAmount, 1, 1);
        break;
      case SFG_keynumToVolEnvHold:
        voice[j].env.h *=
          cents_to_freqmult((float)(60 - voice[j].note) *
                (float)gen[p].genAmount.shAmount, 1, 1);
        break;
      case SFG_keynumToVolEnvDecay:
        voice[j].env.d *=
          cents_to_freqmult((float)(60 - voice[j].note) *
                (float)gen[p].genAmount.shAmount, 1, 1);
        break;
      case SFG_startloopAddrsCoarseOffset:
        sLoopOff += gen[p].genAmount.shAmount * 32768;
        break;
      case SFG_keynum:
        voice[j].note = gen[p].genAmount.wAmount;
        break;
      case SFG_velocity:
        voice[j].v = (float)gen[p].genAmount.wAmount / 127.0;
        break;
      case SFG_initialAttenuation:
        if (gen[p].genAmount.wAmount > 1440) {
          gen[p].genAmount.wAmount = 1440;
        }
        voice[j].v *= cB_to_linear(0 - (float)gen[p].genAmount.wAmount);
        break;
      case SFG_endloopAddrsCoarseOffset:
        eLoopOff += gen[p].genAmount.shAmount * 32768;
        break;
      case SFG_coarseTune:
        coarseTune = gen[p].genAmount.shAmount;
        break;
      case SFG_fineTune:
        fineTune = gen[p].genAmount.shAmount;
        break;
      case SFG_sampleModes:
        voice[j].s.sampleModes = gen[p].genAmount.wAmount;
        break;
      case SFG_scaleTuning:
        scaleTuning = gen[p].genAmount.wAmount;
        break;
      case SFG_exclusiveClass:
        voice[j].exclusive_class = gen[p].genAmount.wAmount;
        break;
      case SFG_overridingRootKey:
        newnote = gen[p].genAmount.shAmount;
        break;
      default:
        break;
    }
  }
  if (voice[j].shdr >= 0) {
    int s = voice[j].shdr;
    // finalize application of generator values
    voice[j].s.dwStart = sf2.shdr[s].dwStart + sOff;
    voice[j].s.dwEnd = sf2.shdr[s].dwEnd + eOff;
    voice[j].s.dwStartloop = sf2.shdr[s].dwStartloop + sLoopOff;
    voice[j].s.dwEndloop = sf2.shdr[s].dwEndloop + eLoopOff;
    voice[j].f = note_to_freq(voice[j].note, scaleTuning);
    voice[j].r = voice[j].f / note_to_freq(newnote < 0 ?
        sf2.shdr[s].byOriginalKey : newnote, scaleTuning) *
        ((float)sf2.shdr[s].dwSampleRate / rate);
    voice[j].r *= cents_to_freqmult(coarseTune * 100.0, 1, 1);
    voice[j].r *= cents_to_freqmult(fineTune, 1, 1);
    voice[j].r *= cents_to_freqmult(sf2.shdr[s].chCorrection, 1, 1);
    if (voice[j].exclusive_class) {
      for (s = 0; s < POLYMAX; s++) {
        if (s != j && voice[s].channel == voice[j].channel &&
            voice[s].exclusive_class == voice[j].exclusive_class) {
          voice[s].samples = 0;  // terminate voice
        }
      }
    }
    // reinitialize static generator values to sf2 defaults
    newnote = -1; coarseTune = 0; fineTune = 0; scaleTuning = 100;
    sOff = 0; eOff = 0; sLoopOff = 0; eLoopOff = 0;
  }
}

struct midi_packet *next_pkt(struct midi_packet *p)
{
  p = (struct midi_packet *)&(p)->data[(p)->len];
  if ((Uint8 *)p >
      pdata + PACKET_LIST_BYTES - (sizeof(struct midi_packet) + 5)) {
    p = tseq;  /* wrap around to start of buffer */
  }
  return p;
}

static struct midi_packet *add_pkt(struct midi_packet *p)
{
  /* timestamp is in samples since start of output */
  p->timestamp = ticks * rate / 1000.0;
  if (ISMIDI((p->data[0] & 0xf))) {
    midi_add_pkt(p);
    return p;
  }
  p = next_pkt(p);
  /* make sure there is room for at least one more 5 byte midi packet */
  if ((Uint8 *)p >
      pdata + PACKET_LIST_BYTES - (sizeof(struct midi_packet) + 5)) {
    p->len = 0;  /* mark last packet before wrap around */
    p = tseq;  /* wrap around to start of buffer */
  }
  /* if head reaches tail after adding, buffer is full but looks empty */
  if (p == tseqt) {
    fprintf(stderr, "midi packet buffer too small\n");
    exit(1);
  }
  return p;
}

// fill_audio(): callback that will fill supplied buffer with audio data
// udata: parameter supplied in SDL_AudioSpec userdata field
// stream: pointer to the audio data buffer to be filled
// len: the length of that buffer in bytes
void fill_audio(void *udata, Uint8 *stream, int len)
{
  float vmod;  // volume mod for ADSR implementation
  static float tlfo = 0.0;
  static float rlfo = 0.0;
  float left, right, lfo;
  int i, j, ch, pgm, voices;
  static float normalize = 1.0;
  float *f32s = (float *)stream;
  len >>= 3; // convert from bytes to samples

  if (rlfo == 0) {
    rlfo = 2.0 * M_PI * 8.176 / rate; // 8.176hz lfo by default
  }
  for (i = 0; i < len; i++) {
    // triangle
    lfo = fabs(0.3184 * (tlfo - M_PI)) - 1.0;
    //lfo = sin(tlfo);
    tlfo += rlfo;
    if (tlfo > 2 * M_PI) {
      tlfo -= 2 * M_PI;
    }
    while (tseqh != tseqt && tseqt->timestamp <= samplepos) {
      /* found midi event starting at this sample position to process */
      int cmd = tseqt->data[0];
      ch = cmd & 0xf;
      switch (cmd & 0xf0) {
        case MIDI_NOTEOFF:
          for (j = 0; j < POLYMAX; j++) {
            if (voice[j].channel == ch &&
                voice[j].note == tseqt->data[1]) {
              if (channel[ch].controller[CTL_SUSTAIN] >= 64) {
                voice[j].sustain = 1;
                continue;
              }
              voice[j].samples = samplepos - voice[j].timestamp +
                voice[j].env.r;
              if (voice[j].s.sampleModes != 1) {
                voice[j].s.sampleModes = 0;  // tell voice to finish past loop
              }
              break;
            }
          }
          break;
        case MIDI_NOTEON:
          /* find an empty voice to use for note start */
          pgm = POLYMAX;
          for (j = 0; j < POLYMAX; j++) {
            if (voice[j].channel == ch && voice[j].note == tseqt->data[1]) {
              pgm = j;
              break;  // steal note already playing on same channel
            }
            if (voice[j].samples <= 0) {
              pgm = j;
            }
          }
          j = pgm;
          if (j >= POLYMAX) {  /* steal oldest voice if none free */
            Uint64 oldest = ~0;
            int jold = j;
            for (j = 0; j < POLYMAX; j++) {
              if (voice[j].timestamp < oldest) {
                oldest = voice[j].timestamp;
                jold = j;
              }
            }
            j = jold;
          }
          if (j < POLYMAX) {
            memset(&voice[j], 0, sizeof(voice[j]));
            voice[j].note = tseqt->data[1];
            voice[j].f = note_to_freq(voice[j].note, 100);
            voice[j].r = 2 * M_PI * voice[j].f / rate;
            voice[j].v = (float)tseqt->data[2] / 128.0;
            voice[j].t = 0.0;
            voice[j].env.a = cents_to_freqmult(-12000, 1, 1) * rate;
            voice[j].env.h = voice[j].env.a;
            voice[j].env.d = voice[j].env.a;
            voice[j].env.s = 1.0;
            voice[j].env.r = voice[j].env.a;
            voice[j].pan = (float)channel[ch].controller[CTL_PAN] / 127.0;
            voice[j].channel = ch;
            voice[j].samples = 0x7fffffff;  // set at noteoff event
            voice[j].timestamp = samplepos;
            voice[j].inst = -1;  // not found
            voice[j].shdr = -1;  // not found
            if (sf2.shdr) {
              int p, zone, bank, count, range, velrange;
              int vel = (int)(127.0 * voice[j].v);
              pgm = channel[ch].program;
              bank = channel[ch].controller[CTL_BANK_SELECT];
              bank <<= 7;
              bank |= channel[ch].controller[CTL_BANK_SELECT + CTL_LSB];
              if (bank == 128) {
                bank = 0; /* soundfonts use bank 128 for percussion */
              }
              if (ISPERC(ch)) {
                bank = 128; /* soundfonts use bank 128 for percussion */
              }
              // find the preset that matches the program
              count = sf2.phdr_size / sizeof(struct sfPresetHeader);
              for (p = 0; p + 1 < count; p++) {
                if (sf2.phdr[p].wBank == bank && bank == 128 &&
                  sf2.phdr[p].wPreset <= pgm) {
                  voice[j].phdr = p; /* default to first percussion match */
                }
                if (sf2.phdr[p].wPreset == pgm) {
                  if (sf2.phdr[p].wBank == 0 && bank != 128) {
                    voice[j].phdr = p; /* default to bank 0 match */
                  }
                  if (sf2.phdr[p].wBank == bank) {
                      break;
                  }
                }
              }
              if (p + 1 < count) {
                voice[j].phdr = p;
              }
              voice[j].pbag = sf2.phdr[voice[j].phdr].wPresetBagNdx;
              voice[j].pbag_max = sf2.phdr[voice[j].phdr + 1].wPresetBagNdx;
              for (zone = voice[j].pbag; zone < voice[j].pbag_max; zone++) {
                voice[j].pgen = sf2.pbag[zone].wGenNdx;
                voice[j].pmod = sf2.pbag[zone].wModNdx;
                voice[j].pgen_max = sf2.pbag[zone + 1].wGenNdx;
                voice[j].pmod_max = sf2.pbag[zone + 1].wModNdx;
                range = velrange = 1;
                for (p = voice[j].pgen; p < voice[j].pgen_max; p++) {
                  if (sf2.pgen[p].sfGenOper == SFG_keyRange) {
                    if (sf2.pgen[p].genAmount.ranges.byLo <= voice[j].note &&
                        sf2.pgen[p].genAmount.ranges.byHi >= voice[j].note) {
                      range = 1;
                    } else {
                      range = 0;
                    }
                  }
                  if (sf2.pgen[p].sfGenOper == SFG_velRange) {
                    if (sf2.pgen[p].genAmount.ranges.byLo <= vel &&
                        sf2.pgen[p].genAmount.ranges.byHi >= vel) {
                      velrange = 1;
                    } else {
                      velrange = 0;
                    }
                  }
                  if (sf2.pgen[p].sfGenOper == SFG_instrument) {
                    if (range && velrange) {
                      voice[j].inst = sf2.pgen[p].genAmount.wAmount;
                      apply_generators(voice[j].pgen, voice[j].pgen_max,
                                       sf2.pgen, j);
                    }
                    break; // instrument is terminal for zone
                  }
                }
                if (zone == voice[j].pgen && p == voice[j].pgen_max) {
                  // apply global zone generotors
                  apply_generators(voice[j].pgen, voice[j].pgen_max,
                                   sf2.pgen, j);
                }
                if (voice[j].inst >= 0) {
                  break;  // found relevant zone
                }
              }
              if (voice[j].inst < 0) {
                // failed to find suitable instrument
                // ibag/ibag_max were memset to 0 earlier
                // for loop below will exit early
              } else {
                voice[j].ibag = sf2.inst[voice[j].inst].wInstBagNdx;
                voice[j].ibag_max = sf2.inst[voice[j].inst + 1].wInstBagNdx;
              }
              for (zone = voice[j].ibag; zone < voice[j].ibag_max; zone++) {
                voice[j].igen = sf2.ibag[zone].wInstGenNdx;
                voice[j].imod = sf2.ibag[zone].wInstModNdx;
                voice[j].igen_max = sf2.ibag[zone + 1].wInstGenNdx;
                voice[j].imod_max = sf2.ibag[zone + 1].wInstModNdx;
                range = velrange = 1;
                for (p = voice[j].igen; p < voice[j].igen_max; p++) {
                  if (sf2.igen[p].sfGenOper == SFG_keyRange) {
                    if (sf2.igen[p].genAmount.ranges.byLo <= voice[j].note &&
                        sf2.igen[p].genAmount.ranges.byHi >= voice[j].note) {
                      range = 1;
                    } else {
                      range = 0;
                    }
                  }
                  if (sf2.igen[p].sfGenOper == SFG_velRange) {
                    if (sf2.igen[p].genAmount.ranges.byLo <= vel &&
                        sf2.igen[p].genAmount.ranges.byHi >= vel) {
                      velrange = 1;
                    } else {
                      velrange = 0;
                    }
                  }
                  if (sf2.igen[p].sfGenOper == SFG_sampleID) {
                    if (range && velrange) {
                      voice[j].shdr = sf2.igen[p].genAmount.wAmount;
                      apply_generators(voice[j].igen, voice[j].igen_max,
                                       sf2.igen, j);
                    }
                    break; // instrument is terminal for zone
                  }
                }
                if (zone == voice[j].igen && p == voice[j].igen_max) {
                  // apply global zone generotors
                  apply_generators(voice[j].igen, voice[j].igen_max,
                                   sf2.igen, j);
                }
                if (voice[j].shdr >= 0) {
                  break;  // found relevant zone
                }
              }
              if (voice[j].shdr < 0) {
                /* failed to find suitable sampleID, free voice */
                voice[j].samples = 0;
              }
            } else {
              if (ISPERC(ch)) {
                /* kill percussion for non-sf2 voice */
                voice[j].samples = 0;
              }
              voice[j].env.r = rate/16;
              voice[j].env.d = rate/16;
              voice[j].env.s = 0.4;
              voice[j].env.a = rate/64;
            }
          }
          break;
        case MIDI_KEY_PRESSURE:
          // todo: find voice, do something to it
          break;
        case MIDI_CTL_CHANGE:
          channel[ch].controller[tseqt->data[1]] = tseqt->data[2];
          /* handle RPN/NRPN */
          if (tseqt->data[1] == CTL_DATA_ENTRY) {
             if (channel[ch].controller[CTL_RPN_LSB] == 0 &&
                 channel[ch].controller[CTL_RPN_MSB] == 0) {
                channel[ch].bender_range = tseqt->data[2];
            }
          }
          if (tseqt->data[1] == CTL_MODWHEEL) {
            channel[ch].mod_mult =
                cents_to_freqmult(47, tseqt->data[2], 127) - 1.0;
          }
          if (tseqt->data[1] == CTL_SUSTAIN && tseqt->data[2] < 64) {
            for (j = 0; j < POLYMAX; j++) {
              if (voice[j].channel == ch && voice[j].sustain) {
                voice[j].sustain = 0;
                voice[j].samples = samplepos - voice[j].timestamp +
                  voice[j].env.r;
                if (voice[j].s.sampleModes != 1) {
                  voice[j].s.sampleModes = 0;  // finish past loop
                }
              }
            }
          }
          break;
        case MIDI_PGM_CHANGE:
          channel[ch].program = tseqt->data[1];
          break;
        case MIDI_CHN_PRESSURE:
          channel[ch].pressure = tseqt->data[1];
          break;
        case MIDI_PITCH_BEND:
          channel[ch].bender = tseqt->data[2];
          channel[ch].bender <<= 7;
          channel[ch].bender |= tseqt->data[1];
          channel[ch].bender_mult = pitchbend_to_freqmult(channel[ch].bender,
                channel[ch].bender_range);
          break;
        default:
          fprintf(stderr, "\r(unhandled midi cmd = 0x%02x)\n", cmd);
          exit(1);
      }
      //memset(&tseqt->data[0], 0, tseqt->len); /* debug: kill off event data */
      tseqt = next_pkt(tseqt);
    }
    left = 0.0;
    right = 0.0;
    for (j = voices = 0; j < POLYMAX; j++) {
      float sample, t;
      int tpos, rpos;
      if (voice[j].samples == 0) {
        continue;
      }
      voices++;
      tpos = samplepos - voice[j].timestamp;  // sample # since attack start
      rpos = voice[j].timestamp + voice[j].samples - samplepos; // release pos
      t = voice[j].t;  // each voice has its own timebase
      if (tpos < voice[j].env.a) {
        // attack phase
        vmod = (float)tpos / voice[j].env.a;
      } else if (tpos < voice[j].env.a + voice[j].env.h) {
        // hold phase
        vmod = 1.0;
      } else if (tpos < voice[j].env.a + voice[j].env.h + voice[j].env.d) {
        // decay phase
        vmod = ((float)tpos - (voice[j].env.a + voice[j].env.h)) /
                voice[j].env.d;
        vmod *= 1.0 - voice[j].env.s;
        vmod = 1.0 - vmod;   // range from 1.0 down to env.s
      } else {
        // sustain phase
        vmod = voice[j].env.s;
        if (vmod <= 0.000001) {  // kill voice when it can't be heard anymore
          voice[j].samples = 0;
        }
      }
      if (rpos < voice[j].env.r) {
        // release phase, go from calculated envelope position down to zero
        // cubic decay, vmod *= (rpos/env.r)^3
        float x = (float)rpos / voice[j].env.r;
        vmod *= x * x * x;
      }
      ch = voice[j].channel;
      pgm = channel[ch].program;
      vmod *= (float)channel[ch].controller[CTL_MAIN_VOLUME] / 127.0;
      vmod *= (float)channel[ch].controller[CTL_EXPRESSION] / 127.0;
      if (voice[j].shdr < 0) {
        pgm = -pgm - 2;  // do math based synthesis
        if (t > 2 * M_PI) {
          t -= 2 * M_PI;
        }
        if (t < 0) {
          t += 2 * M_PI;
        }
      } else {
        if ((voice[j].s.sampleModes & 1) &&
            t + voice[j].s.dwStart >= voice[j].s.dwEndloop) {
          t = voice[j].s.dwStartloop - voice[j].s.dwStart;
        }
        if (t + voice[j].s.dwStart >= voice[j].s.dwEnd) {
          voice[j].samples = 0;  // kill off voice when completely played
        }
      }
      if (pgm <= -1) { // sine
        sample = sin(t);
      } else if (pgm < 0) {  // for all othe negative values be a minimoog
        // morph between tri, saw, square, rect wave full negative pgm value
        float tri, saw, squ;
        float pwm = (lfo + 1.0) * 0.98;
        tri = (fabs(0.3184 * (t - M_PI)) - 1.0);
        saw = 0.3184 * (t - M_PI);
        squ = (t > M_PI * pwm ? -1.0 : 1.0);
        sample = saw; //squ * pwm + saw * (2.0 - pwm);
      } else { // wavetable
        int index = voice[j].s.dwStart + (int)t;
        // cubic interpolate samples
        // more info: http://paulbourke.net/miscellaneous/interpolation/
        float mu = t - (int)t, mu2 = mu * mu;
        float a0, a1, a2, a3;
        float y0, y1, y2, y3;
        y0 = (float)sf2.smpl[index++];
        if ((voice[j].s.sampleModes & 1) && index >= voice[j].s.dwEndloop) {
          index = voice[j].s.dwStartloop;
        } else if (index > voice[j].s.dwEnd) {
          index = voice[j].s.dwEnd;
        }
        y1 = (float)sf2.smpl[index++];
        if ((voice[j].s.sampleModes & 1) && index >= voice[j].s.dwEndloop) {
          index = voice[j].s.dwStartloop;
        } else if (index > voice[j].s.dwEnd) {
          index = voice[j].s.dwEnd;
        }
        y2 = (float)sf2.smpl[index++];
        if ((voice[j].s.sampleModes & 1) && index >= voice[j].s.dwEndloop) {
          index = voice[j].s.dwStartloop;
        } else if (index > voice[j].s.dwEnd) {
          index = voice[j].s.dwEnd;
        }
        y3 = (float)sf2.smpl[index];
        a0 = y3 - y2 - y0 + y1;
        a1 = y0 - y1 - a0;
        a2 = y2 - y0;
        a3 = y1;
        sample = a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3;
        sample *= (1.0 / 32767.0);
      }
      sample *= voice[j].v * vmod;
      if (voice[j].pan < (float)channel[ch].controller[CTL_PAN] / 127.0) {
        float delta = (float)channel[ch].controller[CTL_PAN] / 127.0 -
                      voice[j].pan;
        voice[j].pan += delta/rate;  // smooth pan to target in 1s
      }
      if (voice[j].pan > (float)channel[ch].controller[CTL_PAN] / 127.0) {
        float delta = voice[j].pan -
                      (float)channel[ch].controller[CTL_PAN] / 127.0;
        voice[j].pan -= delta/rate;  // smooth pan to target in 1s
      }
      left += sample * (1.0 - voice[j].pan);
      right += sample * voice[j].pan ;
      t += voice[j].r * channel[ch].bender_mult *
        (channel[ch].mod_mult * lfo + 1.0);
      voice[j].t = t;  // save in per-voice timebase
      if (rpos <= 1) {
        voice[j].samples = 0;
        voice[j].timestamp = ~0;
      }
    }
    /* limit samples to range from -1.0 to 1.0 */
    if (abs(left * normalize) > 0.9 || abs(right * normalize) > 0.9) {
      if (abs(left) > abs(right)) {
        normalize = 1.0 / abs(left);
      } else {
        normalize = 1.0 / abs(right);
      }
    }
    left *= normalize;
    right *= normalize;
    f32s[i * 2] = left;
    f32s[i * 2 + 1] = right;
    samplepos++;
  }
}

void save_audio(char *filename)
{
  SDL_RWops *rw = SDL_RWFromFile(filename, "w");
  int len = 0;                  // TODO: calculate number of samples to save
  Uint8 *buf = malloc(len);     // space for everything at once
  if (!buf) {
    perror("malloc");
    return;
  }
  if (!rw) {
    perror(filename);
    return;
  }

  // wave file header, 16 bit stereo
  SDL_WriteBE32(rw, 'RIFF');    // RIFF chunk container
  SDL_WriteLE32(rw, len + 44 - 8);  // count of 'RIFF' chunk data bytes
  SDL_WriteBE32(rw, 'WAVE');    // RIFF chunk data type = WAVE
  SDL_WriteBE32(rw, 'fmt ');    // 'fmt ' chunk 
  SDL_WriteLE32(rw, 16);        // count of 'fmt ' chunk data bytes
  SDL_WriteLE16(rw, 1);         // compression code: 1 = PCM, 3 = float
  SDL_WriteLE16(rw, 2);         // number of channels = 2
  SDL_WriteLE32(rw, (int)rate); // sample rate = rate
  SDL_WriteLE32(rw, 2 * 2 * (int)rate);  // bytes per second
  SDL_WriteLE16(rw, 2 * 2);     // number of bytes per sample slice
  SDL_WriteLE16(rw, 16);        // significant bits per sample, float=32 or 64
  SDL_WriteBE32(rw, 'data');    // 'data' chunk
  // assume no error on last header write means the previous writes all worked
  if (SDL_WriteLE32(rw, len) < 1) {  // count of 'data' chunk data bytes
    perror(filename);
  }

  fill_audio(NULL, buf, len);  // render entire sequence to memory

  if (SDL_RWwrite(rw, buf, 1, len) != len) {
    perror(filename);
  } else {
    fprintf(stderr, "Wrote %d bytes\n", len);
  }
  SDL_RWclose(rw);
}

void open_sdl_dev(void)
{
  SDL_AudioSpec want, have;

  if (sdl_dev != 0) {
    return;  /* already opened */
  }
  SDL_zero(want);
  want.freq = SAMPLERATE;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = SAMPLELEN;
  want.callback = fill_audio;
  want.userdata = NULL;

  load_sf2(sf2_filename);
  SDL_Init(SDL_INIT_AUDIO);
  sdl_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have,
                                SDL_AUDIO_ALLOW_FORMAT_CHANGE);
  if (sdl_dev == 0) {
    fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
    exit(1);
  }
  if (want.freq != have.freq) {
    fprintf(stderr, "warning: wanted %d, got %d\n", want.freq, have.freq);
    rate = have.freq;
  }
  if (want.format != have.format) {
    fprintf(stderr, "warning: wanted 0x%x, got 0x%x\n",
            want.format, have.format);
  }
  if (want.channels != have.channels) {
    fprintf(stderr, "warning: wanted %dch, got %dch\n",
            want.channels, have.channels);
  }
}

void start_sdl_dev(void)
{
  SDL_PauseAudioDevice(sdl_dev, 0);  /* start filling audio buffer */
}

void stop_sdl_dev(void)
{
  SDL_PauseAudioDevice(sdl_dev, 1);  /* stop filling audio buffer */
}

void load_sysex(int length, Uint8 *data, int type)
{
    unsigned long int i;

    /*
     * If the system exclusive is for roland, evaluate it.  More than
     * roland could be evaluated here if i had documentation.  Please
     * submit patches for any other hardware to laredo@gnu.org
     * Complete emulation of all GS sysex messages in the works....
     */
    if (length > 7 && data[0] == 0x41 && data[2] == 0x42 && data[3] == 0x12) {
	/* GS DATA SET MESSAGES */
	if (data[4] == 0x40 && (data[5] & 0xf0) == 0x10 && data[6] == 0x15) {
		/* USE RHYTHM PART */
		if (!(i = (data[5] & 0xf)))
		    i = 0x09;
		else if (i < 10)
		    i--;
		i = 1<<i;
		if (data[7])
		    perc |= i;
		else
		    perc &= ~i;
	}
	if ((data[4] == 0x40 || data[4] == 0) &&
	    data[5] == 0x00 && data[6] == 0x7f) { /* GS RESET */
		perc = 0x0200;	/* percussion in channel 10 only */
		for (i = 0; i < 16; i++) {	/* set state info */
		    channel[i].bender = 8192;
		    channel[i].bender_range = 2;
		    channel[i].controller[CTL_PAN] = 64;
		    channel[i].controller[CTL_SUSTAIN] = 0;
    		}
	}
    }
    if (!play_ext)
	return;
    // send sysex to external midi device
    midi_send_sysex(length, data, type);
}

/* MT-32 emulation translate table */
static int mt32pgm[128] =
{
   0,   1,   2,   4,   4,   5,   5,   3,  16,  16,  16,  16,  19,
  19,  19,  21,   6,   6,   6,   7,   7,   7,   8,   8,  62,  57,
  63,  58,  38,  38,  39,  39,  88,  33,  52,  35,  97, 100,  38,
  39,  14, 102,  68, 103,  44,  92,  46,  80,  48,  49,  51,  45,
  40,  40,  42,  42,  43,  46,  46,  24,  25,  28,  27, 104,  32,
  32,  34,  33,  36,  37,  39,  35,  79,  73,  76,  72,  74,  75,
  64,  65,  66,  67,  71,  71,  69,  70,  60,  22,  56,  59,  57,
  63,  60,  60,  58,  61,  61,  11,  11,  99, 100,   9,  14,  13,
  12, 107, 106,  77,  78,  78,  76, 111,  47, 117, 127, 115, 118,
 116, 118, 126, 121, 121,  55, 124, 120, 125, 126, 127
};

void seq_set_patch(int chn, int pgm)
{
  if (MT32 && pgm < 128)
    pgm = mt32pgm[pgm];
  if (useprog[chn])
    pgm = useprog[chn] - 1;
  if (ISMIDI(chn)) {
    /* need program data tracked for external synth too */
    channel[chn].program = pgm;
  }
  tseqh->len = 2;
  tseqh->data[0] = MIDI_PGM_CHANGE | chn;
  tseqh->data[1] = pgm;
  tseqh = add_pkt(tseqh);
}

void seq_stop_note(int chn, int note, int vel)
{
  tseqh->len = 3;
  tseqh->data[0] = MIDI_NOTEOFF | chn;
  tseqh->data[1] = note;
  tseqh->data[2] = vel;
  tseqh = add_pkt(tseqh);
}

void seq_key_pressure(int chn, int note, int vel)
{
  tseqh->len = 3;
  tseqh->data[0] = MIDI_KEY_PRESSURE | chn;
  tseqh->data[1] = note;
  tseqh->data[2] = vel;
  tseqh = add_pkt(tseqh);
}

void seq_start_note(int chn, int note, int vel)
{
  if (vel == 0 && !ISMIDI(chn)) {
    seq_stop_note(chn, note, 127);
    return;
  }
  tseqh->len = 3;
  tseqh->data[0] = MIDI_NOTEON | chn;
  tseqh->data[1] = note;
  tseqh->data[2] = vel;
  tseqh = add_pkt(tseqh);
}

void seq_control(int chn, int p1, int p2)
{
  if (ISMIDI(chn)) {
    /* need controller data tracked for external synth too */
    channel[chn].controller[p1] = p2;
  }
  tseqh->len = 3;
  tseqh->data[0] = MIDI_CTL_CHANGE | chn;
  tseqh->data[1] = p1;
  tseqh->data[2] = p2;
  tseqh = add_pkt(tseqh);
}

void seq_chn_pressure(int chn, int vel)
{
  tseqh->len = 2;
  tseqh->data[0] = MIDI_CHN_PRESSURE | chn;
  tseqh->data[1] = vel;
  tseqh = add_pkt(tseqh);
}

void seq_bender(int chn, int p1, int p2)
{
  tseqh->len = 3;
  tseqh->data[0] = MIDI_PITCH_BEND | chn;
  tseqh->data[1] = p1;
  tseqh->data[2] = p2;
  tseqh = add_pkt(tseqh);
}

void seq_reset(void)
{
  int i;
  /* kill all playing voices */
  tseqh = tseqt = tseq;
  for (i = 0; i < POLYMAX; i++) {
    voice[i].samples = 0;
  }
  /* to keep midi in sync with soft synth, initialize both here */
  if (play_ext != chanmask) {
    /* if everything is not going to external midi */
    open_sdl_dev();  /* set up sdl audio device for soft playback */
  }
  if (play_ext & chanmask) {
    init_midi();
  }
  if (sdl_dev != 0) {
    /* if sdl_dev opend, start sdl audio to be in sync with external midi */
    start_sdl_dev();
  }
  for (i = 0; i < 16; i++) {	/* set state info */
      channel[i].bender_mult = 1.0;
      channel[i].bender = 8192;
      channel[i].bender_range = 2;
      channel[i].controller[CTL_PAN] = 64;
      channel[i].controller[CTL_SUSTAIN] = 0;
      channel[i].controller[CTL_EXPRESSION] = 127;
      seq_control(i, CTL_ALL_NOTES_OFF, 0);
      seq_control(i, CTL_ALL_SOUNDS_OFF, 0);
      seq_control(i, CTL_RESET_ALL_CONTROLLERS,0);
      seq_control(i, CTL_BANK_SELECT, 0);
      seq_control(i, CTL_BANK_SELECT + CTL_LSB, 0);
      seq_set_patch(i, 0);
  }
}
