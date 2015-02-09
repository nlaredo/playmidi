/************************************************************************
 * seq2mid.c - converts dump of /dev/sequencer (aka toy.c ouput) to a
 * type 0 midi file.
 *
 * This code was written by by Nathan Laredo (laredo@gnu.ai.mit.edu)
 * Source code may be freely distributed in unmodified form.
 *************************************************************************/
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/soundcard.h>

int outfile, infile;

/*
 * midi header, track header, and timing info.  Given timing is good for
 * /dev/sequencer tick of 100Hz.  Adjust division for others.
 */

#define FLUFFSIZE 29
unsigned char midifluff[FLUFFSIZE] =
{ 0x4d, 0x54, 0x68, 0x64,	/* MThd */
  0x00, 0x00, 0x00, 0x06,	/* 6 bytes in header block */
  0x00, 0x00,			/* midi format 0 */
  0x00, 0x01,			/* one track */
/* the following line is for a 100Hz sequencer tick, adjust accordingly */
  0x00, 0x32,			/* 50 ticks per quarter, 100Hz resolution */
  0x4d, 0x54, 0x72, 0x6b,	/* MTrk */
#define SIZEINDEX 18
  0x00, 0x00, 0x00, 0x00,	/* x bytes in track block */
  0x00,	0xff, 0x51, 0x03,	/* meta tempo event */
  0x07, 0xA1, 0x20		/* one quarter note = .5 sec = 120bpm */
#define STARTCOUNT 7
  };

#define ENDFLUFFSIZE 4
unsigned char endfluff[ENDFLUFFSIZE] =
{ 0x00, 0xff, 0x2f, 0x00 };	/* meta end of track */

/* indexed by high nibble of command */
int cmdlen[16] =
{0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 1, 1, 2, 0};

void midiwrite(buf, count)
unsigned char *buf;
int count;
{
    if(write(outfile, buf, count) < count) {
	perror("write");
	exit(-1);
    }
}

int main(argc, argv)
int argc;
char **argv;
{
    unsigned char delta[4], inputbuf[5], mid[8], cmd = 0;
    unsigned int oldticks = 0, ticks = 0, db = 0;
    off_t filesize, tracksize;
    int i, status = 0;

    if (argc < 3) {
	fprintf(stderr, "usage: %s infile.seq outfile.mid\n", argv[0]);
	exit(1);
    }
    if ((infile = open(argv[1], O_RDONLY, 0)) < 0) {
	perror(argv[1]);
	exit(-1);
    }
    if ((outfile = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0666))
	< 0) {
	perror(argv[2]);
	exit(-1);
    }
    midiwrite(midifluff, FLUFFSIZE);
    inputbuf[4] = 0;
    while (read(infile, inputbuf, 4) == 4) {
	if (inputbuf[0] == SEQ_WAIT)
	    ticks = (*(unsigned int *) &inputbuf[1]);
	if (*inputbuf == SEQ_MIDIPUTC) {
	    if (inputbuf[1] & 0x80)
		cmd = mid[db = 0] = inputbuf[1];
	    else
		mid[db] = inputbuf[1];
	    db++;
	    if (db == cmdlen[cmd >> 4] + 1) {
		register unsigned int buffer = ticks - oldticks;
		delta[i = 3] = (buffer & 0x7f);
		while ((buffer >>= 7) > 0 && i > 0)
		    delta[--i] = (buffer & 0x7f) | 0x80;
		midiwrite(&delta[i], 4 - i);
		if (status == cmd && cmd < 0xf0)
		    midiwrite(&mid[1], db - 1);
		else
		    midiwrite(mid, db);
		status = mid[0];
		oldticks = ticks;
		db = 1;
	    }
	} 
    }
    midiwrite(endfluff, ENDFLUFFSIZE);

    /* write big endian track size */
    filesize = lseek(outfile, 0, SEEK_CUR);
    tracksize = filesize - FLUFFSIZE + STARTCOUNT;
    lseek(outfile, SIZEINDEX, SEEK_SET);
    delta[0] = (tracksize >> 24) & 0xff;
    delta[1] = (tracksize >> 16) & 0xff;
    delta[2] = (tracksize >> 8) & 0xff;
    delta[3] = tracksize & 0xff;
    midiwrite(delta, 4);
    close(infile);
    close(outfile);
    printf("%s: saved as %s, %d bytes, %d bytes track data\n",
	   argv[1], argv[2], (int) filesize, (int) tracksize);
    exit(0);
}
/* end of file */
