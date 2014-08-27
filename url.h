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

#include <glib.h>

gboolean url_head(const char *url, const char *username,
		  const char *password, GError **err);

char * url_get(const char *url, const char *username,
	       const char *password, int *length, GError **err);

char * url_post(const char *url, const char *postdata,
		const char *username, const char *password,
		int *length, GError **err);
