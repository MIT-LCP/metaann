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

#define LARROW "\342\227\202"
#define RARROW "\342\226\270"
#define THINSP "\342\200\211"

void set_display_start_time(WFDB_Time t)
{
    display_start_time = MAX(0, t);
    gtk_widget_queue_draw(wave_view);
}

static void scroll_back_full(G_GNUC_UNUSED GtkButton *btn,
                             G_GNUC_UNUSED gpointer data)
{
    set_display_start_time(display_start_time - nsamp);
}

static void scroll_back_half(G_GNUC_UNUSED GtkButton *btn,
                             G_GNUC_UNUSED gpointer data)
{
    set_display_start_time(display_start_time - nsamp / 2);
}

static void scroll_forward_half(G_GNUC_UNUSED GtkButton *btn,
                                G_GNUC_UNUSED gpointer data)
{
    set_display_start_time(display_start_time + nsamp / 2);
}

static void scroll_forward_full(G_GNUC_UNUSED GtkButton *btn,
                                G_GNUC_UNUSED gpointer data)
{
    set_display_start_time(display_start_time + nsamp);
}

static void grid_mode_changed(GtkComboBox *combo,
			      G_GNUC_UNUSED gpointer data)
{
    set_grid_mode(gtk_combo_box_get_active(combo));
}

GtkWidget *create_wave_window()
{
    static const char * const grid_items[] =
	{ "None", "0.2 s", "0.5 mV", "0.2 s x 0.5 mV",
	  "0.04 s x 0.1 mV", "1 m x 0.5 mV", "1 m x 0.1 mV" };
    GtkWidget *window, *vbox, *hbox, *hbox2, *btn, *wv, *lbl, *combo;
    int i;

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    hbox = gtk_hbox_new(FALSE, 12);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 3);

    /* Grid options */

    hbox2 = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(hbox), hbox2, FALSE, FALSE, 6);
    lbl = gtk_label_new("Grid:");
    gtk_box_pack_start(GTK_BOX(hbox2), lbl, FALSE, FALSE, 0);
    combo = gtk_combo_box_new_text();
    for (i = 0; i < (int) G_N_ELEMENTS(grid_items); i++)
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), grid_items[i]);
    gtk_box_pack_start(GTK_BOX(hbox2), combo, TRUE, TRUE, 0);

    /* Navigation buttons */

    hbox2 = gtk_hbox_new(TRUE, 6);
    gtk_box_pack_start(GTK_BOX(hbox), hbox2, TRUE, TRUE, 0);
    btn = gtk_button_new_with_label(""LARROW THINSP LARROW"");
    gtk_widget_set_tooltip_text(btn, "Scroll backward by a full screen");
    g_signal_connect(btn, "clicked", G_CALLBACK(scroll_back_full), NULL);
    gtk_box_pack_start(GTK_BOX(hbox2), btn, FALSE, TRUE, 0);
    btn = gtk_button_new_with_label(LARROW);
    gtk_widget_set_tooltip_text(btn, "Scroll backward by half a screen");
    g_signal_connect(btn, "clicked", G_CALLBACK(scroll_back_half), NULL);
    gtk_box_pack_start(GTK_BOX(hbox2), btn, FALSE, TRUE, 0);
    btn = gtk_button_new_with_label(RARROW);
    gtk_widget_set_tooltip_text(btn, "Scroll forward by half a screen");
    g_signal_connect(btn, "clicked", G_CALLBACK(scroll_forward_half), NULL);
    gtk_box_pack_start(GTK_BOX(hbox2), btn, FALSE, TRUE, 0);
    btn = gtk_button_new_with_label(""RARROW THINSP RARROW"");
    gtk_widget_set_tooltip_text(btn, "Scroll forward by a full screen");
    g_signal_connect(btn, "clicked", G_CALLBACK(scroll_forward_full), NULL);
    gtk_box_pack_start(GTK_BOX(hbox2), btn, FALSE, TRUE, 0);

    wv = create_wave_view();
    gtk_widget_set_size_request(wv, 200, 200);
    gtk_box_pack_start(GTK_BOX(vbox), wv, TRUE, TRUE, 0);

    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), grid_mode);
    g_signal_connect(combo, "changed", G_CALLBACK(grid_mode_changed), NULL);

    gtk_widget_show_all(vbox);
    return window;
}
