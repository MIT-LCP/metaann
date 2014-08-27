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

#if !GTK_CHECK_VERSION(2, 14, 0)
# define gtk_widget_get_window(w) ((w)->window)
#endif
#if !GTK_CHECK_VERSION(2, 18, 0)
# define gtk_widget_get_allocation(w, a) (*(a) = (w)->allocation)
# define gtk_widget_get_visible(w) GTK_WIDGET_VISIBLE(w)
#endif
#if !GTK_CHECK_VERSION(2, 20, 0)
# define gtk_widget_get_realized(w) GTK_WIDGET_REALIZED(w)
#endif
#if !GTK_CHECK_VERSION(2, 22, 0)
# define gtk_text_view_im_context_filter_keypress(t, e) \
    gtk_im_context_filter_keypress((t)->im_context, (e))
#endif
