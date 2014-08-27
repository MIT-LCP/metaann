/*
 * Metaann
 *
 * Copyright (C) 1990-2010 George B. Moody
 * Copyright (C) 2014 Benjamin Moody
 *
 * Based on 'xvwave.c' from the WAVE package (revised 28 October 2009);
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

#define INIT
#include "wave.h"
#include "gtkwave.h"

static GdkGC *bg_fill;

static int reload_signals, reload_annotations, recalibrate;

void set_record_and_annotator(const char *rec, const char *ann)
{
    char *r, *a;
    int i;

    r = g_strdup(rec ? rec : "");
    a = g_strdup(ann ? ann : "");

    /* If a new record has been selected, re-initialize. */
    if (reload_signals || strncmp(record, r, RNLMAX)) {
	wfdbquit();

	/* Reclaim memory previously allocated for baseline labels, if any. */
	for (i = 0; i < nsig; i++)
	    if (blabel[i]) {
		free(blabel[i]);
		blabel[i] = NULL;
	    }
	
	if (!record_init(r)) {
	    g_free(r);
	    g_free(a);
	    return;
	}
	annotator[0] = '\0';	/* force re-initialization of annotator if
				   record was changed */
	savebackup = 1;
    }

    /* If a new annotator has been selected, re-initialize. */
    if (reload_annotations || strncmp(annotator, a, ANLMAX)) {
	g_strlcpy(annotator, a, ANLMAX);
	if (annotator[0]) {
	    af.name = annotator; af.stat = WFDB_READ;
	    nann = 1;
	}
	else
	    nann = 0;
	annot_init();
	savebackup = 1;
    }

    reload_signals = reload_annotations = 0;
    g_free(r);
    g_free(a);
}

/* Handle exposures in the signal window. */
static void repaint(GtkWidget *w, GdkEventExpose *ev, gpointer data)
{
    set_record_and_annotator(record, annotator);

    if (recalibrate) {
	if (vscale)
	    vscale[0] = 0.0;
	calibrate();
    }

    recalibrate = 0;

    restore_grid();
    do_disp();
    /*restore_cursor();*/
}

static void resize(GtkWidget *w, GtkAllocation *alloc, gpointer data)
{
    int width, height;
    int canvas_width_mm, u;
    width = alloc->width;
    height = alloc->height;
    /* The manipulations below are intended to select a canvas width that
       will allow a (reasonably) standard time interval to be displayed.
       If the canvas is at least 125 mm wide, its width is truncated to
       a multiple of 25 mm;  otherwise, its width is truncated to a multiple
       of 5 mm. */
    canvas_width_mm = width/dmmx(1);
    if (canvas_width_mm > 125) {
	canvas_width_sec = canvas_width_mm / 25;
	canvas_width_mm = 25 * canvas_width_sec;
    }
    else {
	u = canvas_width_mm / 5;
	canvas_width_mm = u * 5;
	canvas_width_sec = u * 0.2;
    }
    canvas_width = mmx(canvas_width_mm);

    /* Similar code might be used to readjust the canvas height, but doing so
       seems unnecessary. */
    canvas_height = height;
    canvas_height_mv = canvas_height / dmmy(10);
    
    /* Recalibrate based on selected scales, clear the display list cache. */
    if (*record && gtk_widget_get_realized(w)) {
	set_baselines();
	alloc_sigdata(nsig > 2 ? nsig : 2);

	set_time_scale(tsa_index);
	set_ampl_scale(vsa_index);
    }
}

static int allowdottedlines;
static GdkColormap *colormap;
static GdkColor color[16];
static unsigned long pixel_table[16];
static int background_color, grid_color, cursor_color, annotation_color,
    signal_color;

static void realize(GtkWidget *w, gpointer data)
{
    const char *annfontname, *cname, *rstring;
    int i, j;
    int height_mm, width_mm;		/* screen dimensions, in millimeters */
    int height_px, width_px;		/* screen dimensions, in pixels */
    int use_color = 0;			/* if non-zero, there are >=4 colors */
    int grey = 0;			/* if non-zero, use grey shades only */
    int ncolors;
    static unsigned long mask[4];
    GdkScreen *screen;
    static GdkGCValues gcvalues;
    GdkCursor *cursor;
    static gint8 dash1[2] = { 1, 1 }, dash2[2] = { 2, 2 };
    PangoFontDescription *pfd;
    PangoRectangle ink, logical;
    GtkAllocation alloc;

    screen = gtk_widget_get_screen(w);

    if (dpmmx == 0) {
        rstring = defaults_get_string("wave.dpi", "Wave.Dpi", "0x0");
	(void)sscanf(rstring, "%lfx%lf", &dpmmx, &dpmmy);
	dpmmx /= 25.4;	/* convert dots/inch into dots/mm */
	dpmmy /= 25.4;
    }
    height_mm = gdk_screen_get_height_mm(screen);
    height_px = gdk_screen_get_height(screen);
    if (height_mm > 0) {
	if (dpmmy == 0.) dpmmy = ((double)height_px)/height_mm;
	else height_mm = height_px/dpmmy;
    }
    else { dpmmy = DPMMY; height_mm = height_px/dpmmy; }
    width_mm = gdk_screen_get_width_mm(screen);
    width_px = gdk_screen_get_width(screen);
    if (width_mm > 0) {
	if (dpmmx == 0.) dpmmx = ((double)width_px)/width_mm;
	else width_mm = width_px/dpmmx;
    }
    else { dpmmx = DPMMX; width_mm = width_px/dpmmx; }

    use_color = 1;
    grey = 0;
    use_overlays = 0;

    /* Determine the color usage. */
    background_color = 0;
    grid_color = 1;
    cursor_color = 2;
    annotation_color = 3;
    signal_color = 4;
    ncolors = 5;

    /* Set the desired RGB values. */
    background_color = 0;
    grid_color = 1;
    mask[0] = mask[1] = mask[2] = mask[3] = ~0;
    if (!use_color) {
	/*
	cursor_color = 1;
	annotation_color = 1;
	signal_color = 1;
	cname = defaults_get_string("wave.signalwindow.mono.background",
				    "Wave.SignalWindow.Mono.Background",
				    "white");
	if (strcmp(cname, "black") && strcmp(cname, "Black")) {
	    pixel_table[0] = WhitePixel(display, DefaultScreen(display));
	    pixel_table[1] = BlackPixel(display, DefaultScreen(display));
	    cursor_bg.red = cursor_bg.green = cursor_bg.blue = 255;
	    cursor_fg.red = cursor_fg.green = cursor_fg.blue = 0;
	}
	else {	/\* reverse-video monochrome mode *\/
	    pixel_table[0] = BlackPixel(display, DefaultScreen(display));
	    pixel_table[1] = WhitePixel(display, DefaultScreen(display));
	    cursor_bg.red = cursor_bg.green = cursor_bg.blue = 0;
	    cursor_fg.red = cursor_fg.green = cursor_fg.blue = 255;
	}
	ncolors = 2;
	*/
    }	
    else {
	/* Allocate a colormap for the signal window. */
	colormap = gtk_widget_get_colormap(wave_view);

	/* Try to get read/write color cells if possible. */
	/*
	if (use_overlays && XAllocColorCells(display, colormap, 0, mask, 4,
					     pixel_table, 1)) {
	*/
	if (0) {
	    cursor_color = 2;
	    annotation_color = 4;
	    signal_color = 8;
	    ncolors = 16;
	}

	/* Otherwise, set up for using the shared colormap. */
	else {
	    use_overlays = 0;
	    if (1 /*visual->map_entries >= 5*/) {
		cursor_color = 2;
		annotation_color = 3;
		signal_color = 4;
		ncolors = 5;
	    }
	    /* If we are here, there must be exactly four colors available. */
	    else {
		cursor_color = 2;
		annotation_color = 2;
		signal_color = 3;
		ncolors = 4;
	    }
	}

	/* Set the desired RGB values. */
	if (grey)
	    cname = defaults_get_string("wave.signalwindow.grey.background",
					"Wave.SignalWindow.Grey.Background",
					"white");
	else
	    cname = defaults_get_string("wave.signalwindow.color.background",
					"Wave.SignalWindow.Color.Background",
					"white");
	gdk_color_parse(cname, &color[background_color]);

	if (grey)
	    cname = defaults_get_string("wave.signalwindow.grey.grid",
					"Wave.SignalWindow.Grey.Grid",
					"grey75");
	else
	    cname = defaults_get_string("wave.signalwindow.color.grid",
					"Wave.SignalWindow.Color.Grid",
					"grey90");
	gdk_color_parse(cname, &color[grid_color]);

	if (cursor_color != annotation_color) {
	    if (grey)
		cname = defaults_get_string("wave.signalwindow.grey.cursor",
					    "Wave.SignalWindow.Grey.Cursor",
					    "grey50");
	    else
		cname = defaults_get_string("wave.signalwindow.color.cursor",
					    "Wave.SignalWindow.Color.Cursor",
					    "orange red");
	    gdk_color_parse(cname, &color[cursor_color]);
	}

	if (grey)
	    cname = defaults_get_string("wave.signalwindow.grey.annotation",
					"Wave.SignalWindow.Grey.Annotation",
					"grey25");
	else
	    cname = defaults_get_string("wave.signalwindow.color.annotation",
					"Wave.SignalWindow.Color.Annotation",
					"yellow green");
	gdk_color_parse(cname, &color[annotation_color]);

	if (cursor_color == annotation_color)
	    color[cursor_color] = color[annotation_color];

	if (grey)
	    cname = defaults_get_string("wave.signalwindow.grey.signal",
					"Wave.SignalWindow.Grey.Signal",
					"black");
	else
	    cname = defaults_get_string("wave.signalwindow.color.signal",
					"Wave.SignalWindow.Color.Signal",
					"blue");
	gdk_color_parse(cname, &color[signal_color]);

	if (use_overlays && 0) {
	    /*
	    color[3] = color[2];
	    color[7] = color[6] = color[5] = color[4];
	    color[15] = color[14] = color[13] = color[12] = color[11] =
		color[10] = color[9] = color[8];
	    for (i = 0; i < ncolors; i++) {
		color[i].pixel = pixel_table[0];
		if (i & 1) color[i].pixel |= mask[0];
		if (i & 2) color[i].pixel |= mask[1];
		if (i & 4) color[i].pixel |= mask[2];
		if (i & 8) color[i].pixel |= mask[3];
		color[i].flags = DoRed | DoGreen | DoBlue;
	    }
	    XStoreColors(display, colormap, color, ncolors);
	    */
	}
	else {
	    (void)gdk_colormap_alloc_color(colormap, &color[background_color], FALSE, TRUE);
	    (void)gdk_colormap_alloc_color(colormap, &color[grid_color], FALSE, TRUE);
	    (void)gdk_colormap_alloc_color(colormap, &color[cursor_color], FALSE, TRUE);
	    if (cursor_color != annotation_color)
		(void)gdk_colormap_alloc_color(colormap, &color[annotation_color], FALSE, TRUE);
	    else
		color[annotation_color] = color[cursor_color];
	    (void)gdk_colormap_alloc_color(colormap, &color[signal_color], FALSE, TRUE);
	}

	/* Get the indices into the color map and store them. */
	for (i = 0; i < ncolors; i++)
	    pixel_table[i] = color[i].pixel;
    }

    gdk_window_set_background(gtk_widget_get_window(w),
			      &color[background_color]);

    /* Create the graphics contexts for writing into the signal window.
       The signal window (except for the grid) is erased using clear_all. */
    gcvalues.foreground = gcvalues.background = color[background_color];
    /*gcvalues.plane_mask = use_overlays ? ~pixel_table[grid_color] : ~0;*/
    clear_all = gdk_gc_new_with_values(gtk_widget_get_window(w),
				       &gcvalues, GDK_GC_FOREGROUND);

    /* The grid is drawn using draw_grd, and erased using clear_grd. */
    gcvalues.foreground = color[grid_color];
    /*gcvalues.plane_mask = use_overlays ? gcvalues.foreground : ~0;*/
    allowdottedlines = defaults_get_boolean("wave.allowdottedlines",
					    "Wave.AllowDottedLines",
					    1);
    if (allowdottedlines && !use_color)
	gcvalues.line_style = GDK_LINE_ON_OFF_DASH;
    else
	gcvalues.line_style = GDK_LINE_SOLID;
    draw_grd = gdk_gc_new_with_values(gtk_widget_get_window(w), &gcvalues,
				      GDK_GC_BACKGROUND | GDK_GC_FOREGROUND |
				      GDK_GC_LINE_STYLE);
    gdk_gc_set_dashes(draw_grd, 0, dash1, 2);

    gcvalues.line_width = 2;
    draw_cgrd = gdk_gc_new_with_values(gtk_widget_get_window(w), &gcvalues,
				       GDK_GC_BACKGROUND | GDK_GC_FOREGROUND |
				       GDK_GC_LINE_WIDTH | GDK_GC_LINE_STYLE);
    gdk_gc_set_dashes(draw_cgrd, 0, dash1, 2);

    if (use_overlays) {
	/*
	gcvalues.foreground = gcvalues.background;
	gcvalues.plane_mask = mask[0];
	clear_grd = XCreateGC(display, xid, GCForeground | GCPlaneMask,
			      &gcvalues);
	*/
    }
    else
	clear_grd = clear_all;

    /* Editing cursors (boxes and bars) are drawn using draw_crs, and erased
       using clear_crs.  If we can't use a read/write color map for this
       purpose, the two GCs are identical, and we use GXxor to do the
       drawing. */
    if (allowdottedlines && !use_color)
	gcvalues.line_style = GDK_LINE_ON_OFF_DASH;
    else
	gcvalues.line_style = GDK_LINE_SOLID;
    if (use_overlays) {
	/*
	gcvalues.foreground = pixel_table[cursor_color];
	gcvalues.plane_mask = gcvalues.foreground;
	gcvalues.function =  GXcopy;
	draw_crs = XCreateGC(display, xid,
			     GCBackground | GCForeground | GCFunction |
			     GCLineStyle | GCPlaneMask, &gcvalues);
	gcvalues.foreground = gcvalues.background;
	gcvalues.plane_mask = mask[1];
	clear_crs = XCreateGC(display, xid, GCForeground | GCPlaneMask,
			      &gcvalues);
	*/
    }
    else {
	gcvalues.foreground = color[cursor_color];
	gcvalues.foreground.pixel ^= pixel_table[background_color];
	/*gcvalues.plane_mask = ~0;*/
	gcvalues.function = GDK_XOR;
	draw_crs = gdk_gc_new_with_values(gtk_widget_get_window(w), &gcvalues,
					  GDK_GC_BACKGROUND | GDK_GC_FOREGROUND |
					  GDK_GC_FUNCTION | GDK_GC_LINE_STYLE);
	clear_crs = draw_crs;
    }

    /* Annotations are printed using draw_ann, and erased using clear_ann. */
    annfontname = defaults_get_string("wave.signalwindow.font",
				      "Wave.SignalWindow.Font",
				      DEFANNFONT);
    pfd = pango_font_description_from_string(annfontname);
    wave_text_layout = gtk_widget_create_pango_layout(w, "M");
    pango_layout_set_font_description(wave_text_layout, pfd);
    pango_font_description_free(pfd);
    pango_layout_get_extents(wave_text_layout, &ink, &logical);
    linesp = PANGO_PIXELS(logical.height);

    /* huh??? logical rect doesn't seem to know where the baseline is */
    wave_view_font_offset = -PANGO_PIXELS(ink.y + ink.height);

    gcvalues.foreground = color[annotation_color];
    if (allowdottedlines)
	gcvalues.line_style = GDK_LINE_ON_OFF_DASH;
    else
	gcvalues.line_style = GDK_LINE_SOLID;
    /*gcvalues.dashes = use_color ? 1 : 2;
    gcvalues.plane_mask = use_overlays ? gcvalues.foreground : ~0;*/
    draw_ann = gdk_gc_new_with_values(gtk_widget_get_window(w), &gcvalues,
				      GDK_GC_BACKGROUND | GDK_GC_FOREGROUND |
				      GDK_GC_LINE_STYLE);
    gdk_gc_set_dashes(draw_ann, 0, use_color ? dash1 : dash2, 2);
    if (use_overlays) {
	/*
	gcvalues.foreground = gcvalues.background;
	gcvalues.plane_mask = mask[2];
	clear_ann = XCreateGC(display, xid,
			 GCBackground | GCFont | GCForeground | GCPlaneMask,
			 &gcvalues);
	*/
    }
    else
	clear_ann = clear_all;

    /* Signals are drawn using draw_sig, and erased using clear_sig. */
    gcvalues.foreground = color[signal_color];
    /*gcvalues.plane_mask = use_overlays ? gcvalues.foreground : ~0;*/
    gcvalues.line_width = defaults_get_integer("wave.signalwindow.line_width",
					       "Wave.SignalWindow.Line_width",
					       1);
    draw_sig = gdk_gc_new_with_values(gtk_widget_get_window(w), &gcvalues,
				      GDK_GC_BACKGROUND | GDK_GC_FOREGROUND |
				      GDK_GC_LINE_WIDTH);
    if (use_overlays) {
	/*
	gcvalues.foreground = gcvalues.background;
	gcvalues.plane_mask = mask[3];
	clear_sig = XCreateGC(display, xid, GCForeground | GCPlaneMask,
			      &gcvalues);
	*/
    }
    else
	clear_sig = clear_all;

    /* Signals are highlighted using highlight_sig, and highlighting is
       removed using unhighlight_sig.  The settings are identical to those
       used for cursor drawing if overlays are enabled;  otherwise, the
       settings are those used for annotation drawing. */
    if (use_overlays) {
	highlight_sig = draw_crs;
	unhighlight_sig = clear_crs;
    }
    else {
	highlight_sig = draw_ann;
	unhighlight_sig = clear_ann;
    }

    gcvalues.foreground = color[background_color];
    bg_fill =  gdk_gc_new_with_values(gtk_widget_get_window(w), &gcvalues,
				      GDK_GC_FOREGROUND);
    if (!clear_all || !draw_grd || !draw_cgrd || !clear_grd || !draw_crs ||
	!clear_crs || !draw_ann || !clear_ann || !draw_sig || !clear_sig ||
	!bg_fill) {
	fprintf(stderr,  "Error allocating graphics context\n");
	return;
    }

    /* Create and install a cursor for the signal window.  (This is the
       crosshair cursor that tracks the mouse pointer, not to be confused
       with the editing cursor, which is a pair of bars extending the entire
       height of the window except for the annotation area, with an optional
       box around an annotation.) */
    cursor = gdk_cursor_new(GDK_CROSSHAIR);
    gdk_window_set_cursor(gtk_widget_get_window(w), cursor);

    gtk_widget_get_allocation(w, &alloc);
    resize(w, &alloc, NULL);
}

/* Hide_grid() makes the grid invisible by modifying the color map so that the
   color used for the grid is identical to that used for the background. */
void hide_grid()
{
    if (use_overlays) {
	/*
	color[background_color].pixel = pixel_table[grid_color];
	XStoreColor(display, colormap, &color[background_color]);
	color[background_color].pixel = pixel_table[background_color];
	*/
    }
}

/* Unhide_grid() reverses the action of hide_grid to make the grid visible
   again. */
void unhide_grid()
{
    if (use_overlays) {
	/*XStoreColor(display, colormap, &color[grid_color]);*/
    }
}

GtkWidget *create_wave_view()
{
    if (wave_view)
	return wave_view;

    tsa_index = vsa_index = ann_mode = overlap = sig_mode = time_mode =
	grid_mode = -1;
    tscale = 1.0;

    if (!show_subtype)
	show_subtype = defaults_get_boolean("wave.view.subtype",
					    "Wave.View.Subtype",
					    0);
    if (!show_chan)
	show_chan    = defaults_get_boolean("wave.view.chan",
					    "Wave.View.Chan",
					    0);
    if (!show_num)
	show_num     = defaults_get_boolean("wave.view.num",
					    "Wave.View.Num",
					    0);
    if (!show_aux)
	show_aux     = defaults_get_boolean("wave.view.aux",
					    "Wave.View.Aux",
					    0);
    if (!show_marker)
	show_marker  = defaults_get_boolean("wave.view.markers",
					    "Wave.View.Markers",
					    0);
    if (!show_signame)
	show_signame = defaults_get_boolean("wave.view.signalnames",
					    "Wave.View.SignalNames",
					    0);
    if (!show_baseline)
	show_baseline= defaults_get_boolean("wave.view.baselines",
					    "Wave.View.Baselines",
					    0);
    if (!show_level)
	show_level   = defaults_get_boolean("wave.view.level",
					    "Wave.View.Level",
					    0);
    if (tsa_index < 0) {
	tsa_index = fine_tsa_index =
	    defaults_get_integer("wave.view.timescale",
				 "Wave.View.TimeScale",
				 DEF_TSA_INDEX);
	coarse_tsa_index = defaults_get_integer("wave.view.coarsetimescale",
						"Wave.View.CoarseTimeScale",
						DEF_COARSE_TSA_INDEX);
    }
    if (vsa_index < 0)
	vsa_index    = defaults_get_integer("wave.view.amplitudescale",
					    "Wave.View.AmplitudeScale",
					    DEF_VSA_INDEX);
    if (ann_mode < 0)
	ann_mode     = defaults_get_integer("wave.view.annotationmode",
					    "Wave.View.AnnotationMode",
					    0);
    if (overlap < 0)
    	overlap      = defaults_get_integer("wave.view.annotationoverlap",
					    "Wave.View.AnnotationOverlap",
					    0);
    if (sig_mode < 0)
	sig_mode     = defaults_get_integer("wave.view.signalmode",
					    "Wave.View.SignalMode",
					    0);
    if (time_mode < 0)
	time_mode    = defaults_get_integer("wave.view.timemode",
					    "Wave.View.TimeMode",
					    0);
    if (grid_mode < 0) {
	grid_mode    = fine_grid_mode =
	               defaults_get_integer("wave.view.gridmode",
					    "Wave.View.GridMode",
					    0);
	coarse_grid_mode = defaults_get_integer("wave.view.coarsegridmode",
					    "Wave.View.CoarseGridMode",
					    0);
    }

    wave_view = gtk_drawing_area_new();
    g_signal_connect(wave_view, "realize", G_CALLBACK(realize), NULL);
    g_signal_connect(wave_view, "size-allocate", G_CALLBACK(resize), NULL);
    g_signal_connect(wave_view, "expose-event", G_CALLBACK(repaint), NULL);
    return wave_view;
}

void wave_view_force_reload()
{
    reload_signals = reload_annotations = 1;
}

void wave_view_force_recalibrate()
{
    recalibrate = 1;
}
