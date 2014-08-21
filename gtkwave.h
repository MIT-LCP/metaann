/* file: xvwave.h    	G. Moody	27 April 1990
			Last revised:   10 June 2005
XView constants, macros, function prototypes, and global variables for WAVE

-------------------------------------------------------------------------------
WAVE: Waveform analyzer, viewer, and editor
Copyright (C) 1990-2005 George B. Moody

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place - Suite 330, Boston, MA 02111-1307, USA.

You may contact the author by e-mail (george@mit.edu) or postal mail
(MIT Room E25-505A, Cambridge, MA 02139 USA).  For updates to this software,
please visit PhysioNet (http://www.physionet.org/).
_______________________________________________________________________________

*/

#include <gtk/gtk.h>
#include "gtk-compat.h"

/* Default screen resolution.  All known X servers seem to know something about
   the screen size, even if what they know isn't true.  Just in case, these
   constants define default values to be used if Height/WidthMMOfScreen give
   non-positive results.  If your screen doesn't have square pixels, define
   DPMMX and DPMMY differently.  Note that these definitions can be made using
   -D... options on the C compiler's command line.

   If your X server is misinformed about your screen size, there are three
   possible remedies.  All of the X11R4 sample servers from MIT accept a `-dpi'
   option that allows you to override the server default at the time the
   server is started.  See the X man page for details.  Alternatively, use
   WAVE's `-dpi' option or `dpi' resource (less desirable solutions, since they
   don't solve similar problems that other applications are likely to
   encounter). */
#ifndef DPMM
#define DPMM	4.0	/* default screen resolution (pixels per millimeter) */
#endif
#ifndef DPMMX
#define DPMMX	DPMM
#endif
#ifndef DPMMY
#define DPMMY	DPMM
#endif

/* Annotations are displayed in the font specified by DEFANNFONT if no
   other choice is specified.  At present, there is no other way to specify
   a choice.  Ultimately, this should be done by reading a resource file. */
#ifndef DEFANNFONT
#define DEFANNFONT	"Sans 10"
#endif

/* Globally visible GTK+ objects. */
COMMON GtkWidget *wave_view;
COMMON PangoLayout *wave_text_layout;
COMMON int wave_view_font_offset;

/* Graphics contexts.  For each displayed object (signal, annotation, cursor,
   and grid) there are drawing and erasing graphics contexts;  in addition,
   there is a `clear_all' GC for erasing everything at once.  If change_color
   is zero (i.e., if we have not installed a customized color map), all of the
   clear_* contexts are equivalent to clear_all, except that clear_crs is
   equivalent to draw_crs (which uses GXinvert for the drawing function in this
   case). */
COMMON GdkGC *draw_sig, *clear_sig,
    *draw_ann, *clear_ann,
    *draw_crs, *clear_crs,
    *draw_grd, *draw_cgrd, *clear_grd,
    *highlight_sig, *unhighlight_sig,
    *clear_all;

/* Display lists

A display list contains all information needed to draw a screenful of signals.
For each of the nsig signals, a display list contains a list of the
(x, y) pixel coordinate pairs that specify the vertices of the polyline
that represents the signal.  

A cache of recently-used display lists (up to MAX_DISPLAY_LISTS of them) is
maintained as a singly-linked list;  the first display list is pointed
to by first_list.
*/

struct display_list {
    struct display_list *next;	/* link to next display list */
    long start;		/* time of first sample */
    int nsig;		/* number of signals */
    int npoints;	/* number of (input) points per signal */
    int ndpts;		/* number of (output) points per signal */
    int xmax;		/* largest x, expressed as window abscissa */
    int *sb;		/* signal baselines, expressed as window ordinates */
    GdkPoint **vlist;	/* vertex list pointers for each signal */
};
COMMON struct display_list *first_list;
COMMON GdkSegment *level;

extern int wave_text_width(const char *str, int length); /* in annot.c */
extern void wave_draw_string(GdkDrawable *drawable, GdkGC *gc,
                             int x, int y, const char *str, int length); /* in annot.c */
extern struct display_list *find_display_list(	/* in signal.c */
					      long time);

GtkWidget *create_wave_view(void);
void wave_view_force_reload(void);
void wave_view_force_recalibrate(void);

GtkWidget *create_wave_window(void);

const char * defaults_get_string(const char *name, const char *classname,
				 const char *ddefault);
int defaults_get_integer(const char *name, const char *classname, int ddefault);
int defaults_get_boolean(const char *name, const char *classname, int ddefault);

void set_grid_mode(int mode);
void set_sig_mode(int mode);
void set_ann_mode(int mode);
void set_overlap(int mode);
void set_time_scale(int i);
void set_ampl_scale(int i);
void set_record_and_annotator(const char *rec, const char *ann);
void set_display_start_time(WFDB_Time t);

