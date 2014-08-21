#if !GTK_CHECK_VERSION(2, 14, 0)
# define gtk_widget_get_window(w) ((w)->window)
#endif
#if !GTK_CHECK_VERSION(2, 18, 0)
# define gtk_widget_get_allocation(w, a) (*(a) = (w)->allocation)
#endif
#if !GTK_CHECK_VERSION(2, 20, 0)
# define gtk_widget_get_realized(w) GTK_WIDGET_REALIZED(w)
#endif
#if !GTK_CHECK_VERSION(2, 22, 0)
# define gtk_text_view_im_context_filter_keypress(t, e) \
    gtk_im_context_filter_keypress((t)->im_context, (e))
#endif
