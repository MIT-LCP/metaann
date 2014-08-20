#include <glib.h>

gboolean config_log_in(const char *username, const char *password);

gboolean defaults_set_string(const char *name,
			     const char *classname,
			     const char *value);
