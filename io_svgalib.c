/************************************************************************
   io_svgalib.c  -- shows midi events using svgalib

   Copyright (C) 1995-1996 Nathan I. Laredo

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
#include <vga.h>
#include <vgagl.h>
#include <sys/time.h>
#include <unistd.h>

/* following includes are for raw + nowait input mode */
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

char *drum3ch[11] =
{
    "STD", "RM.", "PWR", "ELE", "808",
    "JAZ", "BRU", "ORC", "SFX", "PRG", "M32"
};

#define SET(x) (x == 8 ? 1 : x >= 16 && x <= 23 ? 2 : x == 24 ? 3 : \
		x == 25  ? 4 : x == 32 ? 5 : x >= 40 && x <= 47 ? 6 : \
		x == 48 ? 7 : x == 56 ? 8 : x >= 96 && x <= 111 ? 9 : \
		x == 127 ? 10 : 0)

extern int graphics, verbose, perc, ntrks;
extern int note_vel[16][128];
extern unsigned long int ticks;
extern char *filename, *gmvoice[256];
extern float skew;
extern void seq_reset();
extern struct chanstate channel[16];
extern struct timeval start_time;
char textbuf[1024], **nn;
int i, ytxt, mytty;
void *font;
struct termios newtty, oldtty;
struct timeval now_time, want_time;

int cdeltat(t1, t2)
struct timeval *t1;
struct timeval *t2;
{
    int d1, d2;

    d1 = t1->tv_sec - t2->tv_sec;
    if ((d2 = t1->tv_usec - t2->tv_usec) < 0)
	(d2 += 1000000, d1 -= 1);
    d2 /= 10000;
    return (d2 + d1 * 100);
}

void close_show(error)
int error;
{
    vga_setmode(TEXT);
    tcsetattr(i, TCSANOW, &oldtty);
    exit(error);
}

#define CHN		(cmd & 0xf)
#define NOTE		((int)data[0])
#define VEL		((int)data[1])

int updatestatus()
{
    char ch;
    int d1, d2;

    want_time.tv_sec = start_time.tv_sec + (ticks / 100);
    want_time.tv_usec = start_time.tv_usec + (ticks % 100) * 10000;
    if (want_time.tv_usec > 1000000)
	(want_time.tv_usec -= 1000000, want_time.tv_sec++);

    do {
	if (read(mytty, &ch, 1))
	    switch (ch) {
	    case '.':
	    case '>':
		if ((skew -= 0.01) < 0.25)
		    skew = 0.25;
		sprintf(textbuf, "skew=%0.2f", skew);
		gl_write(512, 72, textbuf);
		break;
	    case ',':
	    case '<':
		if ((skew += 0.01) > 4)
		    skew = 4.0;
		sprintf(textbuf, "skew=%0.2f", skew);
		gl_write(512, 72, textbuf);
		break;
	    case 'p':
	    case 'P':
		seq_reset();
		return -1;
		break;
	    case 'r':
	    case '^':
		seq_reset();
		return 0;
		break;
	    case 3:
	    case 27:
	    case 'q':
	    case 'Q':
		close_show(0);
		break;
	    case 'n':
	    case 'N':
		seq_reset();
		return 1;
	    default:
		break;
	    }
	gettimeofday(&now_time, NULL);
	d1 = now_time.tv_sec - start_time.tv_sec;
	d2 = now_time.tv_usec - start_time.tv_usec;
	if (d2 < 0)
	    (d2 += 1000000, d1 -= 1);
	sprintf(textbuf, "%02d:%02d.%d", d1 / 60, d1 % 60, d2 / 100000);
	gl_write(528, 0, textbuf);
	d1 = cdeltat(&want_time, &now_time);
	if (d1 > 15)
	    usleep(100000);
    } while (d1 > 10);
    return NO_EXIT;
}

void draw_note(chn, note, vel)
int chn, note, vel;
{
    register int x, y, c, dy;

    x = 32 * chn;
    y = 400 - note * 3;
    c = vel / 4 + 32 * (chn % 8);

    dy = (channel[chn].bender - 8192);
    dy *= channel[chn].bender_range;
    dy /= 2048;

    gl_line(x, y, x + 12, y - dy, c);
    gl_line(x + 13, y - dy, x + 23, y, c);
}

void showevent(cmd, data, length)
int cmd;
unsigned char *data;
int length;
{
    if (cmd < 8 && cmd > 0) {

	int COLS = WIDTH / 8;

	if (ytxt == 10)		/* clear text area */
	    gl_fillbox(512, 80, WIDTH - 1, HEIGHT - 1, 0);
	strncpy(textbuf, data, length < COLS - 66 ? length : COLS - 66);
	textbuf[length < COLS - 66 ? length : COLS - 66] = 0;
	gl_colorfont(8, 8, (cmd * 32) - 1, font);
	gl_write(512, ytxt * 8, textbuf);
	gl_colorfont(8, 8, 255, font);	/* hope this isn't slow... */
	if ((++ytxt) > (HEIGHT / 8) - 1)
	    ytxt = 10;
    } else if (cmd & 0x80)
	switch (cmd & 0xf0) {
	case MIDI_KEY_PRESSURE:
	    draw_note(CHN, NOTE, VEL);
	    break;
	case MIDI_NOTEON:
	    draw_note(CHN, NOTE, VEL);
	    break;
	case MIDI_NOTEOFF:
	    draw_note(CHN, NOTE, 0);
	    break;
	case MIDI_CTL_CHANGE:
	    /* future expansion */
	    break;
	case MIDI_CHN_PRESSURE:
	    /* future expansion */
	    break;
	case MIDI_PITCH_BEND:
	    /* erase all notes in channel to re-draw with new bend */
	    gl_fillbox(32 * CHN, 8, (32 * CHN) + 24, 400, 0);
	    for (i = 0; i < 128; i++)
		if (note_vel[CHN][i])
		    draw_note(CHN, i, note_vel[CHN][i]);
	    break;
	case MIDI_PGM_CHANGE:
	    if (!ISPERC(CHN))
		strncpy(textbuf, gmvoice[NOTE], 3);
	    else
		strncpy(textbuf, drum3ch[SET(NOTE)], 3);
	    textbuf[3] = 0;
	    gl_write(CHN * 32, 0, textbuf);
	    break;
	case 0xf0:
	case 0xf7:
	    gl_fillbox(0, HEIGHT - 24, 511, HEIGHT - 1, 0);
	    sprintf(textbuf, "Sysex(%2x)", cmd);
	    gl_write(0, HEIGHT - 24, textbuf);
	    for (i = 0; i < length && i < 26; i++) {
		sprintf(textbuf, "%02x", data[i]);
		gl_write(80 + i * 16, HEIGHT - 24, textbuf);
	    }
	    for (; i < length && i < 57; i++) {		/* wrap to next line */
		sprintf(textbuf, "%02x", data[i]);
		gl_write((i - 26) * 16, HEIGHT - 16, textbuf);
	    }
	    for (; i < length && i < 88; i++) {		/* wrap to next line */
		sprintf(textbuf, "%02x", data[i]);
		gl_write((i - 57) * 16, HEIGHT - 8, textbuf);
	    }
	    break;
	default:
	    break;
	}
}

void init_show()
{
    char *tmp;

    ytxt = 10;
    gl_clearscreen(0);
    gl_colorfont(8, 8, 244, font);	/* hope this isn't slow... */
    gl_write(0, 0, "ch1 ch2 ch3 ch4 ch5 ch6 ch7 ch8 ch9 "
	     "c10 c11 c12 c13 c14 c15 c16");
    gl_write(512, 16, RELEASE);
    gl_write(560, 24, "by");
    gl_write(512, 32, "Nathan Laredo");
    tmp = strrchr(filename, '/');
    strncpy(textbuf, (tmp == NULL ? filename : tmp + 1), 14);
    gl_write(512, 48, textbuf);
    sprintf(textbuf, "%d track%c", ntrks, ntrks > 1 ? 's' : ' ');
    gl_write(512, 56, textbuf);
}

void setup_show(argc, argv)
int argc;
char **argv;
{
    int vgamode, i, j;

    graphics++;			/* force -r option if not selected */
    mytty = open("/dev/tty", O_RDONLY | O_NDELAY, 0);
    tcgetattr(mytty, &oldtty);
    tcgetattr(mytty, &newtty);
    newtty.c_lflag &= ~(ICANON | ECHO | ICRNL | ISIG);
    tcsetattr(mytty, TCSANOW, &newtty);
    vga_init();
    if ((vgamode = vga_getdefaultmode()) == -1)
	vgamode = G640x480x256;
    if (!vga_hasmode(vgamode)) {
	fprintf(stderr, "\nRequested vga mode not available!\n");
	close_show(-1);
    }
    gl_setwritemode(WRITEMODE_OVERWRITE);
    vga_setmode(vgamode);
    gl_setcontextvga(vgamode);
    for (i = 0; i < 32; i++) {
	j = i * 2;
	gl_setpalettecolor(i, j, 0, 0);
	gl_setpalettecolor(i + 32, 0, j, 0);
	gl_setpalettecolor(i + 64, j, j, 0);
	gl_setpalettecolor(i + 96, 0, 0, j);
	gl_setpalettecolor(i + 128, j, 0, j);
	gl_setpalettecolor(i + 160, 0, 0, j);
	gl_setpalettecolor(i + 192, 0, j, j);
	gl_setpalettecolor(i + 224, j, j, j);
    }
    font = malloc(256 * 8 * 8 * BYTESPERPIXEL);
    gl_expandfont(8, 8, 255, gl_font8x8, font);
    gl_setfont(8, 8, font);
    gl_enableclipping();
}
