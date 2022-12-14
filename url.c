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
#include <glib.h>
#include <gtk/gtk.h>		/* for gtk_*_version */
#include <wfdb/wfdb.h>
#include <curl/curl.h>
#include "url.h"

#define ERROR_DOMAIN (g_quark_from_static_string("metaann-url"))

static CURL *curl;
static char error_buf[CURL_ERROR_SIZE];

static size_t append_to_str(void *ptr, size_t size, size_t nmemb,
			    void *stream)
{
    if (stream)
	g_string_append_len(stream, ptr, size * nmemb);
    return (size * nmemb);
}

static char * request(const char *url, const char *postdata,
		      const char *username, const char *password,
		      int no_body, int *length, GError **err)
{
    GString *str;
    char *s;
    int status;

    if (length)
	*length = 0;

    if (!curl) {
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf);

	s = g_strdup_printf("metaann/%s (libwfdb/%s %s GTK+/%u.%u.%u)",
			    METAANN_VERSION, wfdbversion(), curl_version(),
			    gtk_major_version, gtk_minor_version,
			    gtk_micro_version);

	curl_easy_setopt(curl, CURLOPT_USERAGENT, s);
	g_free(s);

	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);

    if (username && password) {
	s = g_strconcat(username, ":", password, NULL);
	curl_easy_setopt(curl, CURLOPT_USERPWD, s);
	g_free(s);
    }
    else {
	curl_easy_setopt(curl, CURLOPT_USERPWD, NULL);
    }

    if (postdata) {
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
    }
    else if (no_body) {
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }
    else {
	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    str = g_string_new(NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &append_to_str);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, str);

    strcpy(error_buf, "Unknown I/O error");

    status = curl_easy_perform(curl);
    if (status != 0) {
	g_set_error(err, ERROR_DOMAIN, 1, "%s", error_buf);
	g_string_free(str, TRUE);
	return NULL;
    }

    if (length)
	*length = str->len;

    return g_string_free(str, FALSE);
}

gboolean url_head(const char *url, const char *username,
		  const char *password, GError **err)
{
    char *s;

    g_return_val_if_fail(url != NULL, FALSE);
    g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

    s = request(url, NULL, username, password, 1, NULL, err);
    g_free(s);
    return (s != NULL);
}

char * url_get(const char *url, const char *username,
	       const char *password, int *length, GError **err)
{
    g_return_val_if_fail(url != NULL, NULL);
    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    return request(url, NULL, username, password, 0, length, err);
}

char * url_post(const char *url, const char *postdata,
		const char *username, const char *password,
		int *length, GError **err)
{
    g_return_val_if_fail(url != NULL, NULL);
    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    return request(url, postdata, username, password, 0, length, err);
}
