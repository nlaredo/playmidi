/************************************************************************
   io_ncurses.c  -- shows midi events using ncurses or printf

   Copyright (C) 1994-1996 Nathan I. Laredo

   This program is modifiable/redistributable under the terms
   of the GNU General Public Licence.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
   Send your comments and all your spare pocket change to
   laredo@gnu.ai.mit.edu (Nathan Laredo) or to PSC 1, BOX 709, 2401
   Kelly Drive, Lackland AFB, TX 78236-5128, USA.
 *************************************************************************/
#include "playmidi.h"
#include <ncurses.h>
#include "gsvoices.h"
#include <sys/time.h>
#include <unistd.h>

struct meta_event_names {
  int t;
  char *name;
  /* todo, add function pointer for handling */
};

struct meta_event_names metatype[] = {
  { SEQUENCE_NUMBER, "Sequence Number" },
  { TEXT_EVENT, "Text" },
  { COPYRIGHT_NOTICE, "Copyright Notice" },
  { SEQUENCE_NAME, "Sequence/Track Name" },
  { INSTRUMENT_NAME, "Instrument Name" },
  { LYRIC, "Lyric" },
  { MARKER, "Marker" },
  { CUE_POINT, "Cue Point" },
  { PROGRAM_NAME, "Program Name" },
  { DEVICE_NAME, "Device Name" },
  { CHANNEL_PREFIX, "Channel Prefix" },
  { END_OF_TRACK, "End of Track" },
  { SET_TEMPO, "Tempo" },
  { SMPTE_OFFSET, "SMPTE Offset" },
  { TIME_SIGNATURE, "Time Signature" },
  { KEY_SIGNATURE, "Key Signature" },
  { SEQUENCER_SPECIFIC, "Sequencer Specific" },
  { META_EVENT, NULL } /* end of list */
};

char *sharps[12] =		/* for a sharp key */
{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
char *flats[12] =		/* for a flat key */
{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
char *majflat[15] =		/* name of major key with 'x' flats */
{"C", "F", "Bb", "Eb", "Ab", "Db", "Gb", "Cb", "Fb", "Bbb", "Ebb",
 "Abb", "Gbb", "Cbb", "Fbb"};	/* only first 8 defined by file format */
char *majsharp[15] =		/* name of major key with 'x' sharps */
{"C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#",
 "E#", "B#", "F##", "C##"};	/* only first 8 defined by file format */
char *minflat[15] =		/* name of minor key with 'x' flats */
{"A", "D", "G", "C", "F", "Bb", "Eb", "Ab", "Db", "Gb", "Cb",
 "Fb", "Bbb", "Ebb", "Abb"};	/* only first 8 defined by file format */
char *minsharp[15] =		/* name of minor key with 'x' sharps */
{"A", "E", "B", "F#", "C#", "G#", "D#", "A#", "E#", "B#", "F##",
 "C##", "G##", "D##", "A##"};	/* only first 8 defined by file format */

extern int graphics, verbose, perc;
extern int format, ntrks, division;
extern Uint32 ticks;
extern char *filename;
extern float skew;
extern void seq_reset(int);
extern struct timeval start_time;

struct timeval now_time, want_time;
char textbuf[1024], **nn;
int i, ytxt, karaoke;

void close_show(error)
int error;
{
    if (graphics) {
	attrset(A_NORMAL);
	refresh();
	endwin();
    }
    exit(error);
}

#define NOTE		((int)data[0])
#define VEL		((int)data[1])
#define OCTAVE		(NOTE / 12)
#define OCHAR		(0x30 + OCTAVE)
#define XPOS		(14 + ((NOTE/2) % ((COLS - 16) / 4)) * 4)
#define PXPOS		(14 + ((NOTE) % ((COLS - 16) / 9)) * 9)
#define YPOS		(CHN(cmd) + 2)
#define NNAME		nn[NOTE % 12]

extern struct chanstate channel[16];  // presently active channel state

static char *gsfind(int pgm, int ch, int key)
{
  int i, bank;
  char *rv = NULL;
  struct gsvoices *gs = key ? gs_drum : ISPERC(ch) ? gs_perc : gs_inst;
  bank = channel[ch].controller[CTL_BANK_SELECT];
  bank <<= 7;
  bank |= channel[ch].controller[CTL_BANK_SELECT + CTL_LSB];
  if (key != 0) {
    pgm = channel[ch].program;
  }
  for (i = 0; gs[i].name != NULL; i++) {
    if (gs[i].pgm <= pgm) {
      rv = gs[i].name;
      if (gs[i].pgm == pgm && gs[i].bank == bank && gs[i].key == key) {
        return gs[i].name;
      }
    }
  }
  return rv;
}

int cdeltat(t1, t2)
struct timeval *t1;
struct timeval *t2;
{
    int d1, d2;

    d1 = t1->tv_sec - t2->tv_sec;
    if((d2 = t1->tv_usec - t2->tv_usec) < 0)
	(d2 += 1000000, d1 -= 1);
    d2 /= 10000;
    return (d2 + d1 * 100);
}
int updatestatus()
{
    int ch, d1, d2;

    want_time.tv_sec = start_time.tv_sec + (ticks / 1000);
    want_time.tv_usec = start_time.tv_usec + (ticks % 1000) * 1000;
    if (want_time.tv_usec > 1000000)
	(want_time.tv_usec -= 1000000, want_time.tv_sec++);

    do {
	attrset(A_BOLD);
	if ((ch = getch()) != ERR)
	    switch (ch) {
	    case KEY_RIGHT:
		if ((skew -= 0.01) < 0.25)
		    skew = 0.25;
		if (graphics)
		    mvprintw(1, COLS - 6, "%0.2f", skew);
		break;
	    case KEY_LEFT:
		if ((skew += 0.01) > 4)
		    skew = 4.0;
		if (graphics)
		    mvprintw(1, COLS - 6, "%0.2f", skew);
		break;
	    case KEY_PPAGE:
	    case KEY_UP:
		seq_reset(1);
		return (ch == KEY_UP ? 0 : -1);
		break;
	    case 18:
	    case 12:
            case KEY_RESIZE:
		wrefresh(curscr);
		break;
	    case 'q':
	    case 'Q':
	    case 3:
		close_show(0);
		break;
	    default:
		return 1;	/* skip to next song */
		break;
	    }
	gettimeofday(&now_time, NULL);
	d1 = now_time.tv_sec - start_time.tv_sec;
	d2 = now_time.tv_usec - start_time.tv_usec;
	if (d2 < 0)
	    (d2 += 1000000, d1 -= 1);
	mvprintw(1, 0, "%02d:%02d.%d", d1 / 60, d1 % 60, d2 / 100000);
	refresh();
	d1 = cdeltat(&want_time, &now_time);
	if (0 && d1 > 10)
	    usleep(100000);
    } while (1 && d1 > 30);
    return NO_EXIT;
}

void showevent(cmd, data, length)
int cmd;
unsigned char *data;
int length;
{
    if (cmd < CHANNEL_PREFIX) {
        int t;
        for (t = 0; metatype[t].name; t++) {
          if (metatype[t].t == cmd) {
            break;
          }
        }
	if (graphics && length > COLS)
	    length = COLS;
	if (cmd != 1 && strncmp(textbuf, (char *)data, length - 1) == 0)
	     return;	/* ignore repeat messages, "WinJammer Demo" etc. */
	if (verbose) {
	    printf("%s: %.*s\n", metatype[t].name, length, (char *)data);
	} else {
	    attrset(A_BOLD | COLOR_PAIR(cmd));
	    if (!karaoke || *data == '\\' || *data == '/' || 
		(*data >= '@' && *data <= 'Z') || *data == '(' ||
		karaoke + length > COLS || (cmd != 1 && karaoke)) {
		karaoke = 0;
		if ((++ytxt) > LINES - 1) {
                  move(19, 0);
                  deleteln();
		  ytxt = LINES - 1;
                }
		move(ytxt, 0);
		clrtoeol();
		if (*data == '\\' || *data == '/')
		    (data++, length--);	/* karaoke newlines */
		if (*data == '@')	/* karaoke info */
		    (data += 2, length -= 2);
	    }
	    strncpy(textbuf, (char *)data, length < COLS - karaoke ? length :
		    COLS - karaoke);
	    if (length < 1024)
		textbuf[length] = 0;
	    mvaddstr(ytxt, karaoke, textbuf);
	    if (cmd == 1) {
		karaoke += strlen(textbuf);
		if (karaoke > COLS - 10)
		    karaoke = 0;
	    } else
		karaoke = 0;
	}
    } else if (cmd == KEY_SIGNATURE) {
      Sint8 sf = data[1];
      Sint8 mi = data[2];
	if (graphics || verbose)
	    nn = ((sf & 0x80) ? flats : sharps);
	if (verbose) {
	    if (mi == 0)	/* major key */
		printf("Key: %s major\n", (!(sf & 0x80) ?
			majsharp[sf & 0xf] : majflat[(-sf) & 0xf]));
	    else	/* minor key */
		printf("Key: %s minor\n", (!(sf & 0x80) ?
			minsharp[sf & 0xf] : minflat[(-sf) & 0xf]));
	}
        if (graphics) {
	    attrset(A_NORMAL);
	    if (VEL)	/* major key */
		mvprintw(18, 36, "%3s major", (!(NOTE & 0x80) ?
			majsharp[NOTE] : majflat[256-NOTE]));
	    else	/* minor key */
		mvprintw(18, 36, "%3s minor", (!(NOTE & 0x80) ?
			minsharp[NOTE] : minflat[256-NOTE]));
        }
    } else if (cmd == TIME_SIGNATURE) {
        attrset(A_NORMAL);
	mvprintw(18, 16, "%3d/%-3d", data[0], 1<<(data[1]));
    } else if (cmd == SET_TEMPO) {
	int t = ((*(data) << 16) | (data[1] << 8) | data[2]);
        attrset(A_NORMAL);
	mvprintw(18, 24, "%3d BPM", 60000000/t);
    } else
	switch (cmd & 0xf0) {
	case MIDI_KEY_PRESSURE:
	    if (verbose > 4)
		printf("Chn %d Key Pressure %s%c=%d\n",
		       1 + (cmd & 0xf), NNAME, OCHAR, VEL);
	    break;
	case MIDI_NOTEON:
	    if (graphics)
		if (VEL) {
		    attrset(A_BOLD | COLOR_PAIR((CHN(cmd) % 6 + 1)));
		    if (!ISPERC(CHN(cmd)) || NOTE == 0)
			mvprintw(YPOS, XPOS, "%s%c", NNAME, OCHAR);
		    else
			mvprintw(YPOS, PXPOS, "%8.8s",
                                 gsfind(0, CHN(cmd), NOTE));
		} else {
                  if (!ISPERC(CHN(cmd))) {
		    mvaddstr(YPOS, XPOS, "   ");
                  } else {
		    mvaddstr(YPOS, PXPOS, "        ");
                  }
                }
	    else if (verbose > 5)
		printf("Chn %d Note On %s%c=%d\n",
		       1 + (cmd & 0xf), NNAME, OCHAR, VEL);
	    break;
	case MIDI_NOTEOFF:
	    if (graphics) {
                  if (!ISPERC(CHN(cmd))) {
		    mvaddstr(YPOS, XPOS, "   ");
                  } else {
		    mvaddstr(YPOS, PXPOS, "        ");
                  }
            }
	    else if (verbose > 5)
		printf("Chn %d Note Off %s%c=%d\n",
		       1 + (cmd & 0xf), NNAME, OCHAR, VEL);
	    break;
	case MIDI_CTL_CHANGE:
            if (0 && graphics) {  /* debug midi controller messages */
		mvprintw(YPOS, PXPOS, "[%02x]=%02x", NOTE, VEL);
            }
	    if (verbose > 5)
		printf("Chn %d Ctl Change %d=%d\n",
		       1 + (cmd & 0xf), NOTE, VEL);
	    break;
	case MIDI_CHN_PRESSURE:
	    if (verbose > 5)
		printf("Chn %d Pressure=%d\n",
		       1 + (cmd & 0xf), NOTE);
	    break;
	case MIDI_PITCH_BEND:
	    {
		int val = (VEL << 7) | NOTE;

		if (graphics) {
		    attrset(A_BOLD);
		    if (val > 0x2000)
			mvaddch(YPOS, 12, '>');
		    else if (val < 0x2000)
			mvaddch(YPOS, 12, '<');
		    else
			mvaddch(YPOS, 12, ' ');
		} else if (verbose > 4)
		    printf("Chn %d Bender=0x%04x\n",
			   1 + CHN(cmd), val);
	    }
	    break;
	case MIDI_PGM_CHANGE:
	    if (graphics) {
		attrset(COLOR_PAIR((CHN(cmd) % 6 + 1)) | A_BOLD);
                mvprintw(YPOS, 0, "%12.12s", gsfind(NOTE, CHN(cmd), 0));
	    } else if (verbose > 3)
		printf("Chn %d Program=%s %d\n", 1 + CHN(cmd),
                       gsfind(NOTE, CHN(cmd), 0), NOTE + 1);
	    break;
	case 0xf0:
	case 0xf7:
	    if (verbose > 2) {
		printf("Sysex(%2x): ", cmd);
		for (i = 0; i < length; i++)
		    printf("%02x", data[i]);
		putchar('\n');
	    }
	    break;
	default:
	    break;
	}
}

void init_show()
{
    char *tmp;

    nn = flats;
    ytxt = 18;
    karaoke = 0;
    if (graphics) {
	clear();
	attrset(A_NORMAL);
	mvprintw(0, 0, RELEASE " by Nathan Laredo");
	mvprintw(0, 40, "Now Playing:");
	mvprintw(1, 40, "[P]ause [N]ext [L]ast [O]ptions");
	mvaddstr(ytxt, 0, "=-=-=-=-=-=-=-");
	mvprintw(1, 0, "00:00.0 - 00:00.0, %d track%c", ntrks,
		 ntrks > 1 ? 's' : ' ');
	for (i = 0; i < 16; i++)
	    mvprintw(i + 2, 0, "Channel %2d   |", i + 1);
	tmp = strrchr(filename, '/');
	strncpy(textbuf, (tmp == NULL ? filename : tmp + 1), COLS - 53);
	attrset(A_BOLD);
	mvaddstr(0, 53, textbuf);
	mvaddch(1, 41, 'P');
	mvaddch(1, 49, 'N');
	mvaddch(1, 56, 'L');
	mvaddch(1, 63, 'O');
	mvaddstr(0, 0, RELEASE);
	refresh();
    } else if (verbose) {
	printf("** Now Playing \"%s\"\n", filename);
	printf("** Format: %d, Tracks: %d, Division: %d\n", format,
	       ntrks, division);
    }
}

void setup_show(argc, argv)
int argc;
char **argv;
{
    if (graphics) {
	initscr();
	start_color();
	verbose = 0;
	init_pair(1, COLOR_RED, COLOR_BLACK);
	init_pair(2, COLOR_GREEN, COLOR_BLACK);
	init_pair(3, COLOR_YELLOW, COLOR_BLACK);
	init_pair(4, COLOR_BLUE, COLOR_BLACK);
	init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(6, COLOR_CYAN, COLOR_BLACK);
	init_pair(7, COLOR_WHITE, COLOR_BLACK);
	raw();
	noecho();
	nodelay(stdscr, TRUE);
	keypad(stdscr, TRUE);
    }
}
