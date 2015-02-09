/************************************************************************
   io_xaw.c  -- shows midi events in X11 using Xaw widgets

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
#include "bitmaps.h"
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/Xaw/Form.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Toggle.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/StringDefs.h>
#include <sys/time.h>
#include <unistd.h>

extern int chanmask, play_gus, play_fm, play_ext, gus_dev, sb_dev, ext_dev;
extern int graphics, verbose, perc, ntrks;
extern int note_vel[16][128];
extern unsigned long int ticks;
extern char *filename, *gmvoice[256];
extern float skew;
extern void seq_reset();
extern struct chanstate channel[16];
extern void seq_stop_note(int, int, int, int);
extern struct timeval start_time;
struct timeval want_time, now_time;

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

XtAppContext context;
Widget toplevel, manager, titlebar, status, text, exit_button;
Widget scrollbar, skew_label, nextsong, repeatsong, prevsong;
Pixmap Exit, Next, Play, Prev;	/* button icons */
Widget cbutton[16];		/* program for each channel (also toggles) */
Widget cmeter[16];		/* vu meter for each channel */
#define TEXTSIZE	8192
char textbuf[TEXTSIZE];

int i, return_do_what;
int vu_level[16], vu_delta[16];

char *drumset[11] =
{
 "Standard Kit", "Room Kit", "Power Kit", "Electronic Kit", "TR-808 Kit",
    "Jazz Kit", "Brush Kit", "Orchestra Kit", "Sound FX Kit",
    "Program Kit", "MT-32 Kit"
};

#define SET(x) (x == 8 ? 1 : x >= 16 && x <= 23 ? 2 : x == 24 ? 3 : \
		x == 25  ? 4 : x == 32 ? 5 : x >= 40 && x <= 47 ? 6 : \
		x == 48 ? 7 : x == 56 ? 8 : x >= 96 && x <= 111 ? 9 : \
		x == 127 ? 10 : 0)

void close_show(error)
int error;
{
    exit(error);
}

void ExitCallback(w, client_data, call_data)
Widget w;
XtPointer client_data;
XtPointer call_data;
{
    close_show(0);
}

void MaskCallback(w, client_data, call_data)
Widget w;
XtPointer client_data;
XtPointer call_data;
{
    int i, j, k;
    for (i = 0; i < 16 && cbutton[i] != w; i++);
    if (i < 0 || i >= 16)
	return;
    if (call_data)		/* channel should be enabled */
	chanmask |= (1 << i);
    else {			/* need to stop all notes in channel */
	chanmask &= (~(1 << i));
	if (ISGUS(i))
	    k = gus_dev;
	else if (ISFM(i))
	    k = sb_dev;
	else
	    k = ext_dev;
	for (j = 0; j < 128; j++)
	    if (note_vel[i][j])
		seq_stop_note(k, i, j, note_vel[i][j] = 0);
    }
}

void SkewCallback(w, client_data, call_data)
Widget w;
XtPointer client_data;
XtPointer call_data;
{
    float position;

    position = *(float *) call_data;
    skew = (position * 3.75) + 0.25;
    sprintf(textbuf, "%1.2f", skew);
    XtVaSetValues(skew_label, XtNlabel, textbuf, NULL);
};

void SkewIncCallback(w, client_data, call_data)
Widget w;
XtPointer client_data;
XtPointer call_data;
{
    double s_position;
    int movewhere;

    movewhere = (int) call_data;
    if (movewhere < 0) {
	skew += 0.01;
	if (skew > 4.0)
	    skew = 4.0;
    } else if (movewhere > 0) {
	skew -= 0.01;
	if (skew < 0.25)
	    skew = 0.25;
    } else
	return;
    s_position = (skew - 0.25) / 3.75;	/* scale is 0.0 - 1.0 */
    XawScrollbarSetThumb(scrollbar, s_position, 0.01);
    sprintf(textbuf, "%1.2f", skew);
    XtVaSetValues(skew_label, XtNlabel, textbuf, NULL);
};

void PrevSongCallback(w, client_data, call_data)
Widget w;
XtPointer client_data;
XtPointer call_data;
{
    if ((skew += 0.01) > 4)
	seq_reset();
    return_do_what = -1;
}

void RepeatCallback(w, client_data, call_data)
Widget w;
XtPointer client_data;
XtPointer call_data;
{
    seq_reset();
    return_do_what = 0;
}

void NextSongCallback(w, client_data, call_data)
Widget w;
XtPointer client_data;
XtPointer call_data;
{
    seq_reset();
    return_do_what = 1;
}

void updateMeter(chn)
int chn;
{
    XtVaSetValues(cmeter[chn], XtNwidth, 5 + vu_level[chn], NULL);
}

XtIntervalId mytimerid;
Boolean return_now;
int oldsec = 0, oldusec = 0;

void TimerCallback(w, client_data, timerid)
Widget w;
XtPointer client_data;
XtIntervalId timerid;
{
    int i, sec, usec;

    /* update vu meter values */
    for (i = 0; i < 16; i++)
	if (vu_delta[i]) {	/* decay meter */
	    vu_level[i] -= vu_delta[i];
	    if (vu_level[i] < 0)
		vu_level[i] = vu_delta[i] = 0;
	    updateMeter(i);
	}
    gettimeofday(&now_time, NULL);
    sec = now_time.tv_sec - start_time.tv_sec;
    usec = now_time.tv_usec - start_time.tv_usec;
    if (usec < 0)
	(usec += 1000000, sec -= 1);
    usec /= 100000;	/* change to 10ths of a second */
    sprintf(textbuf, "%02d:%02d.%d", sec / 60, sec % 60, usec);
    if (sec > oldsec || usec > oldusec) {
	XtVaSetValues(status, XtNstring, textbuf, NULL);
	oldsec = sec; oldusec = usec;
    }
    if (return_do_what != NO_EXIT || cdeltat(&want_time, &now_time) < 15)
	return_now = True;
    else
	mytimerid = XtAppAddTimeOut(context, 100, TimerCallback, NULL);
}

XEvent event;

int updatestatus()
{
    XtInputMask mask;

    return_now = False;
    want_time.tv_sec = start_time.tv_sec + (ticks / 100);
    want_time.tv_usec = start_time.tv_usec + (ticks % 100) * 10000;
    if (want_time.tv_usec > 1000000)
	(want_time.tv_usec -= 1000000, want_time.tv_sec++);
    return_do_what = NO_EXIT;
    TimerCallback(context, NULL, 0);		/* start timer */
    do {	/* toolkit main loop */
	if ((mask = XtAppPending(context)))	/* non-blocking */
	    XtAppProcessEvent(context, mask);
#ifndef HOG_CPU
	if (!mask && !return_now) {	/* sleep 1/10th of a second */
	    usleep(750);
	}
#endif
    } while (!return_now || mask);
    return return_do_what;
}


void AppendText(s)
char *s;
{
    XawTextBlock textblk;
    static XawTextPosition whereami;


    if (s == NULL) {	/* reset text to empty */
	XtVaSetValues(text, XtNstring, "", NULL);
	whereami = 0;
	return;
    }
    textblk.firstPos = 0;
    textblk.length = strlen(s);
    textblk.ptr = s;
    textblk.format = XawFmt8Bit;
    XawTextReplace(text, whereami, whereami, &textblk);
    whereami += textblk.length;
}

#define TEXTBUF		(&textbuf[strlen(textbuf)])

void showevent(cmd, data, length)
int cmd;
unsigned char *data;
int length;
{
    int chn, note, vel, i;

    chn = (cmd & 0x0f);
    note = (data[0] & 0x7f);
    vel = (data[1] & 0x7f);
    if (cmd < 8 && cmd > 0) {
	strncpy(textbuf, data, length < TEXTSIZE - 4 ? length : TEXTSIZE - 3);
	textbuf[(length > TEXTSIZE + 2) ? TEXTSIZE - 2 : length] = '\0';
	sprintf(TEXTBUF, "\n");
	AppendText(textbuf);
	if (verbose > 2)
	    printf(textbuf);
    } else if (cmd & 0x80)
	switch (cmd & 0xf0) {
	case MIDI_NOTEON:
	    if (!vel && vu_level[chn])
		vu_delta[chn] = 32;
	    else
		(vu_level[chn] = vel, vu_delta[chn] = 0);
	    updateMeter(chn);
	    break;
	case MIDI_NOTEOFF:
	    if (vu_level[chn])
		vu_delta[chn] = vel / 2;
	    updateMeter(chn);
	    break;
	case MIDI_PGM_CHANGE:
	    if (!ISPERC(chn))
		XtVaSetValues(cbutton[chn], XtNlabel, gmvoice[note], NULL);
	    else
		XtVaSetValues(cbutton[chn], XtNlabel, drumset[SET(note)],
		NULL);
	    break;
	case 0xf0:
	case 0xf7:
	    sprintf(textbuf, "Sysex(%2x): ", cmd);
	    for (i = 0; i < length && i < (TEXTSIZE / 2) - 16; i++)
		sprintf(TEXTBUF, "%02x", data[i]);
	    sprintf(TEXTBUF, "\n");
	    AppendText(textbuf);
	    if (verbose > 2)
		printf(textbuf);
	    break;
	default:
	    break;
	}
}

void init_show()
{
    int i;
    double s_position;

    AppendText(NULL);	/* reset text window */
    s_position = (skew - 0.25) / 3.75;	/* scale is 0.0 - 1.0 */
    sprintf(textbuf, "%1.2f", skew);
    XawScrollbarSetThumb(scrollbar, s_position, 0.01);
    XtVaSetValues(skew_label, XtNlabel, textbuf, NULL);
    XStoreName(XtDisplay(toplevel), XtWindow(toplevel), filename);
    oldsec = oldusec = 0;
    for (i = 0; i < 16; i++) {
	vu_level[i] = vu_delta[i] = 0;
	updateMeter(i);
	sprintf(textbuf, "channel%02d", i + 1);
	XtVaSetValues(cbutton[i], XtNlabel, textbuf, NULL);
    }
    XtVaSetValues(status, XtNlabel, "00:00.0" , NULL);
}

void setup_show(argc, argv)
int argc;
char **argv;
{
    int i;
    Pixel bg, fg;
    Display *mydisp;
    Screen *myscreen;
    Window mywin;

    graphics++;			/* force -r option if not selected */
    toplevel = XtVaOpenApplication(&context, "XPlaymidi", NULL, 0,
	&argc, argv, NULL, sessionShellWidgetClass, XtNminWidth, 425,
	XtNwidth, 425, XtNminHeight, 479, XtNheight, 500, NULL);
    manager = XtVaCreateManagedWidget("controls", formWidgetClass,
	toplevel, XtNdefaultDistance, 2, NULL);
    titlebar = XtVaCreateManagedWidget("titlebar", labelWidgetClass,
	manager, XtNtop, XawChainTop, XtNright, XawChainRight, XtNleft,
	XawChainLeft, XtNborderWidth, 0, XtNfromVert, cbutton[15],
	XtNlabel, "X" RELEASE " by Nathan Laredo" , NULL);
    skew_label = XtVaCreateManagedWidget("skew", labelWidgetClass, manager,
	XtNborderWidth, 0, XtNleft, XawChainLeft, XtNfromVert, titlebar,
	XtNlabel, "1.00", NULL);
    scrollbar = XtVaCreateManagedWidget("scrollbar", scrollbarWidgetClass,
	manager, XtNorientation, XtorientHorizontal, XtNlength, 365,
	XtNfromVert, titlebar, XtNfromHoriz, skew_label, NULL);
    text = XtVaCreateManagedWidget("text", asciiTextWidgetClass, manager,
	XtNbottom, XawChainBottom, XtNleft, XawChainLeft, XtNfromVert,
	skew_label, XtNeditType, XawtextAppend, XtNheight, 400,
	XtNwidth, 132, XtNscrollHorizontal, XawtextScrollAlways,
	XtNscrollVertical, XawtextScrollAlways, XtNtype, XawAsciiString,
	XtNdisplayCaret, False, NULL);
    for (i = 0; i < 16; i++) {
	sprintf(textbuf, "channel%02d", i + 1);
	cbutton[i] = XtVaCreateManagedWidget(textbuf, toggleWidgetClass,
	    manager, XtNstate, (ISPLAYING(i) ? True : False),
	    XtNwidth, 100, XtNjustify, XtJustifyLeft, XtNfromHoriz, text,
	    XtNresizable, False, XtNresize, False, NULL);
	if (!i)
	    XtVaSetValues(cbutton[i], XtNfromVert, scrollbar, NULL);
	else
	    XtVaSetValues(cbutton[i], XtNfromVert, cbutton[i - 1], NULL);
	XtAddCallback(cbutton[i], XtNcallback, MaskCallback, NULL);
	sprintf(textbuf, "meter%02d", i + 1);
	cmeter[i] = XtVaCreateManagedWidget(textbuf, toggleWidgetClass,
	    manager, XtNsensitive, False, XtNfromHoriz, cbutton[i],
	    XtNlabel, "", XtNresize, False, XtNjustify, XtJustifyLeft,
	    XtNwidth, 5, XtNresizable, True, NULL);
	if (!i)
	    XtVaSetValues(cmeter[i], XtNfromVert, scrollbar, NULL);
	else
	    XtVaSetValues(cmeter[i], XtNfromVert, cmeter[i - 1], NULL);
    }
    exit_button = XtVaCreateManagedWidget("exit", commandWidgetClass,
	manager, XtNfromHoriz, text, XtNfromVert, cbutton[15],
	XtNinternalWidth, 1, XtNinternalHeight, 0, XtNborderWidth, 0, NULL);
    prevsong = XtVaCreateManagedWidget("prevfile", commandWidgetClass,
	manager, XtNfromHoriz, exit_button, XtNfromVert, cbutton[15],
	XtNinternalWidth, 1, XtNinternalHeight, 0, XtNborderWidth, 0, NULL);
    repeatsong = XtVaCreateManagedWidget("repeat", commandWidgetClass,
	manager, XtNfromHoriz, prevsong, XtNfromVert, cbutton[15],
	XtNinternalWidth, 1, XtNinternalHeight, 0, XtNborderWidth, 0, NULL);
    nextsong = XtVaCreateManagedWidget("nextfile", commandWidgetClass,
	manager, XtNfromHoriz, repeatsong, XtNfromVert, cbutton[15],
	XtNinternalWidth, 1, XtNinternalHeight, 0, XtNborderWidth, 0, NULL);
    status = XtVaCreateManagedWidget("status", asciiTextWidgetClass,
	manager, XtNfromHoriz, nextsong, XtNfromVert, cbutton[15],
	XtNborderWidth, 0, XtNstring, "--:--.-", XtNeditType, XawtextRead,
	XtNdisplayCaret, False, XtNuseStringInPlace, True, NULL);
    XtAddCallback(scrollbar, XtNjumpProc, SkewCallback, NULL);
    XtAddCallback(scrollbar, XtNscrollProc, SkewIncCallback, NULL);
    XtAddCallback(exit_button, XtNcallback, ExitCallback, NULL);
    XtAddCallback(nextsong, XtNcallback, NextSongCallback, NULL);
    XtAddCallback(repeatsong, XtNcallback, RepeatCallback, NULL);
    XtAddCallback(prevsong, XtNcallback, PrevSongCallback, NULL);
    myscreen = XtScreen(toplevel);
    mydisp = DisplayOfScreen(myscreen); 
    mywin = RootWindowOfScreen(myscreen);
    fg = WhitePixelOfScreen(myscreen);
    bg = BlackPixelOfScreen(myscreen);
    Exit = XCreatePixmapFromBitmapData(mydisp, mywin, Eject_bits,
	Eject_width, Eject_height, fg, bg, 1);
    XtVaSetValues(exit_button, XtNbitmap, Exit, NULL);
    Prev = XCreatePixmapFromBitmapData(mydisp, mywin, Prev_bits,
	Prev_width, Prev_height, fg, bg, 1);
    XtVaSetValues(prevsong, XtNbitmap, Prev, NULL);
    Play = XCreatePixmapFromBitmapData(mydisp, mywin, Play_bits,
	Play_width, Play_height, fg, bg, 1);
    XtVaSetValues(repeatsong, XtNbitmap, Play, NULL);
    Next = XCreatePixmapFromBitmapData(mydisp, mywin, Next_bits,
	Next_width, Next_height, fg, bg, 1);
    XtVaSetValues(nextsong, XtNbitmap, Next, NULL);
    XtRealizeWidget(toplevel);
}
