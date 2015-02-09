/************************************************************************
   playmidi.c -- last change: 1 Jan 96

   Plays a MIDI file to any supported synth (including midi) device

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
#ifndef __FreeBSD__
#include <getopt.h>
#endif
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include "playmidi.h"

SEQ_DEFINEBUF(SEQUENCERBLOCKSIZE);

#ifdef PLAY_FM
int play_fm = 0xffff, play_gus = 0, play_ext = 0, play_awe = 0;
#endif
#ifdef PLAY_GUS
int play_fm = 0, play_gus = 0xffff, play_ext = 0, play_awe = 0;
#endif
#ifdef PLAY_MIDI
int play_fm = 0, play_gus = 0, play_ext = 0xffff, play_awe = 0;
#endif
#ifdef PLAY_AWE32
int play_fm = 0, play_gus = 0, play_ext = 0, play_awe = 0xffff;
#endif

struct miditrack seq[MAXTRKS];

int verbose = 0, chanmask = 0xffff, perc = PERCUSSION;
int dochan = 1, force8bit = 0, wantopl3 = FM_DEFAULT_MODE;
int patchloaded[256], fmloaded[256], useprog[16], usevol[16];
int graphics = 0, reverb = 0, chorus = 0, nrsynths, nrmidis;
int sb_dev = -1, gus_dev = -1, ext_dev = -1, awe_dev = 2, p_remap = 0;
int seqfd, find_header = 0, MT32 = 0;
FILE *mfd;
int FORCE_EXT_DEV = DEFAULT_MIDI_DEV;
unsigned long int default_tempo;
char *filename;
float skew = 1.0;
extern int ntrks;
extern char *gmvoice[256];
extern int mt32pgm[128];
extern int playevents();
extern int gus_load(int);
extern int readmidi(unsigned char *, off_t);
extern void loadfm();
extern void setup_show(int, char **);
extern void close_show(int);

struct synth_info card_info[MAX_CARDS];
void synth_setup()
{
#ifdef ULTRA_DRIVER
    gus_dev = 0;
    sb_dev = -1;
    ext_dev = -1;
    awe_dev = -1;
    play_ext = 0;
    play_fm = 0;
    play_gus = 0xffff;
    play_awe = 0;
#else
    int i;

    if (ioctl(seqfd, SNDCTL_SEQ_NRSYNTHS, &nrsynths) == -1) {
	fprintf(stderr, "there is no soundcard\n");
	exit(-1);
    }
    for (i = 0; i < nrsynths; i++) {
	card_info[i].device = i;
	if (ioctl(seqfd, SNDCTL_SYNTH_INFO, &card_info[i]) == -1) {
	    fprintf(stderr, "cannot get info on soundcard\n");
	    perror(SEQUENCER_DEV);
	    exit(-1);
	}
	if (card_info[i].synth_type == SYNTH_TYPE_SAMPLE
	    && card_info[i].synth_subtype == SAMPLE_TYPE_GUS)
	    gus_dev = i;
	else if (card_info[i].synth_type == SYNTH_TYPE_SAMPLE
	    && card_info[i].synth_subtype == SAMPLE_TYPE_AWE32)
	    awe_dev = i;
	else if (card_info[i].synth_type == SYNTH_TYPE_FM) {
	    sb_dev = i;
	    if (play_fm)
		loadfm();
	    if (wantopl3)
		card_info[i].nr_voices = 12;	/* we have 12 with 4-op */
	}
    }

    if (gus_dev >= 0) {
	if (ioctl(seqfd, SNDCTL_SEQ_RESETSAMPLES, &gus_dev) == -1) {
	    perror("Sample reset");
	    exit(-1);
	}
    }
    if (ioctl(seqfd, SNDCTL_SEQ_NRMIDIS, &nrmidis) == -1) {
	fprintf(stderr, "can't get info about midi ports\n");
	exit(-1);
    }
    if (nrmidis > 0) {
	if (FORCE_EXT_DEV >= 0)
	    ext_dev = FORCE_EXT_DEV;
	else
	    ext_dev = nrmidis - 1;
    }
    if (!play_gus)
	gus_dev = -1;
    if (!play_fm)
	sb_dev = -1;
    if (!play_ext)
	ext_dev = -1;
    if (!play_awe)
	awe_dev = -1;
    if (ext_dev < 0)
	play_ext = 0;
    if (sb_dev < 0)
	play_fm = 0;
    if (gus_dev < 0)
	play_gus = 0;
    if (awe_dev < 0)
	play_awe = 0;
#endif
}

void seqbuf_dump()
{
    if (_seqbufptr)
	if (write(seqfd, _seqbuf, _seqbufptr) == -1) {
	    perror("write " SEQUENCER_DEV);
	    exit(-1);
	}
    _seqbufptr = 0;
}

int hextoi(s)
char *s;
{
    int i, j, k, l;

    j = 0;
    k = strlen(s);
    for (i = 0; i < k; i++) {
	l = toupper(s[i]);
	if (l > 64 && l < 71)
	    j += (l - 55) << ((k - i - 1) * 4);
	else if (l > 47 && l < 58)
	    j += (l - 48) << ((k - i - 1) * 4);
	else if (l != 88) {
	    fprintf(stderr, "invalid character in hexidecimal mask\n");
	    exit(1);
	}
    }
    if (j < 0 || j > 0xffff) {
	fprintf(stderr, "mask must be between 0 and ffff hexidecimal\n");
	exit(1);
    }
    return j;

}

int main(argc, argv)
int argc;
char **argv;
{
    extern char *optarg;
    extern int optind;
    int i, error = 0, j, newprog;
    char *extra;
    char *filebuf;
    struct stat info;
    int piped = 0;

    printf("%s Copyright (C) 1994-1997 Nathan I. Laredo,"
	   " AWE32 by Takashi Iwai\n"
	   "This is free software with ABSOLUTELY NO WARRANTY.\n"
	   "For details please see the file COPYING.\n", RELEASE);
    for (i = 0; i < 16; i++)
	useprog[i] = usevol[i] = 0;	/* reset options */
    while ((i = getopt(argc, argv,
		     "48c:aA:C:dD:eE:fF:gh:G:i:IMp:P:rR:t:vV:x:z")) != -1)
	switch (i) {
	case '8':
	    force8bit++;
	    break;
	case 'x':
	    j = atoi(optarg);
	    if (j < 1 || j > 16) {
		fprintf(stderr, "option -x channel must be 1 - 16\n");
		exit(1);
	    }
	    j = 1 << (j - 1);
	    chanmask &= ~j;
	    break;
	case 'c':
	    if (chanmask == 0xffff)
		chanmask = hextoi(optarg);
	    else
		chanmask |= hextoi(optarg);
	    break;
	case 'D':
	    FORCE_EXT_DEV = atoi(optarg);
	    break;
	case 'e':
	    play_ext = 0xffff;
	    play_gus = play_fm = play_awe = 0;
	    break;
	case 'g':
	    play_gus = 0xffff;
	    play_ext = play_fm = play_awe = 0;
	    break;
	case 'a':
	    play_awe = 0xffff;
	    play_ext = play_fm = play_gus = 0;
	    break;
	case 'h':
	    find_header = atoi(optarg);
	    if (find_header < 1) {
		fprintf(stderr, "option -h header must be > 0\n");
		exit(1);
	    }
	    break;
	case 'f':
	case '4':
	    play_fm = 0xffff;
	    play_ext = play_gus = play_awe = 0;
	    wantopl3 = (i == '4');
	    break;
	case 'I':
	    {
		int k;
		printf("Gravis Ultrasound Program Info:");
		for (j = 0; j < 128; j++) {
		    extra = gmvoice[j];
		    printf("%c%3d %s", j % 6 ? ' ' : '\n', j + 1, extra);
		    for (k = strlen(extra); k < 8; k++)
			putchar(' ');
		}
		putchar('\n');
		exit(1);
	    }
	    break;
	case 'i':
	    chanmask &= ~hextoi(optarg);
	    break;
	case 'M':
	    MT32++;
	    break;
	case 'p':
	    if (strchr(optarg, ',') == NULL) {	/* set all channels */
		newprog = atoi(optarg);
		if (newprog < 1 || newprog > 129) {
		    fprintf(stderr, "option -p prog must be 1 - 129\n");
		    exit(1);
		}
		for (j = 0; j < 16; j++)
		    useprog[j] = newprog;
	    } else {		/* set channels individually */
		extra = optarg;
		while (extra != NULL) {
		    j = atoi(extra);
		    if (j < 1 || j > 16) {
			fprintf(stderr, "opton -p chan must be 1 - 16\n");
			exit(1);
		    }
		    extra = strchr(extra, ',');
		    if (extra == NULL) {
			fprintf(stderr, "option -p prog needed for chan %d\n",
				j);
			exit(1);
		    } else
			extra++;
		    newprog = atoi(extra);
		    if (newprog < 1 || newprog > 129) {
			fprintf(stderr, "option -p prog must be 1 - 129\n");
			fprintf(stderr, "DANGER: 129 may screw everything!\n");
			exit(1);
		    }
		    useprog[j - 1] = newprog;
		    extra = strchr(extra, ',');
		    if (extra != NULL)
			extra++;
		}
	    }
	    break;
	case 'r':
	    graphics++;
	    break;
	case 't':
	    if ((skew = atof(optarg)) < .25) {
		fprintf(stderr, "option -t skew under 0.25 unplayable\n");
		exit(1);
	    }
	    break;
	case 'E':
	    play_ext = hextoi(optarg);
	    play_fm &= ~play_ext;
	    play_gus &= ~play_ext;
	    play_awe &= ~play_ext;
	    break;
	case 'F':
	    play_fm = hextoi(optarg);
	    play_ext &= ~play_fm;
	    play_gus &= ~play_fm;
	    play_awe &= ~play_fm;
	    break;
	case 'G':
	    play_gus = hextoi(optarg);
	    play_fm &= ~play_gus;
	    play_ext &= ~play_gus;
	    play_awe &= ~play_gus;
	    break;
	case 'A':
	    play_awe = hextoi(optarg);
	    play_ext &= ~play_awe;
	    play_fm &= ~play_awe;
	    play_gus &= ~play_awe;
	    break;
	case 'R':
	    reverb = atoi(optarg);
	    if (reverb < 0 || reverb > 127) {
		fprintf(stderr, "option -R reverb must be 0 - 127\n");
		exit(1);
	    }
	    break;
	case 'C':
	    chorus = atoi(optarg);
	    if (chorus < 0 || chorus > 127) {
		fprintf(stderr, "option -C chorus must be 0 - 127\n");
		exit(1);
	    }
	    break;
	case 'P':
	    p_remap = atoi(optarg);
	    if (p_remap < 1 || p_remap > 16) {
		fprintf(stderr, "option -P channel must be 1 - 16\n");
		exit(1);
	    }
	    break;
	case 'v':
	    verbose++;
	    break;
	case 'V':
	    extra = optarg;
	    while (extra != NULL) {
		j = atoi(extra);
		if (j < 1 || j > 16) {
		    fprintf(stderr, "opton -V chan must be 1 - 16\n");
		    exit(1);
		}
		extra = strchr(extra, ',');
		if (extra == NULL) {
		    fprintf(stderr, "option -V volume needed for chan %d\n",
			    j);
		    exit(1);
		}
		extra++;
		newprog = atoi(extra);
		if (newprog < 1 || newprog > 127) {
		    fprintf(stderr, "option -V volume must be 1 - 127\n");
		    exit(1);
		}
		usevol[j - 1] = newprog;
		extra = strchr(extra, ',');
		if (extra != NULL)
		    extra++;
	    }
	    break;
	case 'z':
	    dochan = 0;
	    break;
	case 'd':
	    chanmask &= ~perc;
	    break;
	default:
	    error++;
	    break;
	}

    if (error || optind >= argc) {
	fprintf(stderr, "usage: %s [-options] file1 [file2 ...]\n", argv[0]);
	fprintf(stderr, "  -v       verbosity (additive)\n"
		"  -i x     ignore channels set in bitmask x (hex)\n"
		"  -c x     play only channels set in bitmask x (hex)\n"
		"  -x x     exclude channel x from playable bitmask\n"
		"  -p [c,]x play program x on channel c (all if no c)\n"
		"  -V [c,]x play channel c with volume x (all if no c)\n"
		"  -t x     skew tempo by x (float)\n"
		"  -d       don't play any percussion\n"
		"  -P x     play percussion on channel x\n"
		"  -e       output to external midi\n"
		"  -D x     output to midi device x\n"
		"  -f       output to fm (sb patches)\n"
		"  -4       output to 4-op fm (opl/3 patches)\n"
		"  -a       output to awe32 wave synth\n"
		"  -h x     skip to header x in large archive\n"
		"  -g       output to gravis ultrasound\n"
		"  -E x     play channels in bitmask x external\n"
		"  -F x     play channels in bitmask x on fm\n"
		"  -G x     play channels in bitmask x on gus\n"
		"  -A x     play channels in bitmask x on awe32\n"
		"  -z       ignore channel of all events\n"
		"  -8       force 8-bit samples on GUS\n"
		"  -M       enable MT-32 to GM translation mode\n"
		"  -I       show list of all GM programs (see -p)\n"
		"  -R x     set initial reverb to x (0-127)\n"
		"  -C x     set initial chorus to x (0-127)\n"
		"  -r       real-time playback graphics\n"
		"  -k       kill anything using /dev/sequencer\n"
		"  -w       wait for anything using /dev/sequencer\n");
	exit(1);
    }
    if ((seqfd = open(SEQUENCER_DEV, O_WRONLY, 0)) < 0) {
	perror("open " SEQUENCER_DEV);
	exit(-1);
    }
    synth_setup();
    if (!(play_gus || play_fm || play_ext || play_awe)) {
	fprintf(stderr, "%s: No playback device found.\n", argv[0]);
	exit(-1);
    }
    setup_show(argc, argv);
    /* play all filenames listed on command line */
    for (i = optind; i < argc;) {
	filename = argv[i];
	if (stat(filename, &info) == -1) {
	    if ((extra = malloc(strlen(filename) + 4)) == NULL)
		close_show(-1);
	    sprintf(extra, "%s.mid", filename);
	    if (stat(extra, &info) == -1)
		close_show(-1);
	    if ((mfd = fopen(extra, "r")) == NULL)
		close_show(-1);
	    free(extra);
	} else {
	    char *ext = strrchr(filename, '.');
	    if (ext && strcmp(ext, ".gz") == 0) {
		char temp[1024];
		piped = 1;
		sprintf(temp, "gzip -l %s", filename);
		if ((mfd = popen(temp, "r")) == NULL)
		    close_show(-1);
		fgets(temp, sizeof(temp), mfd); /* skip 1st line */
		fgets(temp, sizeof(temp), mfd);
		strtok(temp, " "); /* compressed size */
		info.st_size = atoi(strtok(NULL, " ")); /* original size */
		pclose(mfd);
		sprintf(temp, "gzip -d -c %s", filename);
		if ((mfd = popen(temp, "r")) == NULL)
		    close_show(-1);
	    } else if ((mfd = fopen(filename, "r")) == NULL)
		close_show(-1);
	}
	if ((filebuf = malloc(info.st_size)) == NULL)
	    close_show(-1);
	fread(filebuf, 1, info.st_size, mfd);
	if (piped)
	    pclose(mfd);
	else
	    fclose(mfd);
	do {
	    if (play_gus)
		gus_load(-1);
	    default_tempo = 500000;
	    /* error holds number of tracks read */
	    error = readmidi(filebuf, info.st_size);
	    if (play_gus && error > 0) {
		int i;		/* need to keep other i safe */
#define CMD (seq[i].data[j] & 0xf0)
#define CHN (seq[i].data[j] & 0x0f)
#define PGM (seq[i].data[j + 1])
		/* REALLY STUPID way to preload GUS, but it works */
		for (i = 0; i < ntrks; i++)
		    for (j = 0; j < seq[i].length - 5; j++)
			if (ISGUS(CHN) && !(PGM & 0x80) &&
			    ((CMD == MIDI_PGM_CHANGE && !ISPERC(CHN))
			     || (CMD == MIDI_NOTEON && ISPERC(CHN))))
			    gus_load(ISPERC(CHN) ? PGM + 128 :
				     useprog[CHN] ? useprog[CHN] - 1 :
				     MT32 ? mt32pgm[PGM] : PGM);
		/* make sure that some program was loaded to use */
		for (j = 0; patchloaded[j] != 1 && j < 128; j++);
		if (j > 127)
		    gus_load(0);
	    }
	    newprog = 1;	/* if there's an error skip to next file */
	    if (error > 0)	/* error holds number of tracks read */
		while ((newprog = playevents()) == 0);
	    if (find_header)	/* play headers following selected */
		find_header += newprog;
	} while (find_header);
	if ((i += newprog) < optind)
	    i = optind;		/* can't skip back past first file */
	free(filebuf);
    }
    close(seqfd);
    close_show(0);
    exit(0);			/* this statement is here to keep the compiler happy */
}
/* end of file */
