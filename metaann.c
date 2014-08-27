/*
 * Metaann
 *
 * Copyright (C) 2014 Benjamin Moody
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wfdb/wfdb.h>
#include <wfdb/wfdblib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <errno.h>

#include "wave.h"
#include "gtkwave.h"
#include "conf.h"
#include "url.h"

/* Input database parameters */

#define TARGET_ANY 999999

static char *database_path;	 /* WFDB path */
static char *database_annotator; /* Name of annotator */
static char *database_calfile;	 /* Name of calibration file */
static char *record_list_url;	 /* File containing list of records */
static int target_anntyp = TARGET_ANY; /* ANNTYP of interest */
static int target_subtyp = TARGET_ANY; /* SUBTYP of interest */
static int target_num = TARGET_ANY;    /* NUM of interest */
static int target_chan = TARGET_ANY;   /* CHAN of interest */
static char *target_aux;	       /* (Prefix of) AUX string of interest */

static int cache_enabled;

/* Cached input data */

struct alarm_info {
  char *message;
  WFDB_Time time;
};

struct alarm_pos {
  int record_index;
  WFDB_Time time;
};

struct record_info {
  char *name;
  int n_alarms;
};

static int n_records;
static struct record_info *records;

static int cur_record_index;
static char *cur_record;

static int cur_record_n_alarms;
static struct alarm_info *cur_record_alarms;
static int cur_alarm_index;
static struct alarm_info *cur_alarm;

static int compare_mode;
static int n_alarms_to_compare;
static struct alarm_pos *alarms_to_compare; 

/* List of possible status codes (user responses) */

struct response_info {
  char *ui_name;
  char *status;
  char *substatus;
  int comment_required;
  int always_adjudicate;
  int never_adjudicate;
};

static int n_responses;
static struct response_info *responses;

/* List of user annotations */

struct result_info {
  char *record;
  WFDB_Time time;
  char *status;
  char *substatus;
  char *comment;
};

struct results_list {
  char *post_url;
  int n_results;
  struct result_info *results;
};

static struct results_list my_results;

static int n_reviewers;
static struct results_list **reviewer_results;

static const struct result_info *pending_result;

/* User interface objects */

static GtkWidget *window, *record_label, *record_num_label,
  *ann_label, *ann_num_label, *message_label,
  *time_scale_combo, *ampl_scale_combo,
  *prev_button, *next_button, *recenter_button,
  *prevcomp_button, *nextcomp_button,
  **alarm_button, *comment_entry,
  **alarm_info_label;
static GtkWidget *user_name_dialog, *user_name_entry, *password_entry;
static GtkWidget *wave_window;
static int button_update;

/**** Utilities ****/

static void G_GNUC_PRINTF(2, 3)
label_printf(GtkWidget *lbl, const char *fmt, ...)
{
  va_list ap;
  char *s;
  va_start(ap, fmt);
  s = g_markup_vprintf_escaped(fmt, ap);
  gtk_label_set_markup(GTK_LABEL(lbl), s);
  g_free(s);
  va_end(ap);
}

static void G_GNUC_PRINTF(3, 4)
show_message(GtkMessageType type, const char *primary,
	     const char *secondary, ...)
{
  GtkWindow *parent;
  GtkWidget *dlg;
  char *s;
  va_list ap;

  if (window && gtk_widget_get_visible(window))
    parent = GTK_WINDOW(window);
  else
    parent = GTK_WINDOW(user_name_dialog);

  dlg = gtk_message_dialog_new
      (parent, GTK_DIALOG_MODAL, type,
       GTK_BUTTONS_OK, "%s", primary);

  if (secondary) {
      va_start(ap, secondary);
      s = g_strdup_vprintf(secondary, ap);
      va_end(ap);
      gtk_message_dialog_format_secondary_text
	  (GTK_MESSAGE_DIALOG(dlg), "%s", s);
      g_free(s);
  }

  gtk_dialog_run(GTK_DIALOG(dlg));
  gtk_widget_destroy(dlg);
}

static void editable_set_text(GtkWidget *w, const char *text)
{
  GtkTextBuffer *buffer;

  if (!text)
    text = "";

  if (GTK_IS_TEXT_VIEW(w)) {
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
    gtk_text_buffer_set_text(buffer, text, -1);
  }
  else {
    gtk_entry_set_text(GTK_ENTRY(w), text);
  }
}

static char * editable_get_text(GtkWidget *w)
{
  GtkTextBuffer *buffer;
  GtkTextIter start, end;

  if (GTK_IS_TEXT_VIEW(w)) {
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_slice(buffer, &start, &end, TRUE);
  }
  else {
    return g_strdup(gtk_entry_get_text(GTK_ENTRY(w)));
  }
}

/**** Status values ****/

static void init_statcodes()
{
  char prop[100];
  struct response_info *si;
  int i;

  n_responses = defaults_get_integer("", "Responses.NumResponses", 0);
  responses = g_new0(struct response_info, n_responses);

  alarm_button = g_new0(GtkWidget *, n_responses);
  alarm_info_label = g_new0(GtkWidget *, n_responses);

  for (i = 0; i < n_responses; i++) {
    si = &responses[i];
    g_snprintf(prop, sizeof(prop), "Responses.Response%d", i);
    si->ui_name = g_strdup(defaults_get_string("", prop, NULL));
    g_snprintf(prop, sizeof(prop), "Responses.Response%d.Status", i);
    si->status = g_strdup(defaults_get_string("", prop, ""));
    g_snprintf(prop, sizeof(prop), "Responses.Response%d.Substatus", i);
    si->substatus = g_strdup(defaults_get_string("", prop, ""));
    g_snprintf(prop, sizeof(prop), "Responses.Response%d.CommentRequired", i);
    si->comment_required = defaults_get_boolean("", prop, 0);
    g_snprintf(prop, sizeof(prop), "Responses.Response%d.AlwaysAdjudicate", i);
    si->always_adjudicate = defaults_get_boolean("", prop, 0);
    g_snprintf(prop, sizeof(prop), "Responses.Response%d.NeverAdjudicate", i);
    si->never_adjudicate = defaults_get_boolean("", prop, 0);
  }
}

static int result_to_statcode(const struct result_info *r)
{
  int i;

  for (i = 0; i < n_responses; i++) {
    if (!g_strcmp0(r->status, responses[i].status)
        && (!g_strcmp0(r->substatus, responses[i].substatus))) {
      return i;
    }
  }

  return -1;
}

/**** Results list ****/

static const struct result_info * put_result(struct results_list *rl,
					     const char *record, WFDB_Time t,
					     const char *status,
					     const char *substatus,
					     const char *comment)
{
  int i;

  g_return_val_if_fail(record != NULL, NULL);

  for (i = 0; i < rl->n_results; i++) {
    if (t == rl->results[i].time
        && !g_strcmp0(record, rl->results[i].record)) {

      if (status) {
        g_free(rl->results[i].status);
        rl->results[i].status = g_strdup(status);
      }
      if (substatus) {
        g_free(rl->results[i].substatus);
        rl->results[i].substatus = g_strdup(substatus);
      }
      if (comment) {
        g_free(rl->results[i].comment);
        rl->results[i].comment = g_strdup(comment);
      }
      return &rl->results[i];
    }
  }

  rl->n_results++;
  rl->results = g_renew(struct result_info, rl->results, rl->n_results);
  rl->results[rl->n_results - 1].record = g_strdup(record);
  rl->results[rl->n_results - 1].time = t;
  rl->results[rl->n_results - 1].status = g_strdup(status ? status : "");
  rl->results[rl->n_results - 1].substatus = g_strdup(substatus ? substatus : "");
  rl->results[rl->n_results - 1].comment = g_strdup(comment ? comment : "");
  return &rl->results[rl->n_results - 1];
}

static void read_results_list(struct results_list *rl,
			      const char *list_url,
			      const char *post_url)
{
  WFDB_FILE *listfile;
  char buf[10000];
  char **strs;
  const char *rec, *status, *substatus, *comment;
  WFDB_Time t;
  int n;

  g_return_if_fail(rl != NULL);
  g_return_if_fail(list_url != NULL);

  if (!post_url || !post_url[0])
    post_url = list_url;

  g_free(rl->post_url);
  rl->post_url = g_strdup(post_url);

  listfile = wfdb_fopen((char*) list_url, "r");
  if (!listfile) {
    g_printerr("warning: cannot read results list '%s'\n", list_url);
    return;
  }

  while (wfdb_fgets(buf, sizeof(buf), listfile)) {
    n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
      buf[--n] = 0;

    strs = g_strsplit(buf, "\t", -1);
    if (strs && strs[0] && strs[1] && strs[2]) {
      rec = strs[0];
      t = strtol(strs[1], NULL, 10);
      status = strs[2];

      /* FIXME: remove support for old deprecated formats */

      if (!strs[3]) {
        substatus = "";
        comment = "";
      }
      else if (strs[4]) {
        substatus = strs[3];
        comment = strs[4];
      }
      else if (!g_strcmp0(strs[3], "noisy")
               || !g_strcmp0(strs[3], "ampl")
               || !g_strcmp0(strs[3], "beats")
               || !g_strcmp0(strs[3], "conv")) {
        substatus = strs[3];
        comment = "";
      }
      else {
        substatus = "";
        comment = strs[3];
      }

      put_result(rl, rec, t, status, substatus, comment);
    }
    g_strfreev(strs);
  }

  wfdb_fclose(listfile);
}

static void save_result(struct results_list *rl,
			const struct result_info *r)
{
  /*
  FILE *listfile;
  int i;

  g_return_if_fail(rl != NULL);
  g_return_if_fail(rl->filename != NULL);

  listfile = fopen(rl->filename, "w");
  if (!listfile) {
    perror(rl->filename);
    exit(1);
  }

  for (i = 0; i < rl->n_results; i++) {
    fprintf(listfile, "%s\t%ld\t%s\t%s\t%s\n",
            rl->results[i].record,
            (long int) rl->results[i].time,
            rl->results[i].status,
            rl->results[i].substatus,
            rl->results[i].comment);
  }

  fclose(listfile);
  */

  GString *postdata;
  char *response;
  GError *err = NULL;

  g_return_if_fail(r != NULL);
  g_return_if_fail(r->record != NULL);
  g_return_if_fail(rl != NULL);
  g_return_if_fail(rl->post_url != NULL);

  postdata = g_string_new(NULL);
  g_string_append(postdata, "record=");
  g_string_append_uri_escaped(postdata, r->record, NULL, FALSE);
  g_string_append_printf(postdata, "&time=%.0f", (double) r->time);

  g_string_append(postdata, "&status=");
  g_string_append_uri_escaped(postdata, r->status, NULL, FALSE);
  g_string_append(postdata, "&substatus=");
  g_string_append_uri_escaped(postdata, r->substatus, NULL, FALSE);
  g_string_append(postdata, "&comment=");
  g_string_append_uri_escaped(postdata, r->comment, NULL, FALSE);

  g_printerr("POST: %s\n", postdata->str);

  response = url_post(rl->post_url, postdata->str,
		      gtk_entry_get_text(GTK_ENTRY(user_name_entry)),
		      gtk_entry_get_text(GTK_ENTRY(password_entry)),
		      NULL, &err);
  g_string_free(postdata, TRUE);
  g_free(response);
  if (err) {
    show_message(GTK_MESSAGE_WARNING, "Unable to save annotations",
		 "%s", err->message);
    g_clear_error(&err);
  }
}

static void flush_results()
{
  if (pending_result) {
    save_result(&my_results, pending_result);
    pending_result = NULL;
  }
}

static const struct result_info * get_result(struct results_list *rl,
                                             const char *record, WFDB_Time t)
{
  static const struct result_info null_result;
  int i;

  g_return_val_if_fail(record != NULL, &null_result);

  for (i = 0; i < rl->n_results; i++) {
    if (t == rl->results[i].time
        && !g_strcmp0(record, rl->results[i].record)) {
      return &rl->results[i];
    }
  }

  return &null_result;
}

static int check_annotated(struct results_list *rl,
			   const char *record, WFDB_Time t)
{
  const struct result_info *r = get_result(rl, record, t);
  return (r && r->status != NULL && r->status[0] != 0);
}

static int n_results_for_record(struct results_list *rl,
				const char *record)
{
  int i, n = 0;
  for (i = 0; i < rl->n_results; i++) {
    if (!g_strcmp0(record, rl->results[i].record)
	&& rl->results[i].status
	&& rl->results[i].status[0])
      n++;
  }

  return n;
}

static int results_conflict(const struct result_info *r1,
                            const struct result_info *r2)
{
  int s1, s2;

  g_return_val_if_fail(r1 != NULL, 0);
  g_return_val_if_fail(r2 != NULL, 0);

  if (!r1->status || !r1->status[0] || !r2->status || !r2->status[0])
    return 0;

  if (g_strcmp0(r1->status, r2->status))
    return 1;

  s1 = result_to_statcode(r1);
  s2 = result_to_statcode(r2);

  if (s1 < 0 || s2 < 0)
    /* unknown status code! */
    return 1;

  if (responses[s1].never_adjudicate)
    return 0;
  if (responses[s2].never_adjudicate)
    return 0;

  if (responses[s1].always_adjudicate)
    return 1;
  if (responses[s2].always_adjudicate)
    return 1;

  return 0;
}

static int alarm_has_conflicts(const char *record, WFDB_Time time)
{
  int i;
  const struct result_info *r1, *r2;
  
  for (i = 0; i < n_reviewers; i++) {
    r1 = get_result(reviewer_results[i], record, time);
    if (r1->status && r1->status[0])
      break;
  }

  for (i++; i < n_reviewers; i++) {
    r2 = get_result(reviewer_results[i], record, time);
    if (results_conflict(r1, r2))
      return 1;
  }

  return 0;
}

#if 0
static int find_rec(const char *name)
{
  int i;

  for (i = 0; i < n_records; i++)
    if (!strcmp(name, records[i].name))
      return i;

  g_warning("can't find record %s", name);
  return -1;
}

static int compare_alarm_pos(const void *p1, const void *p2)
{
  const struct alarm_pos *ap1 = p1;
  const struct alarm_pos *ap2 = p2;
  if (ap1->record_index < ap2->record_index) return -1;
  if (ap1->record_index > ap2->record_index) return 1;
  if (ap1->time < ap2->time) return -1;
  if (ap1->time > ap2->time) return 1;
  return 0;
}
#endif

static void read_reviewer_results()
{
#if 0
  GDir *dir;
  GError *err = NULL;
  const char *name;
  const char prefix[] = "alarm-info.";
  int i, j, k;
  const struct result_info *r1, *r2;
  int n, conflict, total1, total2;

  dir = g_dir_open(".", 0, &err);
  if (err)
    g_error("%s", err->message);

  while ((name = g_dir_read_name(dir))) {
    if (!strncmp(name, prefix, strlen(prefix))
        && !strchr(name, '~')) {

      n_reviewers++;
      reviewer_results = g_renew(struct results_list *,
                                 reviewer_results, n_reviewers);
      reviewer_results[n_reviewers - 1] = g_slice_new0(struct results_list);

      read_results_list(reviewer_results[n_reviewers - 1], name);
    }
  }

  g_print("----- Annotation lists to compare: -----\n");
  for (i = 0; i < n_reviewers; i++)
    g_print("  %s\n", reviewer_results[i]->filename);
  g_print("\n");


  total1 = total2 = 0;
  for (i = 0; i < n_reviewers; i++) {
    for (j = 0; j < reviewer_results[i]->n_results; j++) {
      r1 = &reviewer_results[i]->results[j];
      if (!r1->status || !r1->status[0])
        continue;

      n = 1;
      conflict = 0;

      for (k = i + 1; k < n_reviewers; k++) {
        r2 = get_result(reviewer_results[k], r1->record, r1->time);
        if (!r2->status || !r2->status[0])
          continue;

        n++;
        if (results_conflict(r1, r2))
          conflict++;
      }

      if (n > 1)
        total2++;
      else
        total1++;

      if (conflict) {
        n_alarms_to_compare++;
        alarms_to_compare = g_renew(struct alarm_pos, alarms_to_compare,
                                    n_alarms_to_compare);

        alarms_to_compare[n_alarms_to_compare - 1].record_index = find_rec(r1->record);
        alarms_to_compare[n_alarms_to_compare - 1].time = r1->time;
      }
    }
  }

  qsort(alarms_to_compare, n_alarms_to_compare,
        sizeof(alarms_to_compare[0]), &compare_alarm_pos);

  g_print("Alarms annotated by only one reviewer:     %6d\n", total1);
  g_print("Alarms annotated by two or more reviewers: %6d\n", total2);
  g_print("Alarms with conflicting annotations:       %6d\n", n_alarms_to_compare);
  g_print("\n");
#endif
}

/**** Reading records/alarms ****/

static void delete_recursive(const char *dname)
{
  GDir *dir;
  const char *name;
  char *fullname;

  if (!g_file_test(dname, G_FILE_TEST_IS_SYMLINK)
      && (dir = g_dir_open(dname, 0, NULL))) {
    while ((name = g_dir_read_name(dir))) {
      if (name[0] == '.')
	continue;
      fullname = g_build_filename(dname, name, NULL);
      delete_recursive(fullname);
    }
  }
  g_remove(dname);
}

static void read_records_list()
{
  WFDB_FILE *listfile;
  char buf[256];
  int i;
  int n_alarms;

  listfile = wfdb_fopen(record_list_url, "r");
  if (!listfile) {
    show_message(GTK_MESSAGE_ERROR, "Cannot read record list", NULL);
    exit(1);
  }

  while (wfdb_fgets(buf, sizeof(buf), listfile)) {
    for (i = 0; g_ascii_isgraph(buf[i]); i++)
      ;
    if (i > 0) {
      n_records++;
      records = g_renew(struct record_info, records, n_records);
      records[n_records - 1].name = g_strndup(buf, i);

      n_alarms = strtol(&buf[i], NULL, 10);
      if (n_alarms == 0) {
	g_printerr("warning: number of alarms not listed for record %s\n",
		   records[n_records - 1].name);
	n_alarms = G_MAXINT;
      }
      records[n_records - 1].n_alarms = n_alarms;
    }
  }

  wfdb_fclose(listfile);
}

static int try_open_anns(char *recname, const char *annname)
{
  WFDB_Anninfo ai;
  char *fname;
  char *data;
  int len;
  WFDB_FILE *tmpf;
  int st;

  g_return_val_if_fail(recname != NULL, 0);
  g_return_val_if_fail(annname != NULL, 0);

  wfdbquiet();

  ai.name = g_strdup(annname);
  ai.stat = WFDB_READ;

  if (cache_enabled) {
    fname = wfdbfile(ai.name, recname);
    if (g_str_has_prefix(fname, "http://")
	|| g_str_has_prefix(fname, "https://")) {

      g_printerr("Downloading %s to cache...", fname);
      fflush(stderr);

      data = url_get(fname, gtk_entry_get_text(GTK_ENTRY(user_name_entry)),
		     gtk_entry_get_text(GTK_ENTRY(password_entry)),
		     &len, NULL);
      if (data && (tmpf = wfdb_open(ai.name, recname, WFDB_WRITE))) {
	wfdb_fwrite(data, 1, len, tmpf);
	wfdb_fclose(tmpf);
      }
      g_free(data);

      g_printerr(" done\n");
    }
  }

  st = annopen(recname, &ai, 1);
  g_free(ai.name);

  wfdbverbose();

  return (st >= 0);
}

static int at_first_alarm()
{
  return (cur_record_index <= 0 && cur_alarm_index <= 0);
}

static int at_last_alarm()
{
  return (cur_record_index >= n_records - 1
          && cur_alarm_index >= cur_record_n_alarms - 1);
}

static void select_alarm(int index)
{
  const struct result_info *r;
  int statcode = -1, i, c;
  int *sccount;

  cur_alarm = NULL;
  g_return_if_fail(index >= 0);
  g_return_if_fail(index < cur_record_n_alarms);

  flush_results();

  cur_alarm_index = index;
  cur_alarm = &cur_record_alarms[index];

  label_printf(ann_label, "%s", timstr(cur_alarm->time));
  label_printf(ann_num_label, "(%d/%d)", index + 1, cur_record_n_alarms);
  label_printf(message_label, "<big><b>%s</b></big>", cur_alarm->message);

  r = get_result(&my_results, cur_record, cur_alarm->time);

  button_update = 1;

  statcode = result_to_statcode(r);
  for (i = 0; i < n_responses; i++)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(alarm_button[i]), (statcode == i));

  editable_set_text(comment_entry, r->comment);

  if (compare_mode) {
    c = alarm_has_conflicts(cur_record, cur_alarm->time);

    sccount = g_new0(int, n_responses);

    for (i = 0; i < n_reviewers; i++) {
      r = get_result(reviewer_results[i], cur_record, cur_alarm->time);
      statcode = result_to_statcode(r);
      if (statcode >= 0)
        sccount[statcode]++;
    }

    for (i = 0; i < n_responses; i++) {
      if (sccount[i] == 0)
        label_printf(alarm_info_label[i], " ");
      else
        label_printf(alarm_info_label[i], " <b>(%d)</b>", sccount[i]);
    }

    g_free(sccount);

    for (i = 0; i < n_responses; i++)
      gtk_widget_set_sensitive(alarm_button[i],
			       (c && !responses[i].always_adjudicate));
    gtk_widget_set_sensitive(comment_entry, c);
  }

  gtk_widget_set_sensitive(prev_button, !at_first_alarm());
  gtk_widget_set_sensitive(next_button, !at_last_alarm());

  button_update = 0;
}

static void select_alarm_at_time(WFDB_Time t)
{
  int i;
  for (i = 0; i < cur_record_n_alarms; i++) {
    if (cur_record_alarms[i].time == t) {
      select_alarm(i);
      return;
    }
  }

  g_printerr("warning: can't find alarm in record %s at time %ld\n",
	     cur_record, (long) t);
}

static int is_target_annotation(const WFDB_Annotation *ann)
{
  if (target_anntyp != TARGET_ANY && target_anntyp != ann->anntyp)
    return 0;
  if (target_subtyp != TARGET_ANY && target_subtyp != ann->subtyp)
    return 0;
  if (target_num != TARGET_ANY && target_subtyp != ann->num)
    return 0;
  if (target_chan != TARGET_ANY && target_subtyp != ann->chan)
    return 0;
  if (target_aux && target_aux[0]) {
    if (!ann->aux)
      return 0;
    if (!g_str_has_prefix((const char *) ann->aux + 1, target_aux))
      return 0;
  }
  return 1;
}

static void select_record(int index)
{
  WFDB_Annotation ann;
  int i;

  g_return_if_fail(index >= 0);
  g_return_if_fail(index < n_records);

  flush_results();

  g_free(cur_record);
  cur_record = g_strdup(records[index].name);
  cur_record_index = index;

  wfdbquit();
  setwfdb(database_path);

  /*
  nsig = isigopen(cur_record, 0, 0);
  if (nsig < 0) {
    show_message(GTK_MESSAGE_ERROR, "Cannot read record",
		 "%s", wfdberror());
    exit(1);
  }
  */

  if (!try_open_anns(cur_record, database_annotator)) {
    show_message(GTK_MESSAGE_ERROR, "Cannot read annotations",
		 "%s", wfdberror());
    exit(1);
  }

  for (i = 0; i < cur_record_n_alarms; i++)
    g_free(cur_record_alarms[i].message);
  g_free(cur_record_alarms);
  cur_record_alarms = NULL;
  cur_record_n_alarms = 0;

  g_printerr("reading alarms for %s...\n", cur_record);

  while (0 <= getann(0, &ann)) {
    if (is_target_annotation(&ann)) {
      cur_record_n_alarms++;
      cur_record_alarms = g_renew(struct alarm_info, cur_record_alarms,
                                  cur_record_n_alarms);

      if (ann.aux)
	cur_record_alarms[cur_record_n_alarms - 1].message = g_strdup((char*) ann.aux + 1);
      else
	cur_record_alarms[cur_record_n_alarms - 1].message = g_strdup(anndesc(ann.anntyp));

      cur_record_alarms[cur_record_n_alarms - 1].time = ann.time;
    }
  }

  label_printf(record_label, "<small>%s</small>", cur_record);
  label_printf(record_num_label, "(%d/%d)", index + 1, n_records);

  if (cur_record_n_alarms == 0)
    g_printerr("warning: no alarms found in record %s\n",
               cur_record);
  else
    select_alarm(0);

  wave_view_force_reload();
}

static void next_alarm()
{
  g_return_if_fail(!at_last_alarm());

  if (cur_alarm_index + 1 < cur_record_n_alarms)
    select_alarm(cur_alarm_index + 1);
  else {
    do {
      select_record(cur_record_index + 1);
    } while (cur_record_n_alarms < 1
             && cur_record_index + 1 < n_records);
  }
}

static void prev_alarm()
{
  g_return_if_fail(!at_first_alarm());

  if (cur_alarm_index > 0) {
    select_alarm(cur_alarm_index - 1);
  }
  else {
    do {
      select_record(cur_record_index - 1);
    } while (cur_record_n_alarms < 1
             && cur_record_index > 0);

    select_alarm(cur_record_n_alarms - 1);
  }
}


static int at_first_to_compare()
{
  const struct alarm_pos *p = &alarms_to_compare[0];

  g_return_val_if_fail(cur_alarm != NULL, 0);

  return (n_alarms_to_compare == 0
          || cur_record_index < p->record_index
          || (cur_record_index == p->record_index && cur_alarm->time <= p->time));
}

static int at_last_to_compare()
{
  const struct alarm_pos *p = &alarms_to_compare[n_alarms_to_compare - 1];

  g_return_val_if_fail(cur_alarm != NULL, 0);

  return (n_alarms_to_compare == 0
          || cur_record_index > p->record_index
          || (cur_record_index == p->record_index && cur_alarm->time >= p->time));
}

static void next_to_compare()
{
  int i;

  g_return_if_fail(cur_alarm != NULL);

  for (i = 0; i < n_alarms_to_compare; i++) {
    if (alarms_to_compare[i].record_index > cur_record_index
        || (alarms_to_compare[i].record_index == cur_record_index
            && alarms_to_compare[i].time > cur_alarm->time)) {

      if (alarms_to_compare[i].record_index != cur_record_index)
        select_record(alarms_to_compare[i].record_index);
      select_alarm_at_time(alarms_to_compare[i].time);
      return;
    }
  }
}

static void prev_to_compare()
{
  int i;

  g_return_if_fail(cur_alarm != NULL);

  for (i = n_alarms_to_compare - 1; i >= 0; i--) {
    if (alarms_to_compare[i].record_index < cur_record_index
        || (alarms_to_compare[i].record_index == cur_record_index
            && alarms_to_compare[i].time < cur_alarm->time)) {

      if (alarms_to_compare[i].record_index != cur_record_index)
        select_record(alarms_to_compare[i].record_index);
      select_alarm_at_time(alarms_to_compare[i].time);
      return;
    }
  }
}

/**** Callbacks ****/

static void show_time_at_pos(WFDB_Time t, gdouble pos)
{
  calibrate();
  set_display_start_time(t - pos * nsamp);
}

static void recenter_clicked(G_GNUC_UNUSED GtkButton *btn, G_GNUC_UNUSED gpointer data)
{
  g_printerr("Loading record %s...\n", cur_record);
  set_record_and_annotator(cur_record, database_annotator);

  /* FIXME: until the widget is actually displayed, we don't know what
     nsamp is */
  while (gtk_events_pending())
    gtk_main_iteration();

  show_time_at_pos(cur_alarm->time, 0.75);
}

static void prev_clicked(G_GNUC_UNUSED GtkButton *btn, G_GNUC_UNUSED gpointer data)
{
  if (at_first_alarm())
    select_alarm(cur_alarm_index);
  else {
    prev_alarm();
    recenter_clicked(NULL, NULL);
  }
}

static void next_clicked(G_GNUC_UNUSED GtkButton *btn, G_GNUC_UNUSED gpointer data)
{
  if (at_last_alarm())
    select_alarm(cur_alarm_index);
  else {
    next_alarm();
    recenter_clicked(NULL, NULL);
  }
}

static void prevcomp_clicked(G_GNUC_UNUSED GtkButton *btn, G_GNUC_UNUSED gpointer data)
{
  if (at_first_to_compare())
    select_alarm(cur_alarm_index);
  else {
    prev_to_compare();
    recenter_clicked(NULL, NULL);
  }
}

static void nextcomp_clicked(G_GNUC_UNUSED GtkButton *btn, G_GNUC_UNUSED gpointer data)
{
  if (at_last_to_compare())
    select_alarm(cur_alarm_index);
  else {
    next_to_compare();
    recenter_clicked(NULL, NULL);
  }
}

static void accept_input()
{
  if (compare_mode)
    gtk_widget_activate(nextcomp_button);
  else
    gtk_widget_activate(next_button);
}

static void time_scale_changed(GtkComboBox *combo, G_GNUC_UNUSED gpointer data)
{
  int index = gtk_combo_box_get_active(combo);
  WFDB_Time tcur;

  if (index >= 0 && index != tsa_index) {
    tcur = display_start_time + 0.75 * nsamp;
    set_time_scale(index);
    show_time_at_pos(tcur, 0.75);
  }
}

static void ampl_scale_changed(GtkComboBox *combo, G_GNUC_UNUSED gpointer data)
{
  int index = gtk_combo_box_get_active(combo);

  if (index >= 0 && index != vsa_index)
    set_ampl_scale(index);
}

static void set_status(const char *status, const char *substatus)
{
  const struct result_info *r;
  g_return_if_fail(cur_alarm != NULL);
  r = put_result(&my_results, cur_record, cur_alarm->time, status, substatus, NULL);
  save_result(&my_results, r);
}

static void set_comment(const char *comment)
{
  const struct result_info *r;
  g_return_if_fail(cur_alarm != NULL);
  r = put_result(&my_results, cur_record, cur_alarm->time, NULL, NULL, comment);

  if (pending_result && pending_result != r) {
    /* this should never happen! */
    g_critical("editing comment while another result is pending");
    flush_results();
  }
  pending_result = r;
}

static void status_toggled(G_GNUC_UNUSED GtkToggleButton *btn, gpointer data)
{
  int statcode = GPOINTER_TO_INT(data);
  char *comment;

  g_return_if_fail(cur_alarm != NULL);

  if (!button_update) {
    set_status(responses[statcode].status,
	       responses[statcode].substatus);

    if (responses[statcode].comment_required) {
      comment = editable_get_text(comment_entry);
      if (!comment || !comment[0]) {
        select_alarm(cur_alarm_index);
        gtk_widget_grab_focus(comment_entry);
      }
      else {
        accept_input();
      }
      g_free(comment);
    }
    else {
      accept_input();
    }
  }
}

static void comment_changed(G_GNUC_UNUSED GtkEntry *ent, G_GNUC_UNUSED gpointer data)
{
  char *comment;
  g_return_if_fail(cur_alarm != NULL);
  if (!button_update) {
    comment = editable_get_text(comment_entry);
    set_comment(comment);
    g_free(comment);
  }
}

static void comment_activate(G_GNUC_UNUSED GtkEntry *ent, G_GNUC_UNUSED gpointer data)
{
  g_return_if_fail(cur_alarm != NULL);
  if (check_annotated(&my_results, cur_record, cur_alarm->time))
    accept_input();
}

static gboolean comment_key_press(GtkWidget *w, GdkEventKey *ev, G_GNUC_UNUSED gpointer data)
{
  if (gtk_text_view_im_context_filter_keypress(GTK_TEXT_VIEW(w), ev))
    return TRUE;

  if (ev->keyval == GDK_Return
      || ev->keyval == GDK_KP_Enter
      || ev->keyval == GDK_ISO_Enter) {
    comment_activate(NULL, NULL);
    return TRUE;
  }

  return FALSE;
}

static void login_activate(G_GNUC_UNUSED GtkEntry *ent, G_GNUC_UNUSED gpointer data)
{
  const char *name = gtk_entry_get_text(GTK_ENTRY(user_name_entry));
  const char *pw = gtk_entry_get_text(GTK_ENTRY(password_entry));

  if (!name || !name[0])
    gtk_widget_grab_focus(user_name_entry);
  else if (!pw || !pw[0])
    gtk_widget_grab_focus(password_entry);
  else
    gtk_window_activate_default(GTK_WINDOW(gtk_widget_get_toplevel(user_name_entry)));
}

static char * get_file_contents(const char *filename)
{
  WFDB_FILE *f;
  GString *data;
  char buf[1024];
  int n;

  if (!(f = wfdb_fopen((char*) filename, "r")))
    return g_strdup("");

  data = g_string_new(NULL);
  while ((n = wfdb_fread(buf, 1, sizeof(buf), f)) > 0)
    g_string_append_len(data, buf, n);
  wfdb_fclose(f);
  return g_string_free(data, FALSE);
}

static gpointer getobj(GtkBuilder *builder, const char *name)
{
  gpointer obj = gtk_builder_get_object(builder, name);
  if (!obj) {
    show_message(GTK_MESSAGE_ERROR, "Internal error",
		 "Object '%s' not found", name);
    abort();
  }
  return obj;
}

static int version_cmp(const char *v1, const char *v2)
{
  guint64 a, b;
  char *p1, *p2;

  while (*v1 || *v2) {
    if (!*v1)
      return -1;
    if (!*v2)
      return 1;

    if (g_ascii_isdigit(*v1) || g_ascii_isdigit(*v2)) {
      a = g_ascii_strtoull(v1, &p1, 10);
      b = g_ascii_strtoull(v2, &p2, 10);
      v1 = p1;
      v2 = p2;
      if (a < b)
	return -1;
      if (a > b)
	return 1;
    }
    else {
      if (*v1 < *v2)
	return -1;
      if (*v1 > *v2)
	return 1;
      v1++;
      v2++;
    }
  }

  return 0;
}

int main(int argc, char **argv)
{
  GtkBuilder *builder1, *builder2;
  GtkWidget *main_box, *options_box, *comp_buttons;
  GError *err = NULL;
  int i;
  const char *version_req, *options_fname, *msg, *geomstr, *path,
    *user_cache_dir;
  char *results_list_url, *results_post_url, *options_xml,
    *orig_working_dir, *cache_dir, *name;
  char geom[50];

  gtk_init(&argc, &argv);

  orig_working_dir = g_get_current_dir();

  builder1 = gtk_builder_new();
  if (!gtk_builder_add_from_file(builder1, "metaann.ui", &err)) {
    show_message(GTK_MESSAGE_ERROR, "Internal error",
		 "%s", err->message);
    return 1;
  }

  if (version_cmp(wfdbversion(), "10.5.24") < 0) {
    show_message(GTK_MESSAGE_ERROR, "Unsupported WFDB version",
		 "Your version of the WFDB library (%s) is too old.  "
		 "Please install the latest version in order to continue.",
		 wfdbversion());
    return 1;
  }

  /**** Get username/password to log in ****/

  user_name_dialog = getobj(builder1, "user_name_dialog");
  user_name_entry  = getobj(builder1, "user_name_entry");
  password_entry   = getobj(builder1, "password_entry");
  g_signal_connect(user_name_entry, "activate", G_CALLBACK(login_activate), NULL);
  g_signal_connect(password_entry, "activate", G_CALLBACK(login_activate), NULL);

  gtk_dialog_set_alternative_button_order(GTK_DIALOG(user_name_dialog),
					  1, 2, -1);

  gtk_entry_set_text(GTK_ENTRY(user_name_entry),
		     defaults_get_string("", "Project.UserName", ""));
  gtk_entry_set_text(GTK_ENTRY(password_entry),
		     defaults_get_string("", "Project.Password", ""));

  while (!config_log_in(GTK_WINDOW(user_name_dialog),
			gtk_entry_get_text(GTK_ENTRY(user_name_entry)),
			gtk_entry_get_text(GTK_ENTRY(password_entry)))) {

    if (g_strcmp0(gtk_entry_get_text(GTK_ENTRY(user_name_entry)), ""))
      gtk_widget_grab_focus(password_entry);
    else
      gtk_widget_grab_focus(user_name_entry);

    gtk_window_present(GTK_WINDOW(user_name_dialog));
    if (gtk_dialog_run(GTK_DIALOG(user_name_dialog)) != 1)
      /* cancel button clicked */
      return 1;
  } 

  /* save username in user.conf */
  defaults_set_string("", "Project.UserName",
		      gtk_entry_get_text(GTK_ENTRY(user_name_entry)));

  /* make dialog appear insensitive but don't hide it so user knows
     the program is still running... a real progress dialog would be
     nice but also take some work */
  gtk_widget_set_sensitive(user_name_dialog, FALSE);
  while (gtk_events_pending())
    gtk_main_iteration();

  /**** Load project configuration ****/

  version_req = defaults_get_string("", "Metaann.Version", "");
  if (version_cmp(METAANN_VERSION, version_req) < 0) {
    msg = defaults_get_string
      ("", "Metaann.VersionUpgradeMessage",
       "Your version of the metaann client is too old.  "
       "Please download the latest version in order to continue.");
    show_message(GTK_MESSAGE_ERROR, "Unsupported client version", "%s", msg);
    return 1;
  }

  path = defaults_get_string("", "Database.DBPath", "");
  database_path = g_strconcat(". ", path, NULL);
  database_calfile = g_strdup(defaults_get_string("", "Database.DBCalFile", ""));
  database_annotator = g_strdup(defaults_get_string("", "Database.Annotator", ""));
  record_list_url = g_strdup(defaults_get_string("", "Database.RecordList", ""));
  target_anntyp = defaults_get_integer("", "Database.AnnotationType", TARGET_ANY);
  target_subtyp = defaults_get_integer("", "Database.AnnotationSubtype", TARGET_ANY);
  target_num = defaults_get_integer("", "Database.AnnotationNum", TARGET_ANY);
  target_chan = defaults_get_integer("", "Database.AnnotationChan", TARGET_ANY);
  target_aux = g_strdup(defaults_get_string("", "Database.AnnotationAux", ""));

  results_list_url = g_strdup(defaults_get_string("", "Reviewer.List", ""));
  results_post_url = g_strdup(defaults_get_string("", "Reviewer.Post", ""));

  /* ugh, need to do this before calling isigopen (or other high-level
     wfdb funcs) for the first time */
  if (database_path && database_path[0])
    setwfdb(database_path);

  /* there should also be a cleaner way to do this... */
  if (database_calfile && database_calfile[0])
    g_setenv("WFDBCAL", database_calfile, TRUE);

  options_fname = defaults_get_string("", "Responses.InterfaceXML", "options.ui");
  g_printerr("loading UI from %s\n", options_fname);

  options_xml = get_file_contents(options_fname);
  builder2 = gtk_builder_new();
  if (!gtk_builder_add_from_string(builder2, options_xml, -1, &err)) {
    show_message(GTK_MESSAGE_ERROR, "Internal error",
		 "%s", err ? err->message : NULL);
    return 1;
  }
  g_free(options_xml);

  init_statcodes();

  /**** Create cache directory and chdir to it ****/

  /* (note, we have to chdir to it (a) so that wfdb_open() works, and
     (b) because it might contain spaces, especially on Windows.) */

  user_cache_dir = g_get_user_cache_dir();
  if (user_cache_dir) {
    cache_dir = g_build_filename(user_cache_dir, "metaann", NULL);
    delete_recursive(cache_dir);
    if (!g_mkdir_with_parents(cache_dir, 0700) && !g_chdir(cache_dir)) {
#ifdef G_OS_WIN32
      char *oldfile = g_build_filename(orig_working_dir, "curl-ca-bundle.crt", NULL);
      char *newfile = g_build_filename(cache_dir, "curl-ca-bundle.crt", NULL);
      FILE *f1, *f2;
      char buf[1024];
      int n;

      /* copy CA certificate bundle to cache dir so that curl will be
	 able to find it

	 gahhh this is an ugly kludge */

      f1 = g_fopen(oldfile, "rb");
      f2 = g_fopen(newfile, "wb");
      if (f1 && f2) {
	while ((n = fread(buf, 1, sizeof(buf), f1)) > 0)
	  fwrite(buf, 1, n, f2);
      }
      if (f1) fclose(f1);
      if (f2) fclose(f2);
#endif
      cache_enabled = 1;
    }
  }
  else
    cache_dir = NULL;

  /**** Create annotation toolbox window ****/

  /* defined in metaann.ui */
  window                = getobj(builder1, "window");
  main_box              = getobj(builder1, "main_box");
  record_label          = getobj(builder1, "record_label");
  record_num_label      = getobj(builder1, "record_num_label");
  ann_label             = getobj(builder1, "ann_label");
  ann_num_label         = getobj(builder1, "ann_num_label");
  message_label         = getobj(builder1, "message_label");
  time_scale_combo      = getobj(builder1, "time_scale_combo");
  ampl_scale_combo      = getobj(builder1, "ampl_scale_combo");
  prev_button           = getobj(builder1, "prev_button");
  next_button           = getobj(builder1, "next_button");
  recenter_button       = getobj(builder1, "recenter_button");
  prevcomp_button       = getobj(builder1, "prevcomp_button");
  nextcomp_button       = getobj(builder1, "nextcomp_button");

  /* defined in options.ui */
  options_box           = getobj(builder2, "options_box");
  comment_entry         = getobj(builder2, "comment_entry");

  gtk_box_pack_start(GTK_BOX(main_box), options_box, TRUE, TRUE, 0);

  for (i = 0; i < n_responses; i++) {
    name = g_strdup_printf("%s_button", responses[i].ui_name);
    alarm_button[i] = getobj(builder2, name);
    g_free(name);
    g_signal_connect(alarm_button[i], "toggled", G_CALLBACK(status_toggled), GINT_TO_POINTER(i));

    name = g_strdup_printf("%s_info_label", responses[i].ui_name);
    alarm_info_label[i] = getobj(builder2, name);
    g_free(name);
  }

  g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(time_scale_combo, "changed", G_CALLBACK(time_scale_changed), NULL);
  g_signal_connect(ampl_scale_combo, "changed", G_CALLBACK(ampl_scale_changed), NULL);
  g_signal_connect(prev_button, "clicked", G_CALLBACK(prev_clicked), NULL);
  g_signal_connect(next_button, "clicked", G_CALLBACK(next_clicked), NULL);
  g_signal_connect(recenter_button, "clicked", G_CALLBACK(recenter_clicked), NULL);
  g_signal_connect(prevcomp_button, "clicked", G_CALLBACK(prevcomp_clicked), NULL);
  g_signal_connect(nextcomp_button, "clicked", G_CALLBACK(nextcomp_clicked), NULL);

  if (GTK_IS_TEXT_VIEW(comment_entry)) {
    g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(comment_entry)),
		     "changed", G_CALLBACK(comment_changed), NULL);
    g_signal_connect(comment_entry, "key-press-event", G_CALLBACK(comment_key_press), NULL);
  }
  else {
    g_signal_connect(comment_entry, "changed", G_CALLBACK(comment_changed), NULL);
    g_signal_connect(comment_entry, "activate", G_CALLBACK(comment_activate), NULL);
  }

  gtk_widget_grab_focus(recenter_button);

  comp_buttons = getobj(builder1, "comp_buttons");
  if (compare_mode)
    gtk_widget_show(comp_buttons);
  else
    gtk_widget_hide(comp_buttons);

  g_object_unref(builder1);
  g_object_unref(builder2);
  builder1 = builder2 = NULL;

  /**** Read list of records and annotations so far ****/

  read_records_list();
  read_results_list(&my_results, results_list_url, results_post_url);
  if (compare_mode)
    read_reviewer_results();

  /* skip over fully-annotated records */
  for (i = 0; i < n_records - 1; i++)
    if (n_results_for_record(&my_results, records[i].name) != records[i].n_alarms)
      break;

  select_record(i);

  /* skip over empty records */
  while (cur_record_n_alarms < 1 && cur_record_index + 1 < n_records)
    select_record(cur_record_index + 1);

  if (compare_mode) {
    if (n_alarms_to_compare > 0) {
      select_record(alarms_to_compare[0].record_index);
      select_alarm_at_time(alarms_to_compare[0].time);
    }

    while (!at_last_to_compare()
           && (check_annotated(&my_results, cur_record, cur_alarm->time))) {
      next_to_compare();
    }
  }
  else {
    /* skip over already-annotated alarms */
    while (!at_last_alarm()
           && (check_annotated(&my_results, cur_record, cur_alarm->time))) {
      next_alarm();
    }
  }

  /**** Create wave window ****/

  g_snprintf(geom, sizeof(geom), "%dx%d-0+0",
	     950, gdk_screen_get_height(gdk_screen_get_default()));
  geomstr = defaults_get_string("", "Wave.SignalWindow.Geometry", geom);

  wave_window = create_wave_window();
  g_signal_connect(wave_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  gtk_window_parse_geometry(GTK_WINDOW(wave_window), geomstr);
  gtk_widget_show(wave_window);

  recenter_clicked(NULL, NULL);

  gtk_widget_show_all(window);
  gtk_widget_hide(user_name_dialog);

  gtk_combo_box_set_active(GTK_COMBO_BOX(time_scale_combo), tsa_index);
  gtk_combo_box_set_active(GTK_COMBO_BOX(ampl_scale_combo), vsa_index);

  /**** Main loop ****/

  gtk_main();

  flush_results();

  if (cache_enabled) {
    g_chdir(orig_working_dir);
    delete_recursive(cache_dir);
  }

  return 0;
}
