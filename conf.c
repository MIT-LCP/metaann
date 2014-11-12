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

#include "wave.h"
#include "gtkwave.h"
#include <wfdb/wfdblib.h>
#include <stdarg.h>
#include "conf.h"
#include "url.h"

/* Conf file holding package defaults.  This file is always loaded and
   has lowest priority. */
#define PACKAGE_CONF_FILE "metaann.conf"

/* Conf file holding user settings.  This file has the highest
   priority. */
#define USER_CONF_FILE "user.conf"

/* Conf file holding default user settings.  The contents of this file
   will be copied to USER_CONF_FILE if the latter doesn't exist. */
#define DEFAULT_USER_CONF_FILE "default.conf"

static GKeyFile *package_config;
static GKeyFile *project_config;
static GKeyFile *user_config;

static G_GNUC_PRINTF(3, 4)
void show_warning(GtkWindow *parent_window, const char *primary,
		  const char *fmt, ...)
{
    va_list ap;
    char *msg;
    GtkWidget *dlg;

    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    dlg = gtk_message_dialog_new
	(parent_window, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
	 GTK_BUTTONS_OK, "%s", primary);
    gtk_message_dialog_format_secondary_text
      (GTK_MESSAGE_DIALOG(dlg), "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

/* Load the default configuration files. */
static void load_config(GtkWindow *parent_window)
{
    GError *err = NULL;

    if (!package_config) {
	package_config = g_key_file_new();
	g_key_file_load_from_file(package_config, PACKAGE_CONF_FILE, 0, &err);
	if (err) {
	    show_warning(parent_window, "Error loading configuration",
			 "%s", err->message);
	    g_clear_error(&err);
	}
    }

    if (!user_config) {
	user_config = g_key_file_new();
	g_key_file_load_from_file
	    (user_config, USER_CONF_FILE,
	     (G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS), &err);

	if (g_error_matches(err, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
	    g_clear_error(&err);
	    g_key_file_load_from_file
		(user_config, DEFAULT_USER_CONF_FILE,
		 (G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS), &err);
	}

	if (err) {
	    show_warning(parent_window, "Error loading configuration",
			 "%s", err->message);
	    g_clear_error(&err);
	}
    }

    if (!project_config) {
	project_config = g_key_file_new();
    }
}

/* Load a project configuration file. */
gboolean load_project(GtkWindow *parent_window,
		      const char *project_url)
{
    WFDB_FILE *cfile;
    GString *conf_data;
    char buf[1024];
    int n;
    GError *err = NULL;

    load_config(parent_window);

    if (!(cfile = wfdb_fopen((char*) project_url, "r"))) {
	show_warning(parent_window, "Cannot read project configuration",
		     "Unable to access '%s'", project_url);
	return FALSE;
    }

    conf_data = g_string_new(NULL);
    while ((n = wfdb_fread(buf, 1, sizeof(buf), cfile)) > 0)
	g_string_append_len(conf_data, buf, n);
    wfdb_fclose(cfile);
    g_key_file_load_from_data(project_config, conf_data->str,
			      conf_data->len, 0, &err);
    g_string_free(conf_data, TRUE);

    if (err) {
	show_warning(parent_window, "Error loading project configuration",
		     "%s", err->message);
	g_clear_error(&err);
	return FALSE;
    }
    else {
	return TRUE;
    }
}

static gboolean check_access(GtkWindow *parent_window, const char *url,
			     const char *username, const char *password)
{
    GError *err = NULL;

    if (!g_str_has_prefix(url, "https://") && !g_str_has_prefix(url, "http://"))
	return TRUE;

    if (!username || !username[0] || !password || !password[0])
	return FALSE;

    /* this is ugly.  wfdb will cache password information permanently
       (for the lifetime of the process) so we do not want to call any
       wfdb functions until we have verified the username/password is
       valid */

    if (!url_head(url, username, password, &err)) {
	show_warning(parent_window, "Unable to log in", "%s", err->message);
	g_clear_error(&err);
	return FALSE;
    }

    return TRUE;
}

static void set_wfdbpassword_from_url(const char *url,
				      const char *username,
				      const char *password)
{
    const char *p;
    char *prefix, *s;

    if (!g_str_has_prefix(url, "https://") && !g_str_has_prefix(url, "http://"))
	return;

    p = url;
    while (*p && *p != '/') p++;
    while (*p && *p == '/') p++;
    while (*p && *p != '/') p++;
    prefix = g_strndup(url, p - url);

    s = g_strconcat(prefix, " ", username, ":", password, NULL);
    g_setenv("WFDBPASSWORD", s, TRUE);
    g_free(prefix);
    g_free(s);
}

struct project_list * get_project_list(GtkWindow *parent_window,
				       const char *username,
				       const char *password)
{
    char *project_url, *list_url;
    struct project_list *list;
    WFDB_FILE *lfile;
    char buf[10000];
    char **strs;
    int n;
    int authorized;

    load_config(parent_window);

    /* check for hardcoded project path */

    project_url = g_key_file_get_string(user_config, "Project",
					"Config", NULL);
    if (!project_url)
	project_url = g_key_file_get_string(package_config, "Project",
					    "Config", NULL);
    if (project_url) {
	if (check_access(parent_window, project_url, username, password)) {
	    set_wfdbpassword_from_url(project_url, username, password);
	    list = g_new0(struct project_list, 2);
	    list[0].url = project_url;
	    list[0].reviewer = 1;
	    list[0].adjudicator = 0;
	    return list;
	}
	else {
	    g_free(project_url);
	    return NULL;
	}
    }

    /* check for dynamic project list (PNW) */

    list_url = g_key_file_get_string(user_config, "Project",
				     "ProjectList", NULL);
    if (!list_url)
	list_url = g_key_file_get_string(package_config, "Project",
					 "ProjectList", NULL);
    if (list_url) {
	if (!check_access(parent_window, list_url, username, password))
	    return NULL;

	set_wfdbpassword_from_url(list_url, username, password);

	if (!(lfile = wfdb_fopen(list_url, "r"))) {
	    show_warning(parent_window, "Cannot read list of projects",
			 "Unable to access '%s'", list_url);
	    return NULL;
	}

	n = 0;
	list = NULL;
	while (wfdb_fgets(buf, sizeof(buf), lfile)) {
	    strs = g_strsplit(buf, "\t", -1);
	    if (strs && strs[0] && strs[1] && strs[2]) {
		list = g_renew(struct project_list, list, n + 2);
		list[n].url = g_strdup(strs[0]);
		list[n].title = g_strstrip(g_strdup(strs[2]));

		authorized = atoi(strs[1]);
		list[n].reviewer = (authorized && strchr(strs[1], 'r'));
		list[n].adjudicator = (authorized && strchr(strs[1], 'a'));

		n++;
		list[n].url = NULL;
		list[n].title = NULL;
	    }
	    g_strfreev(strs);
	}
	wfdb_fclose(lfile);
	return list;
    }

    show_warning(parent_window, "No projects defined",
		 "No ProjectList is defined in metaann.conf.  "
		 "You may need to reinstall the package.");
    return NULL;
}

void free_project_list(struct project_list *list)
{
    int n;
    for (n = 0; list && list[n].url; n++) {
	g_free(list[n].url);
	g_free(list[n].title);
    }
    g_free(list);
}

static gboolean save_config()
{
    char *conf_data;
    gsize length;
    GError *err = NULL;

    g_return_val_if_fail(user_config != NULL, FALSE);

    conf_data = g_key_file_to_data(user_config, &length, &err);
    if (!err)
	g_file_set_contents(USER_CONF_FILE, conf_data, length, &err);
    g_free(conf_data);
    if (err) {
	g_printerr("warning: cannot save config: %s", err->message);
	g_clear_error(&err);
	return FALSE;
    }
    return TRUE;
}

const char * defaults_get_string(G_GNUC_UNUSED const char *name,
				 const char *classname,
				 const char *ddefault)
{
    static char *value;
    char *group, *key;

    load_config(NULL);
    g_free(value);
    value = NULL;

    /*printf("[DEF] %s = %s\n", classname, ddefault);*/

    key = strchr(classname, '.');
    g_return_val_if_fail(key != NULL, ddefault);
    group = g_strndup(classname, key - classname);
    key++;

    value = g_key_file_get_string(user_config, group, key, NULL);
    if (!value)
	value = g_key_file_get_string(project_config, group, key, NULL);
    if (!value)
	value = g_key_file_get_string(package_config, group, key, NULL);
    g_free(group);
    return value ? value : ddefault;
}

int defaults_get_integer(G_GNUC_UNUSED const char *name,
			 const char *classname,
			 int ddefault)
{
    char *group, *key;
    GError *err = NULL;
    int value;

    load_config(NULL);

    /*printf("[DEF] %s = %d\n", classname, ddefault);*/

    key = strchr(classname, '.');
    g_return_val_if_fail(key != NULL, ddefault);
    group = g_strndup(classname, key - classname);
    key++;

    value = g_key_file_get_integer(user_config, group, key, &err);
    if (err) {
	g_clear_error(&err);
	value = g_key_file_get_integer(project_config, group, key, &err);
    }
    if (err) {
	g_clear_error(&err);
	value = g_key_file_get_integer(package_config, group, key, &err);
    }
    if (err) {
	g_clear_error(&err);
	value = ddefault;
    }
    g_free(group);
    return value;
}

int defaults_get_boolean(G_GNUC_UNUSED const char *name,
			 const char *classname,
			 int ddefault)
{
    char *group, *key;
    GError *err = NULL;
    int value;

    load_config(NULL);

    /*printf("[DEF] %s = %s\n", classname, ddefault ? "true" : "false");*/

    key = strchr(classname, '.');
    g_return_val_if_fail(key != NULL, ddefault);
    group = g_strndup(classname, key - classname);
    key++;

    value = g_key_file_get_boolean(user_config, group, key, &err);
    if (err) {
	g_clear_error(&err);
	value = g_key_file_get_boolean(project_config, group, key, &err);
    }
    if (err) {
	g_clear_error(&err);
	value = g_key_file_get_boolean(package_config, group, key, &err);
    }
    if (err) {
	g_clear_error(&err);
	value = ddefault;
    }
    g_free(group);
    return value;
}

gboolean defaults_set_string(G_GNUC_UNUSED const char *name,
			     const char *classname,
			     const char *value)
{
    char *group, *key;

    load_config(NULL);

    key = strchr(classname, '.');
    g_return_val_if_fail(key != NULL, FALSE);
    group = g_strndup(classname, key - classname);
    key++;

    g_key_file_set_string(user_config, group, key, value);
    return save_config();
}
