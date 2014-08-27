/*
 * Metaann
 *
 * Copyright (C) 1990-2009 George B. Moody
 * Copyright (C) 2014 Benjamin Moody
 *
 * Based on 'modepan.c' from the WAVE package (revised 12 May 2009);
 * modified by Benjamin Moody, 22 August 2014.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "wave.h"
#include "gtkwave.h"

void set_grid_mode(int mode)
{
    g_return_if_fail(mode >= 0 && mode <= 6);

    grid_mode = mode;
    switch (grid_mode) {
      case 0: ghflag = gvflag = visible = 0; break;
      case 1: ghflag = 0; gvflag = visible = 1; break;
      case 2: ghflag = visible = 1; gvflag = 0; break;
      case 3: ghflag = gvflag = visible = 1; break;
      case 4: ghflag = gvflag = visible = 2; break;
      case 5: ghflag = visible = 1; gvflag = 3; break;
      case 6: ghflag = visible = 2; gvflag = 3; break;
    }
    coarse_grid_mode = fine_grid_mode = grid_mode;
    gtk_widget_queue_draw(wave_view);
}

void set_sig_mode(int mode)
{
    g_return_if_fail(mode >= 0 && mode <= 2);

    if (mode != sig_mode || sig_mode == 2) {
	sig_mode = mode;
	set_baselines();
	gtk_widget_queue_draw(wave_view);
    }
}

void set_ann_mode(int mode)
{
    g_return_if_fail(mode >= 0 && mode <= 2);

    if (ann_mode != mode) {
	ann_mode = mode;
	gtk_widget_queue_draw(wave_view);
    }
}

void set_overlap(int mode)
{
    if (overlap != mode) {
	overlap = mode;
	gtk_widget_queue_draw(wave_view);
    }
}

void set_time_mode(int mode)
{
    g_return_if_fail(mode >= 0 && mode <= 2);

    time_mode = mode;
    if (nsig > 0 && time_mode == 1)
	(void)wtimstr(0L);	/* check if absolute times are available --
				   if not, time_mode is reset to 0 */
    gtk_widget_queue_draw(wave_view);
}

void set_time_scale(int i)
{
    /* The purpose of the complex method of computing canvas_width_sec is to
       obtain a "rational" value for it even if the frame has been resized.
       First, we determine the number of 5 mm time-grid units in the window
       (note that the resize procedure in xvwave.c guarantees that this will
       be an integer;  the odd calculation is intended to take care of
       roundoff error in pixel-to-millimeter conversion).  For each scale,
       the multiplier of u is simply the number of seconds that would be
       represented by 5 mm.   Since u is usually a multiple of 5 (except on
       small displays, or if the frame has been resized to a small size),
       the calculated widths in seconds are usually integers, at worst
       expressible as an integral number of tenths of seconds. */

    g_return_if_fail(i >= 0 && i < 15);

    int u = ((int)(canvas_width/dmmx(1) + 1)/5);	/* number of 5 mm
							   time-grid units */
    switch (tsa_index = i) {
    case 0:	/* 0.25 mm/hour */
	mmpersec = (0.25/3600.);
	canvas_width_sec = 72000 * u; break;
    case 1:	/* 1 mm/hour */
	mmpersec = (1./3600.);
	canvas_width_sec = 18000 * u; break;
    case 2:	/* 5 mm/hour */
	mmpersec = (5./3600.);
	canvas_width_sec = 3600 * u; break;
    case 3:	/* 0.25 mm/min */
	mmpersec = (0.25/60.);
	canvas_width_sec = 1200 * u; break;
    case 4:	/* 1 mm/min */
	mmpersec = (1./60.);
	canvas_width_sec = 300 * u; break;
    case 5:	/* 5 mm/min */
	mmpersec = (5./60.);
	canvas_width_sec = 60 * u; break;
    case 6:	/* 25 mm/min */
	mmpersec = (25./60.);
	canvas_width_sec = 12 * u; break;
    case 7:	/* 50 mm/min */
	mmpersec = (50./60.);
	canvas_width_sec = 6 * u; break;
    case 8:	/* 125 mm/min */
	mmpersec = (125./60.);
	canvas_width_sec = (12 * u) / 5; break;
    case 9:	/* 250 mm/min */
	mmpersec = (250./60.);
	canvas_width_sec = (6 * u) / 5; break;
    case 10:	/* 500 mm/min */
	mmpersec = (500./60.);
	canvas_width_sec = (3 * u) / 5; break;
    case 11:	/* 12.5 mm/sec */
	mmpersec = 12.5;
	canvas_width_sec = (2 * u) / 5; break;
    case 12:	/* 25 mm/sec */
	mmpersec = 25.;
	canvas_width_sec = u / 5; break;
    case 13:	/* 50 mm/sec */
	mmpersec = 50.;
	canvas_width_sec = u / 10; break;
    case 14:	/* 125 mm/sec */
	mmpersec = 125.;
	canvas_width_sec = u / 25; break;
    case 15:	/* 250 mm/sec */
	mmpersec = 250.;
	canvas_width_sec = u / 50.0; break;
    case 16:	/* 500 mm/sec */
	mmpersec = 500.;
	canvas_width_sec = u / 100.0; break;
    case 17:	/* 1000 mm/sec */
	mmpersec = 1000.;
	canvas_width_sec = u / 200.0; break;
    }

    coarse_tsa_index = fine_tsa_index = tsa_index;

    wave_view_force_recalibrate();
    gtk_widget_queue_draw(wave_view);
}

void set_ampl_scale(int i)
{
    static double vsa[] = { 1.0, 2.5, 5.0, 10.0, 20.0, 40.0, 100.0 };

    g_return_if_fail(i >= 0 && i < 7);

    /* Computation of canvas_height_mv could be as complex as above, but
       it doesn't seem so important to obtain a "rational" value here. */
    mmpermv = vsa[i];
    canvas_height_mv = canvas_height/dmmy(vsa[vsa_index = i]);
    wave_view_force_recalibrate();
    gtk_widget_queue_draw(wave_view);
}

/* Time-to-string conversion functions.  These functions use those in the
   WFDB library, but ensure that (1) elapsed times are displayed if time_mode
   is 0, and (2) absolute times (if available) are displayed without brackets
   if time_mode is non-zero. */

long wstrtim(s)
char *s;
{
    long t;

    while (*s == ' ' || *s == '\t') s++;
    if (time_mode == 1 && *s != '[' && *s != 's' && *s != 'c' && *s != 'e') {
	char buf[80];

	sprintf(buf, "[%s]", s);
	s = buf;
    }
    t = strtim(s);
    if (*s == '[') {	/* absolute time specified - strtim returns a negated
			   sample number if s is valid */
	if (t > 0L) t = 0L;	/* invalid s (before sample 0) -- reset t */
	else t = -t;	/* valid s -- convert t to a positive sample number */
    }
    return (t);
}

char *wtimstr(t)
long t;
{
    switch (time_mode) {
      case 0:
      default:
	if (t == 0L) return ("0:00");
	else if (t < 0L) t = -t;
	return (timstr(t));
      case 1:
	{
	    char *p, *q;

	    if (t > 0L) t = -t;
	    p = timstr(t);
	    if (*p == '[') {
		p++;
		q = p + strlen(p) - 1;
		if (*q == ']') *q = '\0';
	    }
	    else {
		time_mode = 0;
		/*if (tim_item) xv_set(tim_item, PANEL_VALUE, time_mode, NULL);*/
	    }
	    return (p);
        }
      case 2:
	{
	    static char buf[12];

	    if (t < 0L) t = -t;
	    sprintf(buf, "s%ld", t);
	    return (buf);
	}
    }
}

char *wmstimstr(t)
long t;
{
    switch (time_mode) {
      case 0:
      default:
	if (t == 0L) return ("0:00");
	else if (t < 0L) t = -t;
	return (mstimstr(t));
      case 1:
	{
	    char *p, *q;

	    if (t > 0L) t = -t;
	    p = mstimstr(t);
	    if (*p == '[') {
		p++;
		q = p + strlen(p) - 1;
		if (*q == ']') *q = '\0';
	    }
	    else {
		time_mode = 0;
		/*if (tim_item) xv_set(tim_item, PANEL_VALUE, time_mode, NULL);*/
	    }
	    return (p);
	}
      case 2:
	{
	    static char buf[12];

	    if (t < 0L) t = -t;
	    sprintf(buf, "s%ld", t);
	    return (buf);
	}
    }
}
