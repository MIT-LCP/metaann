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

static G_GNUC_PRINTF(2, 3)
void show_warning(const char *primary, const char *fmt, ...)
{
    va_list ap;
    char *msg;
    GtkWidget *dlg;

    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    dlg = gtk_message_dialog_new
	(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
	 GTK_BUTTONS_OK, "%s", primary);
    gtk_message_dialog_format_secondary_text
      (GTK_MESSAGE_DIALOG(dlg), "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void load_config()
{
    GError *err = NULL;

    if (!package_config) {
	package_config = g_key_file_new();
	g_key_file_load_from_file(package_config, PACKAGE_CONF_FILE, 0, &err);
	if (err) {
	    show_warning("Error loading configuration", "%s", err->message);
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
	    show_warning("Error loading configuration", "%s", err->message);
	    g_clear_error(&err);
	}
    }

    if (!project_config) {
	project_config = g_key_file_new();
    }
}

static gboolean load_project_config(const char *project_url)
{
    WFDB_FILE *cfile;
    GString *conf_data;
    char buf[1024];
    int n;
    GError *err = NULL;

    if (!(cfile = wfdb_fopen((char*) project_url, "r"))) {
	show_warning("Cannot read project configuration",
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
	show_warning("Error loading project configuration",
		     "%s", err->message);
	g_clear_error(&err);
	return FALSE;
    }
    else {
	return TRUE;
    }
}

gboolean config_log_in(const char *username, const char *password)
{
    char *project_url, *prefix, *s, *p;
    GError *err = NULL;

    load_config();

    project_url = g_key_file_get_string(user_config, "Project",
					"Config", NULL);
    if (!project_url)
	project_url = g_key_file_get_string(package_config, "Project",
					    "Config", NULL);
    if (!project_url)
	return TRUE;

    if (!g_str_has_prefix(project_url, "https://")
	&& !g_str_has_prefix(project_url, "http://")) {
	load_project_config(project_url);
	g_free(project_url);
	return TRUE;
    }

    if (!username || !username[0] || !password || !password[0])
	return FALSE;

    /* this is ugly.  wfdb will cache password information permanently
       (for the lifetime of the process) so we do not want to call any
       wfdb functions until we have verified the username/password is
       valid */

    if (!url_head(project_url, username, password, &err)) {
	g_free(project_url);
	show_warning("Unable to log in", "%s", err->message);
	g_clear_error(&err);
	return FALSE;
    }

    p = project_url;
    while (*p && *p != '/') p++;
    while (*p && *p == '/') p++;
    while (*p && *p != '/') p++;
    prefix = g_strndup(project_url, p - project_url);

    s = g_strconcat(prefix, " ", username, ":", password, NULL);
    g_setenv("WFDBPASSWORD", s, TRUE);
    g_free(prefix);
    g_free(s);

    load_project_config(project_url);
    g_free(project_url);
    return TRUE;
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

    load_config();
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

    load_config();

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

    load_config();

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

    load_config();

    key = strchr(classname, '.');
    g_return_val_if_fail(key != NULL, FALSE);
    group = g_strndup(classname, key - classname);
    key++;

    g_key_file_set_string(user_config, group, key, value);
    return save_config();
}
