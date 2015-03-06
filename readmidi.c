/************************************************************************
   readmidi.c -- last change: 25 Jan 2015

   Creates an array of pointers to each chunk in a midi file.
   ENTIRE MIDI FILE IS RETAINED IN MEMORY so that no additional malloc
   calls need be made to store the data of the events in the midi file.

   Copyright 2015 Nathan I. Laredo

   This program is modifiable/redistributable under the terms
   of the GNU General Public Licence.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *************************************************************************/
#include "playmidi.h"
#include "SDL2/SDL.h"

int format, ntrks, division;
unsigned char *midifilebuf;

extern struct miditrack seq[MAXTRKS];
extern int find_header;
extern unsigned long int default_tempo;

unsigned short Read16()
{
    register unsigned short x;

    x = (*(midifilebuf) << 8) | midifilebuf[1];
    midifilebuf += 2;
    return x;
}

unsigned long Read32()
{
    register unsigned long x;

    x = (*(midifilebuf) << 24) | (midifilebuf[1] << 16) |
	(midifilebuf[2] << 8) | midifilebuf[3];
    midifilebuf += 4;
    return x;
}

int readmidi(filebuf, filelength)
unsigned char *filebuf;
off_t filelength;
{
    unsigned long int i = 0, track, tracklen;

    midifilebuf = filebuf;
    /* allow user to specify header number in from large archive */
    while (i != find_header && midifilebuf < (filebuf + filelength - 32)) {
	if (strncmp((char *)midifilebuf, "MThd", 4) == 0) {
	    i++;
	    midifilebuf += 4;
	} else
	    midifilebuf++;
    }
    if (i != find_header) {	/* specified header was not found */
	midifilebuf = filebuf;
	return find_header = 0;
    }
    if (midifilebuf != filebuf)
	midifilebuf -= 4;
    i = Read32();
    if (i == RIFF) {
	midifilebuf += 16;
	i = Read32();
    }
    if (i == MThd) {
	tracklen = Read32();
	format = Read16();
	ntrks = Read16();
	division = Read16();
    } else if (i == CTMF) {
	/* load a creative labs CMF file, with instruments for fm */
	tracklen = midifilebuf[4] | (midifilebuf[5] << 8);
	format = 0;
	ntrks = 1;
	division = midifilebuf[6] | (midifilebuf[7] << 8);
	default_tempo = 1000000 * division /
		(midifilebuf[8] | (midifilebuf[9] << 8));
	seq[0].data = filebuf + tracklen;
	seq[0].length = filelength - tracklen;
	i = (unsigned long int) (*(short *) &midifilebuf[2]) - 4;
	return ntrks;
    } else {
	int found = 0;
	while (!found && midifilebuf < (filebuf + filelength - 8))
	    if (strncmp((char *)midifilebuf, "MThd", 4) == 0)
		found++;
	    else
		midifilebuf++;
	if (found) {
	    midifilebuf += 4;
	    tracklen = Read32();
	    format = Read16();
	    ntrks = Read16();
	    division = Read16();
	} else {
#ifndef DISABLE_RAW_MIDI_FILES
	    /* this allows playing ANY file, so watch out */
	    midifilebuf -= 4;
	    format = 0;		/* assume it's .mus file ? */
	    ntrks = 1;
	    division = 40;
#else
	    return -1;
#endif
	}
    }
    if (ntrks > MAXTRKS) {
	fprintf(stderr, "\nWARNING: %d TRACKS IGNORED!\n", ntrks - MAXTRKS);
	ntrks = MAXTRKS;
    }
    for (track = 0; track < ntrks; track++) {
	if (Read32() != MTrk) {
	    /* MTrk isn't where it's supposed to be, search rest of file */
	    int fuzz, found = 0;
	    midifilebuf -= 4;
	    if (strncmp((char *)midifilebuf, "MThd", 4) == 0)
		continue;
	    else {
		if (!track) {
		    seq[0].length = filebuf + filelength - midifilebuf;
		    seq[0].data = midifilebuf;
		    continue;	/* assume raw midi data file */
		}
		midifilebuf -= seq[track - 1].length;
		for (fuzz = 0; (fuzz + midifilebuf) <
		     (filebuf + filelength - 8) && !found; fuzz++)
		    if (strncmp((char *)&midifilebuf[fuzz], "MTrk", 4) == 0)
			found++;
		seq[track - 1].length = fuzz;
		midifilebuf += fuzz;
		if (!found)
		    continue;
	    }
	}
	tracklen = Read32();
	if (midifilebuf + tracklen > filebuf + filelength)
	    tracklen = filebuf + filelength - midifilebuf;
	seq[track].length = tracklen;
	seq[track].data = midifilebuf;
	midifilebuf += tracklen;
    }
    ntrks = track;
    return ntrks;
}
