#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wfdb/wfdb.h>
#include <wfdb/wfdblib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <unistd.h>
#include <errno.h>

#include "wave.h"
#include "gtkwave.h"
#include "conf.h"
#include "url.h"

enum {
  GOOD,
  BAD_NOISY,
  BAD_AMPL,
  BAD_BEATS,
  BAD_CONV,
  BAD_OTHER,
  UNCERTAIN,
  N_STATUS_BUTTONS
};

static const struct {
  const char *ui_name;
  const char *status;
  const char *substatus;
} button_status_values[] = {
  { "good",        "good", NULL },
  { "bad_noisy",   "bad", "noisy" },
  { "bad_ampl",    "bad", "ampl" },
  { "bad_beats",   "bad", "beats" },
  { "bad_conv",    "bad", "conv" },
  { "bad_other",   "bad", NULL },
  { "uncertain",   "uncertain", NULL }};

#define TARGET_ANY 999999

static char *database_path;
static char *record_list_url;
static char *database_annotator;
static char *target_aux;
static int target_anntyp = TARGET_ANY;
static int target_subtyp = TARGET_ANY;
static int target_num = TARGET_ANY;
static int target_chan = TARGET_ANY;

static GtkWidget *window, *record_label, *record_num_label,
  *ann_label, *ann_num_label, *message_label,
  *time_scale_combo, *ampl_scale_combo,
  *prev_button, *next_button, *recenter_button,
  *prevcomp_button, *nextcomp_button,
  *alarm_button[N_STATUS_BUTTONS], *comment_entry,
  *alarm_info_label[N_STATUS_BUTTONS];

static GtkWidget *user_name_entry, *password_entry;

static GtkWidget *wave_window;

static int button_update;

struct alarm_info {
  char *message;
  WFDB_Time time;
};

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

static struct results_list my_results;

static int compare_mode;
static int n_alarms_to_compare;
static struct alarm_pos *alarms_to_compare; 

static int n_reviewers;
static struct results_list **reviewer_results;

static int cur_record_index;
static char *cur_record;

static int cur_record_n_alarms;
static struct alarm_info *cur_record_alarms;
static int cur_alarm_index;
static struct alarm_info *cur_alarm;

static const struct result_info *pending_result;

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
  GtkWidget *dlg;
  char *s;
  va_list ap;

  dlg = gtk_message_dialog_new
      (GTK_WINDOW(window), GTK_DIALOG_MODAL, type,
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
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
  }
  else {
    return g_strdup(gtk_entry_get_text(GTK_ENTRY(w)));
  }
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

  g_printerr("%s\n", postdata->str);

  response = url_post(rl->post_url, postdata->str,
		      gtk_entry_get_text(GTK_ENTRY(user_name_entry)),
		      gtk_entry_get_text(GTK_ENTRY(password_entry)),
		      &err);
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
  g_return_val_if_fail(r1 != NULL, 0);
  g_return_val_if_fail(r2 != NULL, 0);

  if (!r1->status || !r1->status[0] || !r2->status || !r2->status[0])
    return 0;

  if (g_strcmp0(r1->status, r2->status)
      /*|| g_strcmp0(r1->substatus, r2->substatus)*/
      || !g_strcmp0(r1->status, "uncertain"))
    return 1;

  if (!g_strcmp0(r1->status, "bad")
      && g_strcmp0(r1->substatus, r2->substatus)
      && (!g_strcmp0(r1->substatus, "conv") || !g_strcmp0(r2->substatus, "conv")))
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
  int st;

  g_return_val_if_fail(recname != NULL, 0);
  g_return_val_if_fail(annname != NULL, 0);

  wfdbquiet();

  ai.name = g_strdup(annname);
  ai.stat = WFDB_READ;
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

static int result_to_statcode(const struct result_info *r)
{
  int i;

  g_assert(N_STATUS_BUTTONS == G_N_ELEMENTS(button_status_values));

  for (i = 0; i < N_STATUS_BUTTONS; i++) {
    if (!g_strcmp0(r->status, button_status_values[i].status)
        && (!button_status_values[i].substatus
            || !g_strcmp0(r->substatus, button_status_values[i].substatus))) {
      return i;
    }
  }

  return -1;
}

static void select_alarm(int index)
{
  const struct result_info *r;
  int statcode = -1, i, c;
  int sccount[N_STATUS_BUTTONS];

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
  for (i = 0; i < N_STATUS_BUTTONS; i++)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(alarm_button[i]), (statcode == i));

  editable_set_text(comment_entry, r->comment);

  if (compare_mode) {
    c = alarm_has_conflicts(cur_record, cur_alarm->time);

    for (i = 0; i < N_STATUS_BUTTONS; i++)
      sccount[i] = 0;

    for (i = 0; i < n_reviewers; i++) {
      r = get_result(reviewer_results[i], cur_record, cur_alarm->time);
      statcode = result_to_statcode(r);
      if (statcode >= 0)
        sccount[statcode]++;
    }

    for (i = 0; i < N_STATUS_BUTTONS; i++) {
      if (sccount[i] == 0)
        label_printf(alarm_info_label[i], " ");
      else
        label_printf(alarm_info_label[i], " <b>(%d)</b>", sccount[i]);
    }

    for (i = 0; i < N_STATUS_BUTTONS; i++)
      gtk_widget_set_sensitive(alarm_button[i], c);
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
  int nsig;
  WFDB_Annotation ann;
  int i;

  g_return_if_fail(index >= 0);
  g_return_if_fail(index < n_records);

  flush_results();

  g_free(cur_record);
  cur_record = g_strdup(records[index].name);
  cur_record_index = index;

  wfdbquit();
  nsig = isigopen(cur_record, 0, 0);
  if (nsig < 0) {
    show_message(GTK_MESSAGE_ERROR, "Cannot read record",
		 "%s", wfdberror());
    exit(1);
  }

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
    if (statcode == BAD_OTHER) {
      comment = editable_get_text(comment_entry);
      set_status("bad", "other");
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
      set_status(button_status_values[statcode].status,
                 button_status_values[statcode].substatus);
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
  if (!obj)
    g_critical("object '%s' not found", name);
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
  GtkBuilder *builder;
  GtkWidget *user_name_dialog, *main_box, *options_box, *comp_buttons;
  GError *err = NULL;
  int i;
  char *name;
  char *results_list_url, *results_post_url, *options_xml;
  const char *version_req, *options_fname, *msg, *geomstr;
  char geom[50];

  gtk_init(&argc, &argv);

  builder = gtk_builder_new();
  if (!gtk_builder_add_from_file(builder, "metaann.ui", &err)) {
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

  user_name_dialog = getobj(builder, "user_name_dialog");
  user_name_entry  = getobj(builder, "user_name_entry");
  password_entry   = getobj(builder, "password_entry");
  g_signal_connect(user_name_entry, "activate", G_CALLBACK(login_activate), NULL);
  g_signal_connect(password_entry, "activate", G_CALLBACK(login_activate), NULL);

  gtk_dialog_set_alternative_button_order(GTK_DIALOG(user_name_dialog),
					  1, 2, -1);

  gtk_entry_set_text(GTK_ENTRY(user_name_entry),
		     defaults_get_string("", "Project.UserName", ""));
  gtk_entry_set_text(GTK_ENTRY(password_entry),
		     defaults_get_string("", "Project.Password", ""));

  while (!config_log_in(gtk_entry_get_text(GTK_ENTRY(user_name_entry)),
			gtk_entry_get_text(GTK_ENTRY(password_entry)))) {

    if (g_strcmp0(gtk_entry_get_text(GTK_ENTRY(user_name_entry)), ""))
      gtk_widget_grab_focus(password_entry);
    else
      gtk_widget_grab_focus(user_name_entry);

    if (gtk_dialog_run(GTK_DIALOG(user_name_dialog)) != 1)
      /* cancel button clicked */
      return 1;
  } 

  gtk_widget_hide(user_name_dialog);

  defaults_set_string("", "Project.UserName",
		      gtk_entry_get_text(GTK_ENTRY(user_name_entry)));

  version_req = defaults_get_string("", "Metaann.Version", "");
  if (version_cmp(METAANN_VERSION, version_req) < 0) {
    msg = defaults_get_string
      ("", "Metaann.VersionUpgradeMessage",
       "Your version of the metaann client is too old.  "
       "Please download the latest version in order to continue.");
    show_message(GTK_MESSAGE_ERROR, "Unsupported client version", "%s", msg);
    return 1;
  }

  database_path = g_strdup(defaults_get_string("", "Database.DBPath", ""));
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
  setwfdb(database_path);

  options_fname = defaults_get_string("", "Metaann.OptionsFile", "options.ui");
  options_xml = get_file_contents(options_fname);
  if (!gtk_builder_add_from_string(builder, options_xml, -1, &err)) {
    show_message(GTK_MESSAGE_ERROR, "Internal error",
		 "%s", err ? err->message : NULL);
    return 1;
  }
  g_free(options_xml);

  /* defined in metaann.ui */
  window                = getobj(builder, "window");
  main_box              = getobj(builder, "main_box");
  record_label          = getobj(builder, "record_label");
  record_num_label      = getobj(builder, "record_num_label");
  ann_label             = getobj(builder, "ann_label");
  ann_num_label         = getobj(builder, "ann_num_label");
  message_label         = getobj(builder, "message_label");
  time_scale_combo      = getobj(builder, "time_scale_combo");
  ampl_scale_combo      = getobj(builder, "ampl_scale_combo");
  prev_button           = getobj(builder, "prev_button");
  next_button           = getobj(builder, "next_button");
  recenter_button       = getobj(builder, "recenter_button");
  prevcomp_button       = getobj(builder, "prevcomp_button");
  nextcomp_button       = getobj(builder, "nextcomp_button");

  /* defined in options.ui */
  options_box           = getobj(builder, "options_box");
  comment_entry         = getobj(builder, "comment_entry");

  gtk_box_pack_start(GTK_BOX(main_box), options_box, TRUE, TRUE, 0);

  for (i = 0; i < N_STATUS_BUTTONS; i++) {
    name = g_strdup_printf("%s_alarm_button", button_status_values[i].ui_name);
    alarm_button[i] = getobj(builder, name);
    g_free(name);
    g_signal_connect(alarm_button[i], "toggled", G_CALLBACK(status_toggled), GINT_TO_POINTER(i));

    name = g_strdup_printf("%s_alarm_info_label", button_status_values[i].ui_name);
    alarm_info_label[i] = getobj(builder, name);
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

  comp_buttons = getobj(builder, "comp_buttons");
  if (compare_mode)
    gtk_widget_show(comp_buttons);
  else
    gtk_widget_hide(comp_buttons);

  g_object_unref(builder);
  builder = NULL;

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

  gtk_widget_show_all(window);

  g_snprintf(geom, sizeof(geom), "%dx%d-0+0",
	     950, gdk_screen_get_height(gdk_screen_get_default()));
  geomstr = defaults_get_string("", "Wave.SignalWindow.Geometry", geom);

  wave_window = create_wave_window();
  g_signal_connect(wave_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  gtk_window_parse_geometry(GTK_WINDOW(wave_window), geomstr);
  gtk_widget_show(wave_window);

  recenter_clicked(NULL, NULL);

  gtk_combo_box_set_active(GTK_COMBO_BOX(time_scale_combo), tsa_index);
  gtk_combo_box_set_active(GTK_COMBO_BOX(ampl_scale_combo), vsa_index);

  gtk_main();
  flush_results();
  return 0;
}

