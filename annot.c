/*
 * Metaann
 *
 * Copyright (C) 1990-2010 George B. Moody
 * Copyright (C) 2014 Benjamin Moody
 *
 * Based on 'annot.c' from the WAVE package (revised 22 June 2010);
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
#include <sys/time.h>
#include <wfdb/ecgmap.h>

/* The ANSI C function strstr is defined here for those systems which don't
   include it in their libraries.  This includes all older (pre-ANSI) C
   libraries;  some modern non-ANSI C libraries (notably those supplied with
   SunOS 4.1) do have strstr, so we can't just make this conditional on
   __STDC__. */
#ifdef sun
#ifdef i386
#define NOSTRSTR
#endif
#endif

#ifdef NOSTRSTR
char *strstr(s1, s2)
char *s1, *s2;
{
    char *p = s1;
    int n;

    if (s1 == NULL || s2 == NULL || *s2 == '\0') return (s1);
    n = strlen(s2);
    while ((p = strchr(p, *s2)) && strncmp(p, s2, n))
	p++;
    return (p);
}
#endif

void set_frame_title();
int badname(char *p);

int wave_text_width(const char *str, int length)
{
    PangoRectangle r;
    pango_layout_set_text(wave_text_layout, str, length);
    pango_layout_get_extents(wave_text_layout, NULL, &r);
    return PANGO_PIXELS_CEIL(r.width);
}

void wave_draw_string(GdkDrawable *drawable, GdkGC *gc,
		      int x, int y, const char *str, int length)
{
    pango_layout_set_text(wave_text_layout, str, length);
    gdk_draw_layout(drawable, gc, x, y + wave_view_font_offset,
		    wave_text_layout);
}

struct ap *get_ap()
{
    struct ap *a;

    if ((a = (struct ap *)malloc(sizeof(struct ap))) == NULL) {
/*
#ifdef NOTICE
	Xv_notice notice = xv_create((Frame)frame, NOTICE,
				     XV_SHOW, TRUE,
#else
	(void)notice_prompt((Frame)frame, (Event *)NULL,
#endif
		      NOTICE_MESSAGE_STRINGS,
		      "Error in allocating memory for annotations\n", 0,
		      NOTICE_BUTTON_YES, "Continue", 0);
#ifdef NOTICE
	xv_destroy_safe(notice);
#endif
*/
	g_warning("Error in allocating memory for annotations");
    }
    return (a);
}

int annotations;	/* non-zero if there are annotations to be shown */
time_t tupdate;		/* time of last update to annotation file */

/* Annot_init() (re)opens annotation file(s) for the current record, and
   reads the annotations into memory.  The function returns 0 if no annotations
   can be read, 1 if some annotations were read but memory was exhausted, or 2
   if all of the annotations were read successfully.  On return, ap_start and
   annp both point to the first (earliest) annotation, ap_end points to the
   last one, and attached and scope_annp are reset to NULL. */

int annot_init()
{
    char *p;
    struct ap *a;

    /* If any annotation editing has been performed, bring the output file
       up-to-date. */
    if (0 /*post_changes() == 0*/)
	return (0);	/* do nothing if the update failed (the user may be
			   able to recover by changing permissions or
			   clearing file space as needed) */

    /* Reset pointers. */
    attached = scope_annp = NULL;

    /* Free any memory that was previously allocated for annotations.
       This might take a while ... */
    /*if (frame) xv_set(frame, FRAME_BUSY, TRUE, NULL);*/
    while (ap_end) {
	a = ap_end->previous;
	if (ap_end->this.aux) free(ap_end->this.aux);
	free(ap_end);
	ap_end = a;
    }

    /* Check that the annotator name, if any, is legal. */
    if (nann > 0 && badname(af.name)) {
	char ts[ANLMAX+3];
	int dummy = (int)sprintf(ts, "`%s'", af.name);

	/*
#ifdef NOTICE
	Xv_notice notice = xv_create((Frame)frame, NOTICE,
				     XV_SHOW, TRUE,
#else
	(void)notice_prompt((Frame)frame, (Event *)NULL,
#endif
			    NOTICE_MESSAGE_STRINGS,
			    "The annotator name:",
			    ts,
			    "cannot be used.  Press `Continue', then",
			    "select an annotator name containing only",
			    "letters, digits, tildes, and underscores.", 0,
			    NOTICE_BUTTON_YES, "Continue", NULL);
#ifdef NOTICE
	xv_destroy_safe(notice);
#endif
	*/
	g_warning("The annotator name %s cannot be used", ts);

	af.name = NULL;
	annotator[0] = '\0';
	/*set_annot_item("");*/
	set_frame_title();
	return (annotations = 0);
    }

    /* Reset the frame title. */
    set_frame_title();

    /* Set time of last update to current time. */
    tupdate = time((time_t *)NULL);

    /* Return 0 if no annotations are requested or available. */
    if (getgvmode() & WFDB_HIGHRES) setafreq(freq);
    else setafreq(0.);
    if (nann < 1 || annopen(record, &af, 1) < 0) {
	ap_start = annp = scope_annp = NULL;
	/*if (frame) xv_set(frame, FRAME_BUSY, FALSE, NULL);*/
	return (annotations = 0);
    }
    if (getgvmode() & WFDB_HIGHRES) setafreq(freq);
    else setafreq(0.);
    if ((ap_start = annp = scope_annp = a = get_ap()) == NULL ||
	getann(0, &(a->this))) {
	(void)annopen(record, NULL, 0);
	/*if (frame) xv_set(frame, FRAME_BUSY, FALSE, NULL);*/
	return (annotations = 0);
    }

    /* Read annotations into memory, constructing a doubly-linked list on the
       fly. */
    do {
	a->next = NULL;
	a->previous = ap_end;
	if (ap_end) ap_end->next = a;
	ap_end = a;
	/* Copy the aux string, if any (since the aux pointer returned by
	   getann points to static memory that may be overwritten).  Return 1
	   if we run out of memory. */
	if (a->this.aux) {
	    if ((p = (char *)calloc(*(a->this.aux)+2, 1)) == NULL) {
		/*
		if (frame)
		    xv_set(frame, FRAME_BUSY, FALSE, NULL);
		if (frame) {
#ifdef NOTICE
		    Xv_notice notice = xv_create((Frame)frame, NOTICE,
						 XV_SHOW, TRUE,
#else
		    (void)notice_prompt((Frame)frame, (Event *)NULL,
#endif
		      NOTICE_MESSAGE_STRINGS,
		      "Error in allocating memory for aux string\n", 0,
		      NOTICE_BUTTON_YES, "Continue", 0);
#ifdef NOTICE
		    xv_destroy_safe(notice);
#endif
		}
		*/
		return (annotations = 1);
	    }
	    memcpy(p, a->this.aux, *(a->this.aux)+1);
	    a->this.aux = p;
	}
    } while ((a = get_ap()) && getann(0, &(a->this)) == 0);

    /* Return 1 if we ran out of memory while reading the annotation file. */
    if (a == NULL) {
	/*if (frame) xv_set(frame, FRAME_BUSY, FALSE, NULL);*/
	return (annotations = 1);
    }

    /* Return 2 if the entire annotation file has been read successfully. */
    else {
	free(a);	/* release last (unneeded) ap structure */
	/*if (frame) xv_set(frame, FRAME_BUSY, FALSE, NULL);*/
	return (annotations = 2);
    }
}

/* next_match() returns the time of the next annotation (i.e., the one later
   than and closest to those currently displayed) that matches the template
   annotation.  The mask specifies which fields must match. */
long next_match(template, mask)
struct WFDB_ann *template;
int mask;
{
    if (annotations && annp) {
	/* annp might be NULL if the annotation list is empty, or if the
	   last annotation occurs before display_start_time;  in either
	   case, next_match() returns -1. */
	do {
	    if (mask&M_ANNTYP) {
		if (template->anntyp) {
		    if (template->anntyp != annp->this.anntyp)
			continue;
		}
		else if ((annp->this.anntyp & 0x80) == 0)
		    continue;
	    }
	    if ((mask&M_SUBTYP) && template->subtyp != annp->this.subtyp)
		continue;
	    if ((mask&M_CHAN)   && template->chan   != annp->this.chan  )
		continue;
	    if ((mask&M_NUM)    && template->num    != annp->this.num   )
		continue;
	    if ((mask&M_AUX)    && (annp->this.aux == NULL ||
			  strstr(annp->this.aux+1, template->aux+1) == NULL))
		continue;
	    if ((mask&M_MAP2)&&template->anntyp!=map2(annp->this.anntyp))
		continue;
	    if (annp->this.time < display_start_time + nsamp)
		continue;
	    return (annp->this.time);
	} while (annp->next, annp = annp->next);
    }
    return (-1L);
}

/* previous_match() returns the time of the previous annotation (i.e., the one
   earlier than and closest to those currently displayed) that matches the
   template annotation.  The mask specifies which fields must match. */
long previous_match(template, mask)
struct WFDB_ann *template;
int mask;
{
    if (annotations) {
	/* annp might be NULL if the annotation list is empty, or if the
	   last annotation occurs before display_start_time.  In the first
	   case, previous_match() returns -1;  in the second case, it begins
	   its search with the last annotation in the list. */
	if (annp == NULL) {
	    if (ap_end && ap_end->this.time < display_start_time)
		annp = ap_end;
	    else
		return (-1L);
	}
	do {
	    if (mask&M_ANNTYP) {
		if (template->anntyp) {
		    if (template->anntyp != annp->this.anntyp)
			continue;
		}
		else if ((annp->this.anntyp & 0x80) == 0)
		    continue;
	    }
	    if ((mask&M_SUBTYP) && template->subtyp != annp->this.subtyp)
		continue;
	    if ((mask&M_CHAN)   && template->chan   != annp->this.chan  )
		continue;
	    if ((mask&M_NUM)    && template->num    != annp->this.num   )
		continue;
	    if ((mask&M_AUX)    && (annp->this.aux == NULL ||
			  strstr(annp->this.aux+1, template->aux+1) == NULL))
		continue;
	    if ((mask&M_MAP2)&&template->anntyp!=map2(annp->this.anntyp))
		continue;
	    if (annp->this.time > display_start_time)
		continue;
	    return (annp->this.time);
	} while (annp->previous, annp = annp->previous);
    }
    return (-1L);
}

/* Show_annotations() displays annotations between times left and left+dt at
appropriate x-locations in the ECG display area. */
void show_annotations(left, dt)
long left;
int dt;
{
    char buf[5], *p;
    int n, s, x, y, ytop, xs = -1, ys;
    long t, right = left + dt;

    if (annotations == 0) return;

    /* Find the first annotation to be displayed. */
    (void)locate_annotation(left, -128);  /* -128 is the lowest chan value */
    if (annp == NULL) return;

    /* Display all of the annotations in the window. */
    while (annp->this.time < right) {
	x = (int)((annp->this.time - left)*tscale);
	if (annp->this.anntyp & 0x80) {
	    y = ytop = abase; p = ".";
	}
	else switch (annp->this.anntyp) {
	  case NOTQRS:
	    y = ytop = abase; p = "."; break;
	  case NOISE:
	    y = ytop = abase - linesp;
	    if (annp->this.subtyp == -1) { p = "U"; break; }
	    /* The existing scheme is good for up to 4 signals;  it can be
	       easily extended to 8 or 12 signals using the chan and num
	       fields, or to an arbitrary number of signals using `aux'. */
	    for (s = 0; s < nsig && s < 4; s++) {
		if (annp->this.subtyp & (0x10 << s))
		    buf[s] = 'u';	/* signal s is unreadable */
		else if (annp->this.subtyp & (0x01 << s))
		    buf[s] = 'n';	/* signal s is noisy */
		else
		    buf[s] = 'c';	/* signal s is clean */
	    }
	    buf[s] = '\0';
	    p = buf; break;
	  case STCH:
	  case TCH:
	  case NOTE:
	    y = ytop = abase - linesp;
	    if (!show_aux && annp->this.aux) p = annp->this.aux+1;
	    else p = annstr(annp->this.anntyp);
	    break;
	  case LINK:
	    y = ytop = abase - linesp;
	    if (!show_aux && annp->this.aux) {
		char *p1 = annp->this.aux + 1, *p2 = p1 + *(p1-1);
		p = p1;
		while (p1 < p2) {
		    if (*p1 == ' ' || *p1 == '\t') {
			p = p1 + 1;
			break;
		    }
		    p1++;
		}
	    }	
	    break;		
	  case RHYTHM:
	    y = ytop = abase + linesp;
	    if (!show_aux && annp->this.aux) p = annp->this.aux+1;
	    else p = annstr(annp->this.anntyp);
	    break;
	  case INDEX_MARK:
	    y = ytop = abase - linesp;
	    p = ":";
	    break;
	  case BEGIN_ANALYSIS:
	    y = ytop = abase - linesp;
	    p = "<";
	    break;
	  case END_ANALYSIS:
	    y = ytop = abase - linesp;
	    p = ">";
	    break;
	  case REF_MARK:
	    y = ytop = abase - linesp;
	    p = ";";
	    break;
	  default:
	    y = ytop = abase; p = annstr(annp->this.anntyp); break;
	}
	if (ann_mode == 2 && y == abase) {
	    int yy = y + annp->this.num*vscalea;

	    if (xs >= 0)
		gdk_draw_line(gtk_widget_get_window(wave_view),
			      draw_ann, xs, ys, x, yy);
	    xs = x;
	    ys = yy;
	}
	else {
	    if (ann_mode == 1 && (unsigned)annp->this.chan < nsig) {
		if (sig_mode == 0)
		    y = ytop +=
			base[(unsigned)annp->this.chan] - abase + mmy(2);
		else {
		    int i;

		    for (i = 0; i < siglistlen; i++)
			if (annp->this.chan == siglist[i]) {
			    y = ytop += base[i] - abase + mmy(2);
			    break;
			}
		}
	    }

	    n = strlen(p);
	    if (n > 3 && !overlap && annp->next &&
		(annp->next)->this.time < right) {
	        int maxwidth;

		maxwidth = (int)(((annp->next)->this.time-annp->this.time)*
				 tscale)
		          - wave_text_width(" ", 1);

	        while (n > 3 && wave_text_width(p, n) > maxwidth)
		    n--;
	    }
	    wave_draw_string(gtk_widget_get_window(wave_view),
			     annp->this.anntyp == LINK ? draw_sig : draw_ann,
			     x, y, p, n);

	    if (annp->this.anntyp == LINK) {
		int xx = x + wave_text_width(p, n), yy = y + linesp/4;

		gdk_draw_line(gtk_widget_get_window(wave_view),
			      draw_sig, x, yy, xx, yy);
	    }
	
	    if (show_subtype) {
		sprintf(buf, "%d", annp->this.subtyp); p = buf; y += linesp;
		wave_draw_string(gtk_widget_get_window(wave_view),
				 draw_ann, x, y, p, strlen(p));
	    }
	    if (show_chan) {
		sprintf(buf, "%d", annp->this.chan); p = buf; y += linesp;
		wave_draw_string(gtk_widget_get_window(wave_view),
				 draw_ann, x, y, p, strlen(p));
	    }
	    if (show_num) {
		sprintf(buf, "%d", annp->this.num); p = buf; y += linesp;
		wave_draw_string(gtk_widget_get_window(wave_view),
				 draw_ann, x, y, p, strlen(p));
	    }
	    if (show_aux && annp->this.aux != NULL) {
		p = annp->this.aux + 1; y += linesp;
		wave_draw_string(gtk_widget_get_window(wave_view),
				 draw_ann, x, y, p, strlen(p));
	    }
	}
	if (show_marker && !gvflag && annp->this.anntyp != NOTQRS) {
	    GdkSegment marker[2];

	    marker[0].x1 = marker[0].x2 = marker[1].x1 = marker[1].x2 = x;
	    if (ann_mode == 1 && (unsigned)annp->this.chan < nsig) {
		unsigned int c = (unsigned)annp->this.chan;

		if (sig_mode == 1) {
		    int i;

		    for (i = 0; i < siglistlen; i++)
			if (c == siglist[i]) {
			    c = i;
			    break;
			}
		    if (i == siglistlen) {
			marker[0].y1 = 0;
			marker[1].y2 = canvas_height;
		    }
		    else {
			marker[0].y1 = (c == 0) ? 0 : (base[c-1] + base[c])/2;
			marker[1].y2 = (c == nsig-1) ? canvas_height :
			    (base[c+1] + base[c])/2;
		    }
		}
		else {
		    marker[0].y1 = (c == 0) ? 0 : (base[c-1] + base[c])/2;
		    marker[1].y2 = (c == nsig-1) ? canvas_height :
			(base[c+1] + base[c])/2;
		}
	    }
	    else {
		marker[0].y1 = 0;
		marker[1].y2 = canvas_height;
	    }
	    marker[0].y2 = ytop - linesp;
	    marker[1].y1 = y + mmy(2);
	    gdk_draw_segments(gtk_widget_get_window(wave_view),
			      draw_ann, marker, 2);
	}
	if (annp->next == NULL) break;
	annp = annp->next;
    
    }
}

void clear_annotation_display()
{
    if (ann_mode == 1 || (use_overlays && show_marker)) {
	gdk_draw_rectangle(gtk_widget_get_window(wave_view), clear_ann, TRUE,
		       0, 0, canvas_width+mmx(10), canvas_height);
	if (!use_overlays)
	    do_disp();
    }
    else
	gdk_draw_rectangle(gtk_widget_get_window(wave_view), clear_ann, TRUE,
		       0, abase-mmy(8), canvas_width+mmx(10), mmy(13));
}

/* This function locates an annotation at time t, attached to signal s, in the
   annotation list.  If there is no such annotation, it returns NULL, and annp
   points to the annotation that follows t (annp is NULL if no annotations
   follow t).  If there is an annotation at time t, the function sets annp to
   point to the first such annotation, and returns the value of annp.  (More
   than one annotation may exist at time t;  if so, on return, annp points to
   the one with the lowest `chan' field than is no less than s.)
 */

struct ap *locate_annotation(t, s)
long t;
int s;
{
    /* First, find out which of ap_start, annp, and ap_end is nearest t. */
    if (annp == NULL) {
	if (ap_start == NULL) return (NULL);
	annp = ap_start;
    }
    if (annp->this.time < t) {
	if (ap_end == NULL || ap_end->this.time < t)
	    /* no annotations follow t */
	    return (annp = NULL);
	if (t - annp->this.time > ap_end->this.time - t)
	    annp = ap_end;	/* closer to end than to previous position */
    }
    else {
	if (t < ap_start->this.time) {	/* no annotations precede t */
	    annp = ap_start;
	    return (NULL);
	}
	if (t - ap_start->this.time < annp->this.time - t)
	    annp = ap_start;
    }

    /* Traverse the list to find the annotation at time t and signal s, or its
       successor. */
    while (annp->this.time >= t && annp->previous)
	annp = annp->previous;
    while ((annp && annp->this.time < t) ||
	   (annp->this.time == t && annp->this.chan < s))
	annp = annp->next;
    if (annp == NULL || annp->this.time != t || annp->this.chan != s)
	return (NULL);
    else
	return (annp);
}

/* Reset the base frame title. */
void set_frame_title()
{
    static char frame_title[7+RNLMAX+1+ANLMAX+2+DSLMAX+1];
         /* 7 for "Record ", RNLMAX for record name, 1 for " " or "(",
	    ANLMAX for annotator name, 2 for "  " or ") ",
	    DSLMAX for description from log file, 1 for null */

    g_snprintf(frame_title, sizeof(frame_title), "Record %s", record);
    gtk_window_set_title(GTK_WINDOW(gtk_widget_get_toplevel(wave_view)),
			 frame_title);
}

/* Reset the base frame footer. */
void set_frame_footer()
{
    /* Keep this in sync with grid choice menu in modepan.c! */
    /*
    static char *grid_desc[7] = { "",
				 "Grid intervals: 0.2 sec",
				 "Grid intervals: 0.5 mV",
				 "Grid intervals: 0.2 sec x 0.5 mV",
				 "Grid intervals: 0.04 sec x 0.1 mV",
				 "Grid intervals: 1 min x 0.5 mV",
				 "Grid intervals: 1 min x 0.1 mV" };

    xv_set(frame, FRAME_RIGHT_FOOTER, grid_desc[grid_mode], NULL);
    if (attached && (attached->this).aux &&
	display_start_time <= (attached->this).time &&
	(attached->this).time < display_start_time + nsamp)
        xv_set(frame, FRAME_LEFT_FOOTER, attached->this.aux + 1, NULL);
    else if (time_mode) {
	int tm_save = time_mode;

	time_mode = 0;
        xv_set(frame, FRAME_LEFT_FOOTER, wtimstr(display_start_time), NULL);
	time_mode = tm_save;
    }
    else {
	char *p = timstr(-display_start_time);

	if (*p == '[') {
	   time_mode = 1;
	   xv_set(frame, FRAME_LEFT_FOOTER, wtimstr(display_start_time), NULL);
	   time_mode = 0;
	}
	else
            xv_set(frame, FRAME_LEFT_FOOTER, "", NULL);
    }
    */
}

    
/* Return 1 if p[] would not be a legal annotator name, 0 otherwise. */
int badname(p)
char *p;
{
    char *pb;

    if (p == NULL || *p == '\0')
	return (1);	/* empty name is illegal */
    for (pb = p + strlen(p) - 1; pb >= p; pb--) {
	if (('a' <= *pb && *pb <= 'z') || ('A' <= *pb && *pb <= 'Z') ||
	    ('0' <= *pb && *pb <= '9') || *pb == '~' || *pb == '_')
	    continue;	/* legal character */
	else if (*pb == '/')
	    return (0);	/* we don't need to check directory names */
	else
	    return (1);	/* illegal character */
    }
    return (0);
}

int read_anntab()
{
    /*
    char *atfname, buf[256], *p1, *p2, *s1, *s2, *getenv(), *strtok();
    int a;
    FILE *atfile;

    if ((atfname =
	 defaults_get_string("wave.anntab","Wave.Anntab",getenv("ANNTAB"))) &&
	(atfile = fopen(atfname, "r"))) {
	while (fgets(buf, 256, atfile)) {
	    p1 = strtok(buf, " \t");
	    if (*p1 == '#') continue;
	    a = atoi(p1);
	    if (0 < a && a <= ACMAX && (p1 = strtok((char *)NULL, " \t"))) {
		p2 = p1 + strlen(p1) + 1;
	    if ((s1 = (char *)malloc(((unsigned)(strlen(p1) + 1)))) == NULL ||
		(*p2 &&
		 (s2 = (char *)malloc(((unsigned)(strlen(p2)+1)))) == NULL)) {
		wfdb_error("read_anntab: insufficient memory\n");
		return (-1);
	    }
	    (void)strcpy(s1, p1);
	    (void)setannstr(a, s1);
	    if (*p2) {
		(void)strcpy(s2, p2);
		(void)setanndesc(a, s2);
	    }
	    else
		(void)setanndesc(a, (char*)NULL);
	    }
	}
	(void)fclose(atfile);
	return (0);
    }
    else
	return (-1);
    */
    return (-1);
}

int write_anntab()
{
    char *atfname;
    FILE *atfile;
    int a;

    if ((atfname = getenv("ANNTAB")) &&
	(atfile = fopen(atfname, "w"))) {
	for (a = 1; a <= ACMAX; a++)
	    if (anndesc(a))
		(void)fprintf(atfile, "%d %s %s\n", a, annstr(a), anndesc(a));
	return (0);
    }
    else
	return (-1);
}    
