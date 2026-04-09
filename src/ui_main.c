#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "app_state.h"

/* ── forward declarations ─────────────────────────────────────────────────── */
gboolean telemetry_draw_split   (GtkWidget *, cairo_t *, App *);
gboolean telemetry_draw_heatmap (GtkWidget *, cairo_t *, App *);
gboolean telemetry_draw_memgauge(GtkWidget *, cairo_t *, App *);
gboolean telemetry_draw_graph   (GtkWidget *, cairo_t *, App *);
gboolean telemetry_draw_matrix  (GtkWidget *, cairo_t *, App *);
gboolean telemetry_draw_strategy(GtkWidget *, cairo_t *, App *);
void     telemetry_tick         (App *);

void on_browse          (GtkButton *, App *);
void on_browse_dest     (GtkButton *, App *);
void on_encrypt_toggle  (GtkButton *, App *);
void on_decrypt_toggle  (GtkButton *, App *);
void on_mpi_changed     (GtkRange  *, App *);
void on_run_clicked     (GtkButton *, App *);
void on_bench_clicked   (GtkButton *, App *);
void on_copy_path       (GtkButton *, gpointer);
void on_fullscreen_toggle(GtkButton *, App *);
void on_clear_log_clicked(GtkButton *, App *);

/* ═══════════════════════════════════════════════════════════════════════════
   CSS
   ═══════════════════════════════════════════════════════════════════════════ */
static const char *DASHBOARD_CSS =
    "* { color: #ffffff; }"
    "window, box, grid, scrolledwindow, viewport { background-color: #0a0e1a; }"

    "headerbar {"
    "  background: linear-gradient(180deg, #0f1628 0%, #080c18 100%);"
    "  border-bottom: 2px solid #00d4ff; min-height: 44px; }"
    "headerbar * { color: #e2e8f0; }"
    "headerbar .title {"
    "  font-family: monospace; font-weight: bold; font-size: 13px;"
    "  letter-spacing: 1px; color: #00d4ff; }"
    "headerbar button {"
    "  background: rgba(0,212,255,0.06);"
    "  border: 1px solid rgba(0,212,255,0.25); border-radius: 6px; padding: 4px 10px; }"
    "headerbar button:hover { background: rgba(0,212,255,0.15); border-color: #00d4ff; }"

    "notebook > header { background-color: #080c18; border-bottom: 1px solid #1e2d4a; }"
    "notebook > header * { color: #64748b; }"
    "notebook > header > tabs > tab {"
    "  padding: 10px 24px; background: transparent; border: none;"
    "  font-family: monospace; font-weight: bold; font-size: 10px; letter-spacing: 0.5px; }"
    "notebook > header > tabs > tab:checked { color: #00d4ff; border-bottom: 2px solid #00d4ff; }"
    "notebook > header > tabs > tab:checked * { color: #00d4ff; }"
    "notebook stack { background-color: #0a0e1a; }"

    ".left-panel { background: rgba(15,22,40,0.92); border-right: 1px solid #1e2d4a; }"

    "entry {"
    "  background: rgba(21,29,53,0.85); color: #e2e8f0; caret-color: #00d4ff;"
    "  border: 1px solid #1e2d4a; border-radius: 6px; padding: 8px 10px;"
    "  font-family: monospace; }"
    "entry:focus { border-color: #00d4ff; }"

    "button {"
    "  background: rgba(15,30,48,0.8); color: #e2e8f0;"
    "  border: 1px solid #1e2d4a; border-radius: 6px; padding: 7px 14px; font-weight: bold; }"
    "button:hover { background: rgba(0,61,92,0.7); border-color: #00d4ff; }"
    "button label, button * { color: #e2e8f0; }"

    ".tog-on { background: rgba(0,51,68,0.9); border: 1px solid #00d4ff; border-radius: 6px; }"
    ".tog-on * { color: #00d4ff; font-weight: bold; }"
    ".tog-off { background: rgba(15,22,40,0.7); border: 1px solid #1e2d4a; border-radius: 6px; }"
    ".tog-off * { color: #64748b; }"

    ".run-btn {"
    "  background: rgba(0,61,92,0.85); border: 1px solid #00d4ff;"
    "  border-radius: 8px; padding: 13px; }"
    ".run-btn * { color: #ffffff; font-size: 12px; font-weight: bold;"
    "  font-family: monospace; letter-spacing: 0.5px; }"
    ".run-btn:hover { background: rgba(0,85,128,0.9); border-color: #33deff; }"
    ".run-btn:disabled { opacity: 0.5; }"
    ".bench-btn {"
    "  background: rgba(26,10,46,0.85); border: 1px solid #7c3aed;"
    "  border-radius: 8px; padding: 9px; }"
    ".bench-btn * { color: #c4b5fd; font-size: 11px; font-family: monospace; }"
    ".bench-btn:hover { background: rgba(46,17,85,0.9); }"
    ".bench-btn:disabled { opacity: 0.5; }"

    ".draw-panel {"
    "  background: rgba(10,14,26,0.96); border: 1px solid #1e2d4a; border-radius: 10px; }"

    ".detail-card {"
    "  background: rgba(21,29,53,0.8); border: 1px solid #1e2d4a;"
    "  border-radius: 8px; padding: 12px; }"
    ".card-cyan   { border-left: 3px solid #00d4ff; }"
    ".card-violet { border-left: 3px solid #7c3aed; }"
    ".card-mint   { border-left: 3px solid #00e5a0; }"
    ".card-amber  { border-left: 3px solid #f59e0b; }"

    "progressbar { min-height: 8px; }"
    "progressbar trough { background: #1e2d4a; min-height: 8px; border-radius: 4px; }"
    "progressbar progress { min-height: 8px; border-radius: 4px; }"
    "progressbar.serial progress { background: #475569; }"
    "progressbar.cpu    progress { background: #00d4ff; }"
    "progressbar.gpu    progress { background: #7c3aed; }"

    "scale trough { background: #1e2d4a; border-radius: 4px; }"
    "scale highlight { background: #00d4ff; border-radius: 4px; }"
    "scale slider { background: #00d4ff; border-radius: 50%; min-width: 14px; min-height: 14px; }"

    ".log-terminal {"
    "  background: rgba(6,9,18,0.98); border: 1px solid #1e2d4a; border-radius: 8px;"
    "  font-family: monospace; font-size: 11px; }"
    "textview, textview text { background: transparent; color: #94a3b8; caret-color: #00d4ff; }"

    ".strategy-box {"
    "  background: rgba(13,30,46,0.85); border-left: 3px solid #00d4ff;"
    "  border-radius: 4px; padding: 8px 12px; }"

    ".speedup-pill { background: rgba(10,61,46,0.6); border-radius: 4px; padding: 2px 8px; }"
    ".speedup-pill * { color: #00e5a0; font-weight: bold; font-size: 10px; }"
    ".speedup-pill-baseline { background: rgba(30,42,56,0.6); border-radius: 4px; padding: 2px 8px; }"
    ".speedup-pill-baseline * { color: #64748b; font-size: 10px; }"

    "separator { background: #1e2d4a; min-width: 1px; min-height: 1px; }"
    "label { color: #e2e8f0; }"
    ".status-lbl { font-family: monospace; font-size: 10px; font-style: italic; }"

    "filechooser, filechooser * { background: #0a0e1a; color: #e2e8f0; }"
    "filechooser button { background: #0f1628; border: 1px solid #1e2d4a; color: #e2e8f0; border-radius: 4px; }"
    "filechooser entry  { background: #151d35; color: #e2e8f0; border: 1px solid #1e2d4a; border-radius: 4px; }"

    ".time-row-hidden  { opacity: 0.22; }"
    ".time-row-visible { opacity: 1.0;  }"
;

static void load_css(void) {
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, DASHBOARD_CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

/* ── 60-FPS animation timer ───────────────────────────────────────────────── */
static gboolean anim_timer_cb(gpointer data) {
    App *a = (App *)data;
    if (!a->is_running && a->anim_timer_id == 0) return FALSE;
    telemetry_tick(a);
    if (a->split_area)     gtk_widget_queue_draw(a->split_area);
    if (a->heatmap_area)   gtk_widget_queue_draw(a->heatmap_area);
    if (a->mem_gauge_area) gtk_widget_queue_draw(a->mem_gauge_area);
    if (a->strategy_card)  gtk_widget_queue_draw(a->strategy_card);
    if (a->graph_area)     gtk_widget_queue_draw(a->graph_area);
    if (a->matrix_area)    gtk_widget_queue_draw(a->matrix_area);
    return TRUE;
}

void ui_start_animation(App *a) {
    if (a->anim_timer_id == 0)
        a->anim_timer_id = g_timeout_add(16, anim_timer_cb, a);
}

void ui_stop_animation(App *a) {
    if (a->anim_timer_id) { g_source_remove(a->anim_timer_id); a->anim_timer_id = 0; }
}

/* ── keyboard handler ─────────────────────────────────────────────────────── */
static gboolean on_key_press(GtkWidget *w, GdkEventKey *ev, App *a) {
    if (ev->keyval == GDK_KEY_Escape || ev->keyval == GDK_KEY_F11) {
        if (a->is_fullscreen) { gtk_window_unfullscreen(GTK_WINDOW(a->window)); a->is_fullscreen = 0; }
        else                  { gtk_window_fullscreen(GTK_WINDOW(a->window));   a->is_fullscreen = 1; }
        return TRUE;
    }
    return FALSE;
}

/* ── cursor helpers ───────────────────────────────────────────────────────── */
static void set_cursor_recursive(GtkWidget *widget, GdkCursor *cursor) {
    GdkWindow *gw = gtk_widget_get_window(widget);
    if (gw) gdk_window_set_cursor(gw, cursor);
    if (GTK_IS_CONTAINER(widget)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList *l = children; l != NULL; l = l->next)
            set_cursor_recursive(GTK_WIDGET(l->data), cursor);
        g_list_free(children);
    }
}

static GdkCursor *make_default_cursor(void) {
    GdkDisplay *display = gdk_display_get_default();
    GdkCursor *cursor = gdk_cursor_new_from_name(display, "default");
    if (!cursor) cursor = gdk_cursor_new_for_display(display, GDK_LEFT_PTR);
    return cursor;
}

static void on_window_realize_set_cursor(GtkWidget *w, gpointer user_data) {
    (void)user_data;
    GdkCursor *cursor = make_default_cursor();
    set_cursor_recursive(w, cursor);
    g_object_unref(cursor);
}

static gboolean on_window_enter_reassert_cursor(GtkWidget *w, GdkEventCrossing *ev, gpointer user_data) {
    (void)ev; (void)user_data;
    GdkCursor *cursor = make_default_cursor();
    set_cursor_recursive(w, cursor);
    g_object_unref(cursor);
    return FALSE;
}

/* ── widget helpers ───────────────────────────────────────────────────────── */
static GtkWidget *make_section_lbl(const char *text) {
    GtkWidget *l = gtk_label_new(NULL);
    char m[256];
    snprintf(m, sizeof(m),
        "<span size='small' foreground='#334155' font_family='monospace'"
        " weight='bold' letter_spacing='1024'>%s</span>", text);
    gtk_label_set_markup(GTK_LABEL(l), m);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(l, 2);
    return l;
}

static GtkWidget *make_detail_card(const char *label, const char *val,
                                    const char *sub, GtkWidget **val_ref) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "detail-card");
    gtk_widget_set_hexpand(box, TRUE);

    GtkWidget *lbl = gtk_label_new(NULL);
    char lm[128];
    snprintf(lm, sizeof(lm),
        "<span size='x-small' foreground='#334155' font_family='monospace'>%s</span>", label);
    gtk_label_set_markup(GTK_LABEL(lbl), lm);
    gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);

    GtkWidget *v = gtk_label_new(NULL);
    char vm[128];
    snprintf(vm, sizeof(vm), "<b><span size='large' foreground='#ffffff'>%s</span></b>", val);
    gtk_label_set_markup(GTK_LABEL(v), vm);
    gtk_widget_set_halign(v, GTK_ALIGN_CENTER);
    if (val_ref) *val_ref = v;

    GtkWidget *s = gtk_label_new(NULL);
    char sm[128];
    snprintf(sm, sizeof(sm), "<span size='x-small' foreground='#334155'>%s</span>", sub);
    gtk_label_set_markup(GTK_LABEL(s), sm);
    gtk_widget_set_halign(s, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), v,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), s,   FALSE, FALSE, 0);
    return box;
}

static GtkWidget *draw_panel(GtkWidget *inner, int min_h) {
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "draw-panel");
    gtk_widget_set_size_request(inner, -1, min_h);
    gtk_container_add(GTK_CONTAINER(frame), inner);
    return frame;
}

/* ═══════════════════════════════════════════════════════════════════════════
   LEFT PANEL
   ═══════════════════════════════════════════════════════════════════════════ */
static GtkWidget *build_left_panel(App *a) {
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(outer, 260, -1);
    gtk_widget_set_hexpand(outer, FALSE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "left-panel");
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    gtk_box_pack_start(GTK_BOX(outer), box, TRUE, TRUE, 0);

    /* INPUT FILE */
    gtk_box_pack_start(GTK_BOX(box), make_section_lbl("INPUT FILE"), FALSE, FALSE, 0);
    GtkWidget *frow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    a->input_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->input_entry), "Select file…");
    gtk_widget_set_hexpand(a->input_entry, TRUE);
    GtkWidget *browse = gtk_button_new_with_label("Browse");
    gtk_widget_set_hexpand(browse, FALSE);
    g_signal_connect(browse, "clicked", G_CALLBACK(on_browse), a);
    gtk_box_pack_start(GTK_BOX(frow), a->input_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(frow), browse, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), frow, FALSE, FALSE, 0);

    /* DESTINATION */
    gtk_box_pack_start(GTK_BOX(box), make_section_lbl("DESTINATION (OPTIONAL)"), FALSE, FALSE, 0);
    GtkWidget *drow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    a->dest_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->dest_entry), "Default: auto-named");
    gtk_widget_set_hexpand(a->dest_entry, TRUE);
    GtkWidget *browse_dest = gtk_button_new_with_label("Save As");
    gtk_widget_set_hexpand(browse_dest, FALSE);
    g_signal_connect(browse_dest, "clicked", G_CALLBACK(on_browse_dest), a);
    gtk_box_pack_start(GTK_BOX(drow), a->dest_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(drow), browse_dest, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), drow, FALSE, FALSE, 0);

    a->output_path_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(a->output_path_lbl),
        "<span foreground='#334155' size='small'>Output → select a file first</span>");
    gtk_widget_set_halign(a->output_path_lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(a->output_path_lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(a->output_path_lbl, TRUE);
    gtk_box_pack_start(GTK_BOX(box), a->output_path_lbl, FALSE, FALSE, 0);

    a->copy_path_btn = gtk_button_new_with_label("⎘  Copy Path");
    g_object_set_data_full(G_OBJECT(a->copy_path_btn), "path", g_strdup(""), g_free);
    g_signal_connect(a->copy_path_btn, "clicked", G_CALLBACK(on_copy_path), NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(a->copy_path_btn), "bench-btn");
    gtk_widget_set_halign(a->copy_path_btn, GTK_ALIGN_START);
    gtk_widget_set_sensitive(a->copy_path_btn, FALSE);
    gtk_box_pack_start(GTK_BOX(box), a->copy_path_btn, FALSE, FALSE, 0);

    /* MODE */
    gtk_box_pack_start(GTK_BOX(box), make_section_lbl("MODE"), FALSE, FALSE, 0);
    GtkWidget *trow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    a->encrypt_btn = gtk_button_new_with_label("⬛  Encrypt");
    a->decrypt_btn = gtk_button_new_with_label("⬛  Decrypt");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-on");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-off");
    gtk_widget_set_hexpand(a->encrypt_btn, TRUE);
    gtk_widget_set_hexpand(a->decrypt_btn, TRUE);
    g_signal_connect(a->encrypt_btn, "clicked", G_CALLBACK(on_encrypt_toggle), a);
    g_signal_connect(a->decrypt_btn, "clicked", G_CALLBACK(on_decrypt_toggle), a);
    gtk_box_pack_start(GTK_BOX(trow), a->encrypt_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(trow), a->decrypt_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), trow, FALSE, FALSE, 0);

    /* MPI */
    gtk_box_pack_start(GTK_BOX(box), make_section_lbl("MPI PROCESSES"), FALSE, FALSE, 0);
    GtkWidget *srow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    a->mpi_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 8, 1);
    gtk_range_set_value(GTK_RANGE(a->mpi_scale), 4);
    gtk_scale_set_draw_value(GTK_SCALE(a->mpi_scale), FALSE);
    gtk_widget_set_hexpand(a->mpi_scale, TRUE);
    a->mpi_val_label = gtk_label_new("4");
    gtk_widget_set_size_request(a->mpi_val_label, 20, -1);
    g_signal_connect(a->mpi_scale, "value-changed", G_CALLBACK(on_mpi_changed), a);
    gtk_box_pack_start(GTK_BOX(srow), a->mpi_scale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(srow), a->mpi_val_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), srow, FALSE, FALSE, 0);

    /* STRATEGY */
    gtk_box_pack_start(GTK_BOX(box), make_section_lbl("ACTIVE STRATEGY"), FALSE, FALSE, 0);
    GtkWidget *sbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(sbox2), "strategy-box");
    a->strategy_label = gtk_label_new("Auto");
    gtk_widget_set_hexpand(a->strategy_label, TRUE);
    gtk_widget_set_halign(a->strategy_label, GTK_ALIGN_START);
    GtkWidget *info_lbl = gtk_label_new("?");
    gtk_widget_set_tooltip_text(info_lbl,
        "Strategy is auto-selected based on file size,\n"
        "CPU core count, and GPU availability.");
    gtk_box_pack_start(GTK_BOX(sbox2), a->strategy_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(sbox2), info_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), sbox2, FALSE, FALSE, 0);

    /* STATUS */
    a->status_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(a->status_lbl),
        "<span foreground='#334155' size='small' font_family='monospace'>Ready</span>");
    gtk_widget_set_halign(a->status_lbl, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(a->status_lbl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(a->status_lbl, TRUE);
    gtk_box_pack_start(GTK_BOX(box), a->status_lbl, FALSE, FALSE, 0);

    GtkWidget *fs_btn = gtk_button_new_with_label("⛶  Fullscreen (F11)");
    g_signal_connect(fs_btn, "clicked", G_CALLBACK(on_fullscreen_toggle), a);
    gtk_box_pack_start(GTK_BOX(box), fs_btn, FALSE, FALSE, 0);

    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(box), spacer, TRUE, TRUE, 0);

    a->run_btn   = gtk_button_new_with_label("▶  Run Encrypt");
    a->bench_btn = gtk_button_new_with_label("⏱  Run Benchmark");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->run_btn),   "run-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->bench_btn), "bench-btn");
    g_signal_connect(a->run_btn,   "clicked", G_CALLBACK(on_run_clicked),   a);
    g_signal_connect(a->bench_btn, "clicked", G_CALLBACK(on_bench_clicked), a);
    gtk_box_pack_end(GTK_BOX(box), a->bench_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(box), a->run_btn,   FALSE, FALSE, 0);

    return outer;
}

/* ═══════════════════════════════════════════════════════════════════════════
   TAB 1 — Real-time
   ═══════════════════════════════════════════════════════════════════════════ */
static GtkWidget *build_tab_realtime(App *a) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    a->split_area = gtk_drawing_area_new();
    g_signal_connect(a->split_area, "draw", G_CALLBACK(telemetry_draw_split), a);
    GtkWidget *r1 = draw_panel(a->split_area, 140);
    gtk_widget_set_hexpand(r1, TRUE); gtk_widget_set_vexpand(r1, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), r1, TRUE, TRUE, 0);

    a->heatmap_area = gtk_drawing_area_new();
    g_signal_connect(a->heatmap_area, "draw", G_CALLBACK(telemetry_draw_heatmap), a);
    GtkWidget *r2 = draw_panel(a->heatmap_area, 140);
    gtk_widget_set_hexpand(r2, TRUE); gtk_widget_set_vexpand(r2, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), r2, TRUE, TRUE, 0);

    a->strategy_card = gtk_drawing_area_new();
    g_signal_connect(a->strategy_card, "draw", G_CALLBACK(telemetry_draw_strategy), a);
    GtkWidget *r3 = draw_panel(a->strategy_card, 140);
    gtk_widget_set_hexpand(r3, TRUE); gtk_widget_set_vexpand(r3, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), r3, TRUE, TRUE, 0);

    a->mem_gauge_area = gtk_drawing_area_new();
    g_signal_connect(a->mem_gauge_area, "draw", G_CALLBACK(telemetry_draw_memgauge), a);
    GtkWidget *r4 = draw_panel(a->mem_gauge_area, 140);
    gtk_widget_set_hexpand(r4, TRUE); gtk_widget_set_vexpand(r4, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), r4, TRUE, TRUE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scrolled, TRUE); gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), vbox);
    return scrolled;
}

/* ── helper: one bar row ─────────────────────────────────────────────────── */
static GtkWidget *make_bar_row(const char *label_str,
                                GtkWidget **bar_out, GtkWidget **time_out,
                                GtkWidget **speedup_out,
                                const char *css_cls, const char *pill_cls) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(row, 4); gtk_widget_set_margin_bottom(row, 4);

    GtkWidget *lbl = gtk_label_new(NULL);
    char _lm[128];
    snprintf(_lm, sizeof(_lm),
        "<span foreground='#475569' font_family='monospace' size='small'>%s</span>", label_str);
    gtk_label_set_markup(GTK_LABEL(lbl), _lm);
    gtk_widget_set_size_request(lbl, 130, -1);
    gtk_widget_set_halign(lbl, GTK_ALIGN_END);

    *bar_out = gtk_progress_bar_new();
    gtk_widget_set_hexpand(*bar_out, TRUE);
    gtk_widget_set_size_request(*bar_out, -1, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(*bar_out), css_cls);

    *time_out = gtk_label_new("--");
    gtk_widget_set_size_request(*time_out, 64, -1);

    *speedup_out = gtk_label_new("--");
    gtk_widget_set_size_request(*speedup_out, 96, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(*speedup_out), pill_cls);

    gtk_box_pack_start(GTK_BOX(row), lbl,          FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), *bar_out,     TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(row), *time_out,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), *speedup_out, FALSE, FALSE, 0);
    return row;
}

/* ═══════════════════════════════════════════════════════════════════════════
   TAB 2 — Analysis & Speedup
   ═══════════════════════════════════════════════════════════════════════════ */
static GtkWidget *build_tab_analysis(App *a) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    gtk_box_pack_start(GTK_BOX(vbox), make_section_lbl("ENCRYPTION DETAILS"), FALSE, FALSE, 0);
    GtkWidget *dgrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(dgrid), 8);
    gtk_grid_set_column_homogeneous(GTK_GRID(dgrid), TRUE);
    GtkWidget *c1 = make_detail_card("File size",   "--",      "",         &a->detail_filesize);
    GtkWidget *c2 = make_detail_card("Algorithm",   "AES-CTR", "256-bit",  NULL);
    GtkWidget *c3 = make_detail_card("MPI chunks",  "--",      "",         &a->detail_chunks);
    GtkWidget *c4 = make_detail_card("OMP threads", "--",      "per proc", &a->detail_threads);
    gtk_style_context_add_class(gtk_widget_get_style_context(c1), "card-cyan");
    gtk_style_context_add_class(gtk_widget_get_style_context(c2), "card-violet");
    gtk_style_context_add_class(gtk_widget_get_style_context(c3), "card-mint");
    gtk_style_context_add_class(gtk_widget_get_style_context(c4), "card-amber");
    gtk_grid_attach(GTK_GRID(dgrid), c1, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(dgrid), c2, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(dgrid), c3, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(dgrid), c4, 3, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(vbox), dgrid, FALSE, FALSE, 0);

    GtkWidget *tc_frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(tc_frame), GTK_SHADOW_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(tc_frame), "draw-panel");
    GtkWidget *tc_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(tc_box), 12);
    gtk_container_add(GTK_CONTAINER(tc_frame), tc_box);
    gtk_box_pack_start(GTK_BOX(tc_box),
        make_section_lbl("TIME COMPARISON — SHOWS ONLY STRATEGIES THAT RAN"),
        FALSE, FALSE, 0);

    a->serial_row_widget = make_bar_row("SERIAL",
        &a->serial_bar, &a->serial_time_lbl, &a->serial_speedup_lbl,
        "serial", "speedup-pill-baseline");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(a->serial_row_widget), "time-row-hidden");
    gtk_box_pack_start(GTK_BOX(tc_box), a->serial_row_widget, FALSE, FALSE, 0);

    a->cpu_row_widget = make_bar_row("MPI + OPENMP",
        &a->cpu_bar, &a->cpu_time_lbl, &a->cpu_speedup_lbl,
        "cpu", "speedup-pill");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(a->cpu_row_widget), "time-row-hidden");
    gtk_box_pack_start(GTK_BOX(tc_box), a->cpu_row_widget, FALSE, FALSE, 0);

    a->gpu_row_widget = make_bar_row("MPI + OPENCL",
        &a->gpu_bar, &a->gpu_time_lbl, &a->gpu_speedup_lbl,
        "gpu", "speedup-pill");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(a->gpu_row_widget), "time-row-hidden");
    gtk_box_pack_start(GTK_BOX(tc_box), a->gpu_row_widget, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), tc_frame, FALSE, FALSE, 0);

    GtkWidget *rowG = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    a->graph_area = gtk_drawing_area_new();
    g_signal_connect(a->graph_area, "draw", G_CALLBACK(telemetry_draw_graph), a);
    GtkWidget *gf = draw_panel(a->graph_area, 200);
    gtk_widget_set_hexpand(gf, TRUE); gtk_widget_set_vexpand(gf, TRUE);

    a->matrix_area = gtk_drawing_area_new();
    g_signal_connect(a->matrix_area, "draw", G_CALLBACK(telemetry_draw_matrix), a);
    GtkWidget *mf = draw_panel(a->matrix_area, 200);
    gtk_widget_set_hexpand(mf, TRUE); gtk_widget_set_vexpand(mf, TRUE);

    gtk_box_pack_start(GTK_BOX(rowG), gf, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(rowG), mf, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), rowG, TRUE, TRUE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scrolled, TRUE); gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), vbox);
    return scrolled;
}

/* ═══════════════════════════════════════════════════════════════════════════
   TAB 3 — Event log
   ═══════════════════════════════════════════════════════════════════════════ */
static GtkWidget *build_tab_log(App *a) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hdr), make_section_lbl("EVENT STREAM"), FALSE, FALSE, 0);
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear");
    gtk_widget_set_halign(clear_btn, GTK_ALIGN_END);
    gtk_widget_set_hexpand(clear_btn, FALSE);
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_log_clicked), a);
    gtk_box_pack_end(GTK_BOX(hdr), clear_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hdr, FALSE, FALSE, 4);

    a->log_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(a->log_scrolled),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(a->log_scrolled, TRUE);
    gtk_widget_set_vexpand(a->log_scrolled, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(a->log_scrolled), "log-terminal");

    a->log_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(a->log_textview), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(a->log_textview), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(a->log_textview), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(a->log_textview), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(a->log_textview), 10);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(a->log_textview), TRUE);

    a->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->log_textview));
    a->tag_info  = gtk_text_buffer_create_tag(a->log_buffer, "info",  "foreground", "#94a3b8", NULL);
    a->tag_warn  = gtk_text_buffer_create_tag(a->log_buffer, "warn",  "foreground", "#f59e0b", NULL);
    a->tag_error = gtk_text_buffer_create_tag(a->log_buffer, "error", "foreground", "#ef4444",
                                               "weight", PANGO_WEIGHT_BOLD, NULL);
    a->tag_ok    = gtk_text_buffer_create_tag(a->log_buffer, "ok",    "foreground", "#00e5a0", NULL);
    a->tag_debug = gtk_text_buffer_create_tag(a->log_buffer, "debug", "foreground", "#334155", NULL);

    gtk_container_add(GTK_CONTAINER(a->log_scrolled), a->log_textview);
    gtk_box_pack_start(GTK_BOX(vbox), a->log_scrolled, TRUE, TRUE, 0);
    return vbox;
}

/* ═══════════════════════════════════════════════════════════════════════════
   PUBLIC ENTRY POINT
   ═══════════════════════════════════════════════════════════════════════════ */
void ui_main_build(App *a) {
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

    load_css();

    a->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_maximize(GTK_WINDOW(a->window));
    gtk_window_set_resizable(GTK_WINDOW(a->window), TRUE);
    g_signal_connect(a->window, "destroy",         G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(a->window, "key-press-event", G_CALLBACK(on_key_press),  a);
    g_signal_connect(a->window, "realize",         G_CALLBACK(on_window_realize_set_cursor), NULL);
    gtk_widget_add_events(a->window, GDK_ENTER_NOTIFY_MASK);
    g_signal_connect(a->window, "enter-notify-event",
                     G_CALLBACK(on_window_enter_reassert_cursor), NULL);

    /* Header bar */
    a->header_bar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(a->header_bar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(a->header_bar), "PARALLEL FILE ENCRYPTER");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(a->header_bar),
        "MPI + OpenMP + OpenCL  |  AES-CTR 256-bit");

    GtkWidget *min_btn = gtk_button_new_with_label("−");
    gtk_widget_set_tooltip_text(min_btn, "Minimise");
    g_signal_connect_swapped(min_btn, "clicked", G_CALLBACK(gtk_window_iconify), a->window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(a->header_bar), min_btn);

    GtkWidget *max_btn = gtk_button_new_with_label("□");
    gtk_widget_set_tooltip_text(max_btn, "Maximise / Restore");
    g_signal_connect_swapped(max_btn, "clicked", G_CALLBACK(gtk_window_maximize), a->window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(a->header_bar), max_btn);

    gtk_window_set_titlebar(GTK_WINDOW(a->window), a->header_bar);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(root), build_left_panel(a), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);

    a->notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(a->notebook, TRUE);
    gtk_widget_set_vexpand(a->notebook, TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(a->notebook), FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(a->notebook),
        build_tab_realtime(a), gtk_label_new("Real-time performance"));
    gtk_notebook_append_page(GTK_NOTEBOOK(a->notebook),
        build_tab_analysis(a), gtk_label_new("Analysis & Speedup"));
    gtk_notebook_append_page(GTK_NOTEBOOK(a->notebook),
        build_tab_log(a),      gtk_label_new("Event log"));
    gtk_box_pack_start(GTK_BOX(root), a->notebook, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(a->window), root);
    gtk_widget_show_all(a->window);

    /* Force cursor on every realized widget after show_all */
    {
        GdkCursor *cursor = make_default_cursor();
        set_cursor_recursive(a->window, cursor);
        g_object_unref(cursor);
    }

    ui_start_animation(a);
}

int main(int argc, char *argv[]) {
    const char *use_wayland = getenv("ENCRYPTER_USE_WAYLAND");
    if (!(use_wayland && strcmp(use_wayland, "1") == 0)) {
        g_setenv("GDK_BACKEND", "x11", TRUE);
        gdk_set_allowed_backends("x11");
    }

    if (!g_getenv("XCURSOR_THEME")) g_setenv("XCURSOR_THEME", "Adwaita", FALSE);
    if (!g_getenv("XCURSOR_SIZE"))  g_setenv("XCURSOR_SIZE",  "24", FALSE);

    gtk_init(&argc, &argv);

    App a;
    memset(&a, 0, sizeof(a));
    a.encrypting  = 1;
    a.num_procs   = 4;
    a.num_threads = 4;

    strncpy(a.strategy.mode_name, "Auto", 31);
    strncpy(a.strategy.reason,
        "Awaiting first run.\nStrategy is decided at\nruntime based on hardware.", 255);
    a.strategy.mode        = MODE_MPI_OPENMP;
    a.strategy.mpi_procs   = 4;
    a.strategy.omp_threads = 4;

    ui_main_build(&a);
    gtk_main();
    return 0;
}