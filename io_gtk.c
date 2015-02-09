/************************************************************************
   io_gtk.c  -- shows midi events in X11/Gnome using Gtk widgets

   Copyright (C) 1995-1996 Nathan I. Laredo
   Copyright (C) 1997 Elliot Lee

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
#include <gtk/gtk.h>
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

typedef unsigned char Boolean;
#define True 1
#define False 0

/* Gives us t2 usecs - t1 usecs */
int cdeltat(struct timeval *t1, struct timeval *t2)
{
  long long d1 = (t1->tv_sec * 10000) + t1->tv_usec;
  long long d2 = (t2->tv_sec * 10000) + t2->tv_usec;
  return (int)(d1 - d2);
}

int cdeltat_old(t1, t2)
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

GtkWindow *mainwin;
GtkVBox *dispbox;
GtkHBox *mwbox, *btnbox;
GtkButton *exit_button, *nextsong, *prevsong, *repeatsong;
GtkToggleButton *cbutton[16];
GtkProgressBar *cmeter[16];
GtkHScale *scrollbar;
GtkAdjustment *sbadj;
GtkText *miditext;
GtkLabel *lbl_timer;
gint timer_tag, quittimer_tag;
#define TEXTSIZE 8192
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

void close_show(int error)
{
  gtk_exit(error);
}

void ExitCallback(GtkWidget *w)
{
  close_show(0);
}

void MaskCallback(GtkWidget *w)
{
    int i, j, k;
    for (i = 0; i < 16 && cbutton[i] != GTK_TOGGLE_BUTTON(w); i++);
    if (i < 0 || i >= 16)
	return;
    if (GTK_TOGGLE_BUTTON(w)->active)		/* channel should be enabled */
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

void SkewCallback(GtkScale *w)
{
  skew = (sbadj->value * 3.75) + 0.25;
}

void playCallback(GtkButton *w)
{
  seq_reset();
  if(w == nextsong)
    return_do_what = 1;
  else if(w == prevsong)
    return_do_what = -1;
  else if(w == repeatsong)
    return_do_what = 0;
  else
    g_print("Unknown button pressed\n");
}

void updateMeter(int chn)
{
  gtk_progress_bar_update(cmeter[chn], (5 + vu_level[chn]) / 200.0);
}

Boolean return_now;
int oldsec = 0, oldusec = 0;

void TimerCallback(gpointer data)
{
    int i, sec, usec;
    gettimeofday(&now_time, NULL);
    if(start_time.tv_sec && start_time.tv_usec) {
      sec = now_time.tv_sec - start_time.tv_sec;
      usec = now_time.tv_usec - start_time.tv_usec;
    } else
      sec = usec = 0;

    if (usec < 0)
	(usec += 1000000, sec -= 1);
    /* update vu meter values */
    for (i = 0; i < 16; i++)
	if (vu_delta[i]) {	/* decay meter */
	    vu_level[i] -= vu_delta[i];
	    if (vu_level[i] < 0)
		vu_level[i] = vu_delta[i] = 0;
	    if(sec > oldsec)
	      updateMeter(i);
	}
    usec /= 100000;	/* change to 10ths of a second */
    if(sec > oldsec) {
      sprintf(textbuf, "%02d:%02d.%d", sec / 60, sec % 60, usec);
      gtk_label_set(lbl_timer, textbuf);
      gtk_widget_queue_draw(lbl_timer);
      oldsec = sec; oldusec = usec;
    }
    if(return_do_what != NO_EXIT || cdeltat(&want_time, &now_time) < 100) {
      return_now = True;
      gtk_main_quit();
    }
}

void quittimer(gpointer data)
{
  gtk_timeout_remove(quittimer_tag);
  return_now = True;
  gettimeofday(&now_time, NULL);
  g_print("quittimer with %d\n", cdeltat(&want_time, &now_time));
  gtk_main_quit();
}

int updatestatus(void)
{
  return_now = False;
  want_time.tv_sec = start_time.tv_sec + (ticks / 100);
  want_time.tv_usec = start_time.tv_usec + (ticks % 100) * 10000;
  if (want_time.tv_usec > 1000000)
    (want_time.tv_usec -= 1000000, want_time.tv_sec++);
  return_do_what = NO_EXIT;
  /*
  gettimeofday(&now_time, NULL);
  if(cdeltat(&want_time, &now_time) < 100)
    return return_do_what;
  quittimer_tag = gtk_timeout_add((cdeltat(&want_time, &now_time)/1000) - 5,
				  quittimer, NULL);
  g_print("Timeout in %d ms\n", (cdeltat(&want_time, &now_time)/1000) - 5);
  */
  gtk_main();
  
  return return_do_what;
}

void AppendText(char *s)
{
  return;
}

#define TEXTBUF		(&textbuf[strlen(textbuf)])

void showevent(int cmd, unsigned char *data, int length)
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
	      gtk_label_set(GTK_BUTTON(cbutton[chn])->child, gmvoice[note]);
	    else
	      gtk_label_set(GTK_BUTTON(cbutton[chn])->child, drumset[SET(note)]);
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

void init_show(void)
{
  int i;
  sbadj->value = 1.0;
  gtk_widget_queue_draw(GTK_WIDGET(scrollbar));
  for(i = 0; i < 16; i++) {
    vu_level[i] = vu_delta[i] = 0;
    updateMeter(i);
    sprintf(textbuf, "channel%02d", i + 1);
    gtk_label_set(GTK_BUTTON(cbutton[i])->child, textbuf);
  }
  gtk_label_set(lbl_timer, "00:00.0");
  gtk_window_set_title(mainwin, filename);
}

void nuttin(void) {}

#define SW(x) gtk_widget_show(GTK_WIDGET(x))
void
setup_show(int argc, char **argv)
{
  int i;
  gtk_init(&argc, &argv);
  mainwin = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  graphics++;
  mwbox = GTK_HBOX(gtk_hbox_new(1, 5));
  SW(mwbox);
  btnbox = GTK_HBOX(gtk_hbox_new(1, 5));
  SW(btnbox);
  dispbox = GTK_VBOX(gtk_vbox_new(1, 5));
  SW(dispbox);
  gtk_box_pack_start_defaults(GTK_WIDGET(mwbox), dispbox);
  gtk_box_pack_start_defaults(GTK_WIDGET(dispbox), btnbox);
  gtk_window_set_title(mainwin,
		       "Gtk" RELEASE " by Nathan Laredo (Gtk by Elliot Lee)");
  sbadj = gtk_adjustment_new(1.0, 0.0, 2.0, 0.05, 1.0, 2.0); /* XXX ??? */
  scrollbar = GTK_HSCALE(gtk_hscale_new(sbadj));
  gtk_scale_set_draw_value(GTK_WIDGET(scrollbar), TRUE);
  gtk_scale_set_digits(GTK_WIDGET(scrollbar), 1);
  gtk_signal_connect(GTK_OBJECT(sbadj),
		     "changed", (GtkSignalFunc)SkewCallback, NULL);
  SW(scrollbar);
  gtk_box_pack_start_defaults(GTK_WIDGET(dispbox), scrollbar);
  for(i = 0; i < 16; i++) {
    GtkWidget *tmp;
    sprintf(textbuf, "channel%02d", i + 1);
    cbutton[i] = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label(textbuf));
    gtk_toggle_button_set_state(cbutton[i], TRUE);
    gtk_signal_connect(GTK_OBJECT(cbutton[i]),
		       "toggled", (GtkSignalFunc)MaskCallback,
		       NULL);
    gtk_widget_set_usize(cbutton[i], 80, 10);
    SW(cbutton[i]);
    sprintf(textbuf, "meter%02d", i + 1);
    cmeter[i] = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_widget_set_usize(cmeter[i], 50, 10);
    SW(cmeter[i]);
    tmp = gtk_hbox_new(1, 5);
    SW(tmp);
    gtk_box_pack_start_defaults(tmp, cbutton[i]);
    gtk_box_pack_start_defaults(tmp, cmeter[i]);
    gtk_box_pack_start_defaults(dispbox, tmp);
  }
  lbl_timer = gtk_label_new("00:00.0");
  SW(lbl_timer);
  gtk_box_pack_start_defaults(dispbox, lbl_timer);
  exit_button = GTK_BUTTON(gtk_button_new_with_label("Exit"));
  SW(exit_button);
  gtk_signal_connect(exit_button, "clicked", (GtkSignalFunc)ExitCallback,
		     0);
  prevsong = GTK_BUTTON(gtk_button_new_with_label("<<"));
  SW(prevsong);
  gtk_signal_connect(prevsong, "clicked", (GtkSignalFunc)playCallback,
		     NULL);
  repeatsong = GTK_BUTTON(gtk_button_new_with_label(" > "));
  SW(repeatsong);
  gtk_signal_connect(repeatsong, "clicked", (GtkSignalFunc)playCallback,
		     NULL);
  nextsong = GTK_BUTTON(gtk_button_new_with_label(">>"));
  SW(nextsong);
  gtk_signal_connect(nextsong, "clicked", (GtkSignalFunc)playCallback,
		     NULL);
  gtk_box_pack_start_defaults(btnbox, exit_button);
  gtk_box_pack_start_defaults(btnbox, nextsong);
  gtk_box_pack_start_defaults(btnbox, prevsong);
  gtk_box_pack_start_defaults(btnbox, repeatsong);
  /*  timer_tag = gtk_timeout_add(100, (GtkFunction)TimerCallback, NULL); */
  gtk_idle_add((GtkFunction)TimerCallback, NULL);
  gtk_container_add(mainwin, mwbox);
  gtk_main_iteration();
  SW(mainwin);
}
#undef SW
