/************************************************************************
   playevents.c  -- actually sends sorted list of events to device

   Copyright 2015 Nathan Laredo (laredo@gnu.org)

   This program is modifiable/redistributable under the terms
   of the GNU General Public Licence.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *************************************************************************/
#include "playmidi.h"
#include <sys/time.h>

extern void seq_set_patch(int, int);
extern void seq_key_pressure(int, int, int);
extern void seq_start_note(int, int, int);
extern void seq_stop_note(int, int, int);
extern void seq_control(int, int, int);
extern void seq_chn_pressure(int, int);
extern void seq_bender(int, int, int);
extern void seq_reset();
extern int graphics, verbose, division, ntrks, format;
extern int perc;
extern int play_ext, reverb, chorus, chanmask;
extern int usevol[16];
extern int mt32pgm[128], MT32;
extern struct miditrack seq[MAXTRKS];
extern float skew;
extern unsigned long int default_tempo;
extern void load_sysex(int, unsigned char *, int);
extern void showevent(int, unsigned char *, int);
extern void init_show();
extern int updatestatus();

Uint32 ticks, tempo;
Uint32 start_tick;
struct timeval start_time;
extern struct midi_packet *tseqh, *tseqt;

unsigned long int rvl(struct miditrack *s)
{
    register unsigned long int value = 0;
    register unsigned char c;

    if (s->index < s->length && ((value = s->data[(s->index)++]) & 0x80)) {
	value &= 0x7f;
	do {
	    if (s->index >= s->length)
		c = 0;
	    else
		value = (value << 7) +
		    ((c = s->data[(s->index)++]) & 0x7f);
	} while (c & 0x80);
    }
    return (value);
}

/* indexed by high nibble of command */
int cmdlen[16] = {0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 1, 1, 2, 0};

#define CHN		(seq[track].running_st & 0xf)
#define NOTE		data[0]
#define VEL		data[1]

int playevents(void)
{
    unsigned long int tempo = default_tempo, lasttime = 0;
    unsigned int lowtime, track, best, length;
    unsigned char *data;
    double current = 0.0, dtime = 0.0;
    int play_status, playing = 1;

    init_show();
    seq_reset();
    ticks = 0;
    start_tick = SDL_GetTicks();
    gettimeofday(&start_time, NULL);
    for (track = 0; track < ntrks && seq[track].data; track++) {
	seq[track].index = seq[track].running_st = 0;
	seq[track].ticks = rvl(&seq[track]);
    }
    for (best = 0; best < 16; best++) {
	seq_control(best, CTL_BANK_SELECT, 0);
	seq_control(best, CTL_REVERB_DEPTH, reverb);
	seq_control(best, CTL_CHORUS_DEPTH, chorus);
	seq_control(best, CTL_MAIN_VOLUME, 127);
	seq_chn_pressure(best, 127);
	//seq_control(best, CTL_BRIGHTNESS, 127);
    }
    while (playing) {
	lowtime = ~0;
	for (best = track = 0; track < ntrks && seq[track].data; track++)
	    if (seq[track].ticks < lowtime) {
		best = track;
		lowtime = seq[track].ticks;
	    }
	if (lowtime == ~0)
	    break;		/* no more data to read */
	track = best;

	/* this section parses data in midi file buffer */
	if ((seq[track].data[seq[track].index] & 0x80) &&
	    (seq[track].index < seq[track].length))
	    seq[track].running_st = seq[track].data[seq[track].index++];
	if (seq[track].running_st == 0xff && seq[track].index < seq[track].length)
	    seq[track].running_st = seq[track].data[seq[track].index++];
	if (seq[track].running_st > 0xf7)	/* midi real-time message (ignored) */
	    length = 0;
	else if (!(length = cmdlen[(seq[track].running_st & 0xf0) >> 4]))
	    length = rvl(&seq[track]);

	if (seq[track].index + length < seq[track].length) {
	    /* use the parsed midi data */
	    data = &(seq[track].data[seq[track].index]);
	    if (seq[track].running_st == SET_TEMPO)
		tempo = ((*(data) << 16) | (data[1] << 8) | data[2]);
	    if (seq[track].ticks > lasttime) {
		if (division > 0) {
		    dtime = ((double) ((seq[track].ticks - lasttime) * (tempo / 1000)) /
			     (double) (division)) * skew;
		    current += dtime;
		    lasttime = seq[track].ticks;
		} else if (division < 0)
		    current = ((double) seq[track].ticks /
			       ((double) ((division & 0xff00 >> 8) *
				   (division & 0xff)) * 1000.0)) * skew;
		/* stop if there's more than 40 seconds of nothing */
		if (dtime > 40096.0)
		    playing = 0;
		else if ((int) current > ticks) {
                    Uint32 timeout = start_tick + current;
                    while (0 && !SDL_TICKS_PASSED(SDL_GetTicks(), timeout)) {
                      SDL_Delay(timeout - SDL_GetTicks());
                    }
                    ticks = current;
		    if (graphics)
			if ((play_status = updatestatus()) != NO_EXIT)
			    return play_status;
		}
	    }
	    if (playing && seq[track].running_st > 0x7f && ISPLAYING(CHN)) {
		switch (seq[track].running_st & 0xf0) {
		case MIDI_KEY_PRESSURE:
		    seq_key_pressure(CHN, NOTE, VEL);
		    break;
		case MIDI_NOTEON:
		    if (VEL && usevol[CHN])
			VEL = usevol[CHN];
		    seq_start_note(CHN, NOTE, VEL);
		    break;
		case MIDI_NOTEOFF:
		    seq_stop_note(CHN, NOTE, VEL);
		    break;
		case MIDI_CTL_CHANGE:
		    seq_control(CHN, NOTE, VEL);
		    break;
		case MIDI_CHN_PRESSURE:
		    seq_chn_pressure(CHN, NOTE);
		    break;
		case MIDI_PITCH_BEND:
		    seq_bender(CHN, NOTE, VEL);
		    break;
		case MIDI_PGM_CHANGE:
		    seq_set_patch(CHN, NOTE);
		    break;
		case MIDI_SYSTEM_PREFIX:
		    if (length > 1)
			load_sysex(length, data, seq[track].running_st);
		    break;
		default:
		    break;
		}
            }
	    if (verbose || graphics) {
		showevent(seq[track].running_st, data, length);
	    }
	}
	/* this last little part queues up the next event time */
	seq[track].index += length;
	if (seq[track].index >= seq[track].length)
	    seq[track].ticks = ~0;	/* mark track complete */
	else
	    seq[track].ticks += rvl(&seq[track]);
    }
    return 1;
}
