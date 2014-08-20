#include <glib.h>

gboolean url_head(const char *url, const char *username,
		  const char *password, GError **err);

char * url_get(const char *url, const char *username,
	       const char *password, GError **err);

char * url_post(const char *url, const char *postdata,
		const char *username, const char *password,
		GError **err);
