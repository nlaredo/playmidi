/************************************************************************
 * toy.c - start of midi recording package.  Will take input from
 * /dev/sequencer and layer with an existing /dev/sequencer dump file,
 * saving the result as a new /dev/sequencer dump file.  In short, you
 * must have hardware midi devices to use this "toy".
 *
 * This program is an experiment in midi recording to solve some timing
 * and input/output issues that I've had while writing my midi studio
 * package (basically to test how to best use select() on /dev/sequencer).
 *
 * This code was written by by Nathan Laredo (laredo@gnu.ai.mit.edu)
 * Source code may be freely distributed in unmodified form.
 *************************************************************************/
#include <stdio.h>
#ifndef __FreeBSD__
#include <getopt.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#ifndef __FreeBSD__
#include <sys/soundcard.h>
#else
#include <machine/soundcard.h>
#endif
#include <sys/ioctl.h>


#define SEQUENCER_DEV	"/dev/sequencer"
#define SEQUENCERBLOCKSIZE	128

/*
 * The following are if you have more than one midi device on
 * your system as I do.   My Roland piano is on the 2nd midi port (GUS), and
 * my Korg daughterboard is on the 1st midi port (SB16).
 * Input will be taken from any midi device known by the kernel sequencer.
 * device.
 */

#define OUT_DEV 0

SEQ_DEFINEBUF(SEQUENCERBLOCKSIZE);
unsigned char inputbuf[SEQUENCERBLOCKSIZE];
unsigned char outputbuf[SEQUENCERBLOCKSIZE];
int seqfd, outfile, infile;

/* indexed by high nibble of command */
int cmdlen[16] =
{0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 1, 1, 2, 0};

void seqbuf_dump()
{
    if (_seqbufptr) {
	if (write(seqfd, _seqbuf, _seqbufptr) == -1) {
	    perror(SEQUENCER_DEV);
	    exit(-1);
	}
	if (write(outfile, _seqbuf, _seqbufptr) == -1) {
	    perror("write");
	    exit(-1);
	}
    }
    _seqbufptr = 0;
}

void seq_addevent(s, c)
unsigned char *s;
int c;
{
    int i;

    if (*s == SEQ_SYNCTIMER)	/* we want only one of these messages */
	return;
    _SEQ_NEEDBUF(c);
    for (i = 0; i < c; i++)
	_seqbuf[_seqbufptr + i] = s[i];
    _SEQ_ADVBUF(c);
}

int seqread(f, buf)
int f;
unsigned char *buf;
{
    return read(f, buf, 4);
}


int main(argc, argv)
int argc;
char **argv;
{
    extern char *optarg;
    extern int optind;
    int i, transpose = 0, channel = 0, program = 0, error = 0;
    unsigned char mid[8], imid[8], cmd = 0, icmd = 0;
    unsigned int oticks = 0, ticks = 0, db = 0, idb = 0, wait = 0;
    fd_set rdfs;
    struct timeval tv, start, now, want;

    while ((i = getopt(argc, argv, "c:p:wt:")) != -1)
	switch (i) {
	case 'c':
	    channel = atoi(optarg);
	    break;
	case 'p':
	    program = atoi(optarg);
	    break;
	case 'w':
	    wait++;
	    break;
	case 't':
	    transpose = atoi(optarg);
	    break;
	default:
	    error++;
	    break;
	}

    if (error || argc - optind != 2) {
	fprintf(stderr, "usage: %s [-t semitones]"
		" [-c channel] [-p program] [-wait] "
		"inputfile.seq outputfile.seq\n", argv[0]);
	exit(1);
    }
    if ((seqfd = open(SEQUENCER_DEV, O_RDWR, 0)) < 0) {
	perror("open " SEQUENCER_DEV);
	exit(-1);
    }
    if ((infile = open(argv[optind], O_RDONLY, 0)) < 0) {
	perror(argv[optind]);
	exit(-1);
    }
    if ((outfile = open(argv[optind + 1], O_WRONLY | O_CREAT | O_TRUNC, 0666))
	< 0) {
	perror(argv[optind + 1]);
	exit(-1);
    }
    ioctl(seqfd, SNDCTL_SEQ_RESET);
    if (program >= 1 && program <= 128) {
	if (channel < 1 || channel > 16)
	    channel = 1;
	SEQ_MIDIOUT(OUT_DEV, 0xc0 | (channel - 1));
	SEQ_MIDIOUT(OUT_DEV, program - 1);
    }
    /* extra byte for converting 3-byte timer ticks */
    inputbuf[4] = 0;
    outputbuf[4] = 0;
    if (wait) {	/* wait for midi input to start */
	FD_ZERO(&rdfs);
	FD_SET(seqfd, &rdfs);
	select(FD_SETSIZE, &rdfs, NULL, NULL, NULL);
	seqread(seqfd, outputbuf);	/* trash time stamp (hopefully) */
    }
    SEQ_START_TIMER();
    SEQ_DUMPBUF();
    gettimeofday(&start, NULL);
    while (ticks < 0xffffff) {
	FD_ZERO(&rdfs);
	FD_SET(infile, &rdfs);
	tv.tv_sec = tv.tv_usec = 0;	/* no wait */
	if (!select(FD_SETSIZE, &rdfs, NULL, NULL, &tv)) {
	    /* wait forever for more input -- mark end of input */
	    inputbuf[0] = SEQ_WAIT;
	    inputbuf[1] = inputbuf[2] = inputbuf[3] = 0xff;
	} else if (seqread(infile, inputbuf) < 4) {
	    inputbuf[0] = SEQ_WAIT;
	    inputbuf[1] = inputbuf[2] = inputbuf[3] = 0xff;
	}
	if (inputbuf[0] == SEQ_WAIT)
	    ticks = (*(unsigned int *) &inputbuf[1]);
	want.tv_sec = ticks / 100 + start.tv_sec;
	want.tv_usec = (ticks % 100) * 10000 + start.tv_usec;
	if (want.tv_usec >= 1000000) {
	    want.tv_usec -= 1000000;
	    want.tv_sec++;
	}
	if (*inputbuf == SEQ_MIDIPUTC) {
	    if (inputbuf[1] & 0x80)
		icmd = imid[idb = 0] = inputbuf[1];
	    else
		imid[idb] = inputbuf[1];
	    idb++;
	    if (idb == cmdlen[icmd >> 4] + 1) {
		fprintf(stderr, ">%8d ", ticks);
		for (i = 0; i < idb; i++) {
		    fprintf(stderr, "%02x", imid[i]);
		    SEQ_MIDIOUT(OUT_DEV, imid[i]);
		}
		fprintf(stderr, "\n");
		idb = 1;
	    }
	} else {
	    gettimeofday(&now, NULL);
	    while (timercmp(&now, &want, <)) {
		tv.tv_sec = want.tv_sec - now.tv_sec;
		tv.tv_usec = want.tv_usec - now.tv_usec;
		if (tv.tv_usec < 0) {
		    tv.tv_usec += 1000000;
		    tv.tv_sec--;
		}
		FD_ZERO(&rdfs);
		FD_SET(seqfd, &rdfs);
		if (select(FD_SETSIZE, &rdfs, NULL, NULL, &tv)) {
		    seqread(seqfd, outputbuf);
		    if (outputbuf[0] == SEQ_WAIT) {
			oticks = (*(unsigned int *) &outputbuf[1]);
			if (oticks - ticks)
			    seq_addevent(outputbuf, 4);
		    }
		    if (outputbuf[0] == SEQ_MIDIPUTC) {
			if (outputbuf[1] & 0x80)
			    cmd = (mid[db = 0] = outputbuf[1]) & 0xf0;
			else
			    mid[db] = outputbuf[1];
			db++;
			if (db == cmdlen[cmd >> 4] + 1) {
			    if (transpose && (cmd == 0x80 || cmd == 0x90))
				mid[1] += transpose;
			    fprintf(stderr, "<%8d ", oticks);
			    for (i = 0; i < db; i++) {
				if (channel >= 1 && channel <= 16)
				    mid[0] = cmd | (channel - 1);
				fprintf(stderr, "%02x", mid[i]);
				SEQ_MIDIOUT(OUT_DEV, mid[i]);
			    }
			    fprintf(stderr, "\n");
			    db = 1;
			    SEQ_DUMPBUF();
			}
		    }
		}
		gettimeofday(&now, NULL);
	    }
	    seq_addevent(inputbuf, 4);
	}
	SEQ_DUMPBUF();
    }
    /* should NEVER get to here, if we do, something is really screwed */
    close(seqfd);
    close(infile);
    close(outfile);
    exit(0);
}
/* end of file */
