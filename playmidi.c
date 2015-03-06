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

struct miditrack seq[MAXTRKS];

int verbose = 0, chanmask = 0xffff, perc = 0x0200;
int dochan = 1, play_ext = 0;
int useprog[16], usevol[16];
int graphics = 0, reverb = 0, chorus = 0;
int find_header = 0, MT32 = 0;
FILE *mfd;
int ext_dev = 0;
unsigned long int default_tempo;
char *filename;
char *sf2_filename = "inst.sf2";
float skew = 1.0;
extern int ntrks;
extern int mt32pgm[128];
extern int playevents();
extern int gus_load(int);
extern int readmidi(unsigned char *, off_t);
extern void loadfm();
extern void setup_show(int, char **);
extern void close_show(int);

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

    printf("%s Copyright 2015 Nathan I. Laredo\n"
	   "This is free software with ABSOLUTELY NO WARRANTY.\n"
	   "For details please see the file COPYING.\n", RELEASE);
    for (i = 0; i < 16; i++)
	useprog[i] = usevol[i] = 0;	/* reset options */
    while ((i = getopt(argc, argv,
		     "c:aA:b:C:dD:eE:F:gh:G:i:lMp:P:rR:t:vV:x:z")) != -1)
	switch (i) {
        case 'b':
            sf2_filename = strdup(optarg);
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
		chanmask = strtoul(optarg, NULL, 16);
	    else
		chanmask |= strtoul(optarg, NULL, 16);
	    break;
	case 'e':
	    play_ext = 0xffff;
	    break;
	case 'D':
	    ext_dev = atoi(optarg);
	    break;
	case 'h':
	    find_header = atoi(optarg);
	    if (find_header < 1) {
		fprintf(stderr, "option -h header must be > 0\n");
		exit(1);
	    }
	    break;
	case 'i':
	    chanmask &= ~strtoul(optarg, NULL, 16);
	    break;
	case 'M':
	    MT32++;
	    break;
	case 'p':
	    if (strchr(optarg, ',') == NULL) {	/* set all channels */
		newprog = atoi(optarg);
		if (newprog < 1 || newprog > 128) {
		    fprintf(stderr, "option -p prog must be 1 - 128\n");
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
		    if (newprog < 1 || newprog > 128) {
			fprintf(stderr, "option -p prog must be 1 - 128\n");
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
	    play_ext = strtoul(optarg, NULL, 16);
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
            extra = optarg;
            perc = 0;
            do {
              j = atoi(extra);
              if (j < 1 || j > 16) {
                  fprintf(stderr, "option -P channel must be 1 - 16\n");
                  exit(1);
              }
              perc |= (1 << (j - 1));
              extra = strchr(extra, ',');
              if (extra) {
                extra++;
              }
	    } while (extra);
	    break;
        case 'l':
            init_midi();
            show_ports();
            exit(1);
            break;
	case 'v':
	    verbose++;
	    break;
	case 'V':
	    if (strchr(optarg, ',') == NULL) {	/* set all channels */
		newprog = atoi(optarg);
		if (newprog < 1 || newprog > 128) {
		    fprintf(stderr, "option -V vol must be 1 - 128\n");
		    exit(1);
		}
		for (j = 0; j < 16; j++)
		    usevol[j] = newprog;
	    } else {		/* set channels individually */
		extra = optarg;
		while (extra != NULL) {
		    j = atoi(extra);
		    if (j < 1 || j > 16) {
			fprintf(stderr, "opton -V chan must be 1 - 16\n");
			exit(1);
		    }
		    extra = strchr(extra, ',');
		    if (extra == NULL) {
			fprintf(stderr, "option -V vol needed for chan %d\n",
				j);
			exit(1);
		    } else
			extra++;
		    newprog = atoi(extra);
		    if (newprog < 1 || newprog > 128) {
			fprintf(stderr, "option -p prog must be 1 - 128\n");
			exit(1);
		    }
		    usevol[j - 1] = newprog;
		    extra = strchr(extra, ',');
		    if (extra != NULL)
			extra++;
		}
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
		"  -b sf2fn use sf2fn as filename for sf2 file to use\n"
		"  -l       list available midi ports for -D x option\n"
		"  -i x     ignore channels set in bitmask x (hex)\n"
		"  -c x     play only channels set in bitmask x (hex)\n"
		"  -x x     exclude channel x from playable bitmask\n"
		"  -p [c,]x play program x on channel c (all if no c)\n"
		"  -V [c,]x play channel c with volume x (all if no c)\n"
		"  -t x     skew tempo by x (float)\n"
		"  -d       don't play any percussion\n"
		"  -P x,[x] treat channel x as percussion\n"
		"  -e       output to external midi\n"
		"  -D x     output to midi device x\n"
		"  -h x     skip to header x in large archive\n"
		"  -E x     play channels in bitmask x external\n"
		"  -z       ignore channel of all events\n"
		"  -M       enable MT-32 to GM translation mode\n"
		"  -I       show list of all GM programs (see -p)\n"
		"  -R x     set initial reverb to x (0-127)\n"
		"  -C x     set initial chorus to x (0-127)\n"
		"  -r       real-time playback graphics\n");
	exit(1);
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
	    default_tempo = 500000;
	    /* error holds number of tracks read */
	    error = readmidi((unsigned char *)filebuf, info.st_size);
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
    close_midi();
    close_show(0);
    exit(0);			/* this statement is here to keep the compiler happy */
}
/* end of file */
