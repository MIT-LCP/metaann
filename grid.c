/*
 * Metaann
 *
 * Copyright (C) 1990-2010 George B. Moody
 * Copyright (C) 2014 Benjamin Moody
 *
 * Based on 'grid.c' from the WAVE package (revised 13 July 2010);
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

static int grid_plotted;

/* Call this function from the repaint procedure to restore the grid after
   the window has been cleared. */
void restore_grid()
{
    grid_plotted = 0;
    show_grid();
}

/* Show_grid() does what is necessary to display the grid in the requested
style.  Note that the grid can be made to disappear and reappear by show_grid()
without redrawing it, by manipulating the color map. */
void show_grid()
{
    int i, ii, x, xx, y, yy;
    double dx, dxfine, dy, dyfine, vm;
    static int oghf, ogvf, grid_hidden;
    static double odx, ody;

    /* If the grid should not be visible, hide it if necessary. */
    if (!visible) {
	if (grid_plotted && !grid_hidden) {
	    hide_grid();
	    grid_hidden = 1;
	}
	return;
    }

    /* Calculate the grid spacing in pixels */
    if (tmag <= 0.0) tmag = 1.0;
    switch (gvflag) {
      case 0:
      case 1: dx = tmag * seconds(0.2); break;
      case 2: dx = tmag * seconds(0.2); dxfine = tmag * seconds(0.04); break;
      case 3: dx = tmag * seconds(300.0); dxfine = tmag * seconds(60.0); break;
    }
    if (vmag == NULL || vmag[0] == 0.0) vm = 1.0;
    else vm = vmag[0];
    switch (ghflag) {
      case 0:
      case 1: dy = vm * millivolts(0.5); break;
      case 2: dy = vm * millivolts(0.5);
	  dyfine = vm * millivolts(0.1); break;
    }

    /* The grid must be drawn if it has not been plotted already, if the grid
       spacing or style has changed, or if we are not using a read/write color
       map. */
    if (!grid_plotted || ghflag != oghf || gvflag != ogvf ||
	(ghflag && dy != ody) || (gvflag && dx != odx) || !use_overlays) {
	gdk_draw_rectangle(gtk_widget_get_window(wave_view), clear_grd, TRUE,
		       0, 0, canvas_width,canvas_height);
	
	/* If horizontal grid lines are enabled, draw them. */
	if (ghflag)
	    for (i = y = 0; y < canvas_height + dy; i++, y = i*dy) {
		if (0 < y && y < canvas_height)
		    gdk_draw_line(gtk_widget_get_window(wave_view),
			      (ghflag > 1) ? draw_cgrd : draw_grd,
			      0, y, canvas_width, y);
		if (ghflag > 1)		/* Draw fine horizontal grid lines. */
		    for (ii = 1; ii < 5; ii++) {
			yy = y + ii*dyfine;
			gdk_draw_line(gtk_widget_get_window(wave_view), draw_grd,
				  0, yy, canvas_width, yy);
		    }
	    }

	/* If vertical grid lines are enabled, draw them. */
	if (gvflag)
	    for (i = x = 0; x < canvas_width + dx; i++, x = i*dx) {
		if (0 < x && x < canvas_width)
		    gdk_draw_line(gtk_widget_get_window(wave_view),
			      (gvflag > 1) ? draw_cgrd : draw_grd,
			      x, 0, x, canvas_height);
		if (gvflag > 1)		/* Draw fine vertical grid lines. */
		    for (ii = 1; ii < 5; ii++) {
			xx = x + ii*dxfine;
			gdk_draw_line(gtk_widget_get_window(wave_view), draw_grd,
				  xx, 0, xx, canvas_height);
		    }
	    }

        oghf = ghflag; ogvf = gvflag; odx = dx; ody = dy;
	grid_plotted = 1;
    }

    /* If the grid was hidden, make it visible by changing the color map. */
    if (grid_hidden) {
	unhide_grid();
        grid_hidden = 0;
    }
}
