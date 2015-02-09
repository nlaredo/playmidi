/************************************************************************
 * beat.c - cheap little metronome to run in the background and use
 * system beep so as not to tie up /dev/sequencer.
 *
 * This code was written by by Nathan Laredo (laredo@gnu.ai.mit.edu)
 * Source code may be freely distributed in unmodified form.
 *************************************************************************/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(argc, argv)
int argc;
char **argv;
{
    unsigned long i, j;

    if (argc != 2) {
	fprintf(stderr, "usage: %s tempo in beats per minute\n", argv[0]);
	exit(1);
    }
    i = atoi(argv[1]);
    if (i < 48 || i > 384) {
	fprintf(stderr, "error: tempo should be between 48 and 384 bpm\n");
	exit(-1);
    }
    j = 60000000 / i;	/* get usec per beat */
    while (fprintf(stderr, "\7"))
	usleep(j);
    exit(0);
}
