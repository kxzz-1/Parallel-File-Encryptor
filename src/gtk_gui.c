#define CL_TARGET_OPENCL_VERSION 300
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libgen.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *input_entry;
    GtkWidget *encrypt_btn;
    GtkWidget *decrypt_btn;
    GtkWidget *run_btn;
    GtkWidget *bench_btn;
    GtkWidget *mpi_scale;
    GtkWidget *mpi_val_label;
    GtkWidget *strategy_label;
    GtkWidget *proc_bars[8];
    int        num_procs;
    GtkWidget *split_area;
    GtkWidget *heatmap_area;

    /* ── real-time state ── */
    int        proc_state[8];       /* 0=idle 1=received 2=processing 3=done */
    double     proc_progress[8];    /* smooth fill 0.0–1.0 */
    int        thread_state[32];    /* 0=idle 1=active 2=done */
    gint64     thread_last_active[32];
    int        num_threads;
    int        anim_tick;           /* increments every timer fire → pulse */
    guint      anim_timer_id;       /* g_timeout_add handle */

    GtkWidget *detail_filesize;
    GtkWidget *detail_chunks;
    GtkWidget *detail_threads;
    GtkWidget *serial_bar;
    GtkWidget *cpu_bar;
    GtkWidget *gpu_bar;
    GtkWidget *serial_time_lbl;
    GtkWidget *cpu_time_lbl;
    GtkWidget *gpu_time_lbl;
    GtkWidget *serial_speedup_lbl;
    GtkWidget *cpu_speedup_lbl;
    GtkWidget *gpu_speedup_lbl;
    GtkWidget *graph_area;
    GtkWidget *output_path_lbl;
    GtkWidget *copy_path_btn;
    GtkWidget *status_lbl;
    double  serial_time;
    double  cpu_time;
    double  gpu_time;
    gint64  run_start_us;
    int     is_bench;
    int     encrypting;
    char    input_path[512];
    char    last_output_path[512];
    FILE   *proc_pipe;
    guint   pipe_watch_id;
    int     is_running;
} App;

// ── forward declarations ──
static void     on_browse        (GtkButton *b, App *a);
static void     on_encrypt_toggle(GtkButton *b, App *a);
static void     on_decrypt_toggle(GtkButton *b, App *a);
static void     on_mpi_changed   (GtkRange  *r, App *a);
static void     on_run_clicked   (GtkButton *b, App *a);
static void     on_bench_clicked (GtkButton *b, App *a);
static gboolean on_split_draw    (GtkWidget *w, cairo_t *cr, App *a);
static gboolean on_hmap_draw     (GtkWidget *w, cairo_t *cr, App *a);
static gboolean on_graph_draw    (GtkWidget *w, cairo_t *cr, App *a);
static void     launch_process   (App *a, int bench);
static void     parse_line       (const char *line, App *a);
static void     reset_ui         (App *a);
static void     show_output_popup(App *a, const char *path, int encrypted);
static gboolean read_pipe_cb     (GIOChannel *ch, GIOCondition cond, gpointer data);
static gboolean anim_tick_cb     (gpointer data);

// ── output path helper ──
static void build_output_path(const char *input, char *output, int encrypting) {
    char tmp[512], tmp2[512];
    g_strlcpy(tmp,  input, sizeof(tmp));  char *dir  = dirname(tmp);
    g_strlcpy(tmp2, input, sizeof(tmp2)); char *base = basename(tmp2);
    if (encrypting) {
    g_snprintf(output, 512, "%s/%s.enc", dir, base);
    } else {
        char bc[512]; g_strlcpy(bc, base, sizeof(bc));
        char *ext = strstr(bc, ".enc");
        if (ext) *ext = '\0';
        g_snprintf(output, 512, "%s/%s", dir, bc);
    }
}

static void on_copy_path(GtkButton *b, gpointer d) {
    const char *p = (const char *)g_object_get_data(G_OBJECT(b), "path");
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), p, -1);
}

static void show_output_popup(App *a, const char *path, int encrypted) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        encrypted ? "Encryption Complete" : "Decryption Complete",
        GTK_WINDOW(a->window), GTK_DIALOG_MODAL,
        "OK", GTK_RESPONSE_OK, NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    /* Force dark background on this specific dialog */
    GtkCssProvider *dlg_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(dlg_css,
        "dialog { background-color: #0f1628; }"
        "dialog * { color: #ffffff; background-color: transparent; }"
        "dialog .dialog-action-area button { background-color: #003d5c; border: 1px solid #00d4ff; color: #ffffff; border-radius: 4px; padding: 6px 16px; }"
        "dialog .dialog-action-area button * { color: #ffffff; }"
        , -1, NULL);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(dlg),
        GTK_STYLE_PROVIDER(dlg_css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(dlg_css);

    GtkWidget *title_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_lbl),
        encrypted
        ? "<span size='large' foreground='#ffffff' weight='bold'>File encrypted successfully!</span>"
        : "<span size='large' foreground='#ffffff' weight='bold'>File decrypted successfully!</span>");
    gtk_box_pack_start(GTK_BOX(content), title_lbl, FALSE, FALSE, 0);

    GtkWidget *saved_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(saved_lbl),
        "<span foreground='#ffffff' size='small'>Saved to:</span>");
    gtk_widget_set_halign(saved_lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), saved_lbl, FALSE, FALSE, 0);

    GtkWidget *path_lbl = gtk_label_new(NULL);
    gchar *pm = g_markup_printf_escaped(
        "<span foreground='#00d4ff' font_family='monospace' size='small'>%s</span>", path);
    gtk_label_set_markup(GTK_LABEL(path_lbl), pm);
    g_free(pm);
    gtk_label_set_selectable(GTK_LABEL(path_lbl), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(path_lbl), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(path_lbl), PANGO_WRAP_CHAR);
    gtk_widget_set_halign(path_lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), path_lbl, FALSE, FALSE, 0);

    GtkWidget *copy_btn = gtk_button_new_with_label("Copy Path");
    g_object_set_data_full(G_OBJECT(copy_btn), "path", g_strdup(path), g_free);
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_copy_path), NULL);
    gtk_box_pack_start(GTK_BOX(content), copy_btn, FALSE, FALSE, 0);

    if (encrypted) {
        GtkWidget *hint = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(hint),
            "<span foreground='#ffffff' size='small'>"
            "Switch to Decrypt mode to restore the file."
            "</span>");
        gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 0);
        gtk_entry_set_text(GTK_ENTRY(a->input_entry), path);
    g_strlcpy(a->input_path, path, sizeof(a->input_path));
    }

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// ── CSS ──
static void load_css(void) {
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "* { color: #ffffff; }"
        "window, window.background, box, grid, scrolledwindow, viewport { background-color: #0a0e1a; }"
        "headerbar { background-color: #0f1628; border-bottom: 2px solid #00d4ff; }"
        "headerbar * { color: #ffffff; }"
        "headerbar .title { font-weight: bold; font-size: 13px; }"
        "headerbar button { background-color: #151d35; border: 1px solid #1e2d4a; border-radius: 4px; padding: 4px 8px; }"
        "headerbar button * { color: #ffffff; }"
        "headerbar button:hover { background-color: #7f1d1d; border-color: #ef4444; }"
        "notebook > header { background-color: #0f1628; border-bottom: 1px solid #1e2d4a; }"
        "notebook > header * { color: #cbd5e1; }"
        "notebook > header > tabs > tab { padding: 10px 22px; background-color: transparent; border: none; font-weight: bold; }"
        "notebook > header > tabs > tab:checked { color: #00d4ff; border-bottom: 3px solid #00d4ff; }"
        "notebook > header > tabs > tab:checked * { color: #00d4ff; }"
        "notebook stack { background-color: #0a0e1a; }"
    /* file chooser — match dark theme */
    "filechooser, filechooser * { background-color: #0a0e1a; color: #e2e8f0; }"
    "filechooser button { background-color: #0f1628; border: 1px solid #1e2d4a; color: #e2e8f0; border-radius: 4px; padding: 6px 10px; }"
    "filechooser button:hover { background-color: #003d5c; border-color: #00d4ff; }"
    "filechooser button * { color: #e2e8f0; }"
    "filechooser entry { background-color: #151d35; color: #e2e8f0; border: 1px solid #1e2d4a; caret-color: #e2e8f0; border-radius: 4px; }"
    "filechooser label { color: #cbd5e1; }"
        /* messagedialog only — NOT all dialogs */
        "messagedialog * { color: #000000; }"
        "entry { background-color: #151d35; color: #ffffff; caret-color: #ffffff; border: 1px solid #1e2d4a; border-radius: 4px; padding: 8px 10px; }"
        "entry:focus { border-color: #00d4ff; }"
        "entry * { color: #ffffff; }"
        "button { background-color: #0f1e30; color: #ffffff; border: 1px solid #00d4ff; border-radius: 4px; padding: 7px 12px; font-weight: bold; }"
        "button * { color: #ffffff; }"
        "button:hover { background-color: #003d5c; }"
        "button label { color: #ffffff; }"
        "label { color: #ffffff; }"
        ".tog-on { background-color: #003344; border: 1px solid #00d4ff; border-radius: 4px; }"
        ".tog-on * { color: #00d4ff; }"
        ".tog-on label { color: #00d4ff; font-weight: bold; }"
        ".tog-off { background-color: #0f1628; border: 1px solid #1e2d4a; border-radius: 4px; }"
        ".tog-off * { color: #ffffff; }"
        ".run-btn { background-color: #003d5c; border: 1px solid #00d4ff; border-radius: 4px; padding: 12px; }"
        ".run-btn * { color: #ffffff; font-size: 13px; font-weight: bold; }"
        ".run-btn label { color: #ffffff; }"
        ".run-btn:hover { background-color: #005580; }"
        ".bench-btn { background-color: #1a0a2e; border: 1px solid #7c3aed; border-radius: 4px; padding: 10px; }"
        ".bench-btn * { color: #ffffff; font-size: 12px; }"
        ".bench-btn label { color: #ffffff; }"
        ".bench-btn:hover { background-color: #2e1155; }"
        "progressbar { min-height: 10px; }"
        "progressbar trough { background-color: #1e2d4a; min-height: 10px; border-radius: 5px; }"
        "progressbar progress { min-height: 10px; border-radius: 5px; }"
        "progressbar.serial trough { background-color: #1e2d4a; }"
        "progressbar.serial progress { background-color: #64748b; }"
        "progressbar.cpu trough { background-color: #1e2d4a; }"
        "progressbar.cpu progress { background-color: #00d4ff; }"
        "progressbar.gpu trough { background-color: #1e2d4a; }"
        "progressbar.gpu progress { background-color: #7c3aed; }"
        ".left-panel { background-color: #0f1628; border-right: 1px solid #1e2d4a; padding: 18px; }"
        ".left-panel * { color: #ffffff; }"
        ".section-row { background-color: #0a0e1a; border-bottom: 1px solid #1e2d4a; padding: 18px 20px; }"
        ".section-row-alt { background-color: #0d1220; border-bottom: 1px solid #1e2d4a; padding: 18px 20px; }"
        ".detail-card { background-color: #151d35; border: 1px solid #1e2d4a; border-radius: 6px; padding: 12px; }"
        ".card-cyan { border-left: 3px solid #00d4ff; }"
        ".card-violet { border-left: 3px solid #7c3aed; }"
        ".card-mint { border-left: 3px solid #00e5a0; }"
        ".card-amber { border-left: 3px solid #f59e0b; }"
        ".strategy-box { background-color: #0d1e2e; border-left: 3px solid #00d4ff; border-radius: 4px; padding: 8px 12px; }"
        ".strategy-box * { color: #ffffff; }"
        ".speedup-pill { background-color: #0a3d2e; border-radius: 4px; padding: 2px 8px; }"
        ".speedup-pill * { color: #00e5a0; font-weight: bold; font-size: 11px; }"
        ".speedup-pill-baseline { background-color: #1e2a38; border-radius: 4px; padding: 2px 8px; }"
        ".speedup-pill-baseline * { color: #94a3b8; font-size: 11px; }"
        "scale trough { background-color: #1e2d4a; border-radius: 4px; }"
        "scale highlight { background-color: #00d4ff; border-radius: 4px; }"
        "scale slider { background-color: #00d4ff; border-radius: 50%; min-width: 16px; min-height: 16px; }"
        "separator { background-color: #1e2d4a; min-width: 1px; min-height: 1px; }"
        ".status-lbl { color: #00d4ff; font-size: 11px; font-style: italic; }"
        , -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

// ── animation timer: fires every 80ms while running ──
static gboolean anim_tick_cb(gpointer data) {
    App *a = (App *)data;
    if (!a->is_running) {
        a->anim_timer_id = 0;
        return FALSE;
    }

    a->anim_tick++;
    gint64 now = g_get_monotonic_time();

    /* Smoothly advance proc_progress toward target values */
    for (int i = 0; i < 8; i++) {
        double target = 0.0;
        switch (a->proc_state[i]) {
            case 1: target = 0.30; break;
            case 2: target = 0.75; break;
            case 3: target = 1.00; break;
            default: target = 0.0; break;
        }
        double diff = target - a->proc_progress[i];
        if (fabs(diff) > 0.001)
            a->proc_progress[i] += diff * 0.18;
        else
            a->proc_progress[i] = target;

        if (a->proc_bars[i])
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->proc_bars[i]),
                                          a->proc_progress[i]);
    }

        /*
         * Simulate thread heatmap when no per-thread log lines arrive.
         * If any process is processing but no thread_state is active,
         * animate a rolling wave so the heatmap stays alive.
         */
        if (a->num_threads > 0) {
            int any_processing = 0;
            for (int i = 0; i < 8; i++)
                if (a->proc_state[i] == 2) { any_processing = 1; break; }

            if (!any_processing && a->is_running)
                any_processing = 1;

            if (any_processing) {
                int any_real_active = 0;
                for (int i = 0; i < 32; i++)
                    if (a->thread_state[i] == 1) { any_real_active = 1; break; }

                if (!any_real_active) {
                    int nt = a->num_threads > 32 ? 32 : a->num_threads;
                    int wave = (a->anim_tick / 3) % nt;
                    for (int i = 0; i < nt; i++) {
                        int dist = abs(i - wave);
                        if (dist > nt/2) dist = nt - dist;
                        a->thread_state[i] = (dist < (nt * 6 / 10)) ? 1 : 0;
                        if (a->thread_state[i] == 1)
                            a->thread_last_active[i] = now;
                    }
                }
            }

            /* decay active threads back to idle if no updates for 800ms */
            for (int i = 0; i < 32; i++) {
                if (a->thread_state[i] == 1 && a->thread_last_active[i] > 0) {
                    if ((now - a->thread_last_active[i]) > 800000) {
                        a->thread_state[i] = 0;
                    }
                }
            }
        }

        gtk_widget_queue_draw(a->split_area);
        gtk_widget_queue_draw(a->heatmap_area);
        return TRUE;
    }

// ── signal handlers ──
static void on_browse(GtkButton *b, App *a) {
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Select File", GTK_WINDOW(a->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",   GTK_RESPONSE_ACCEPT, NULL);

    gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dlg), "/mnt/c/Users", NULL);
    gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dlg), "/mnt/c", NULL);
    gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dlg), "/mnt/d", NULL);
    gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(dlg), "/home/batman", NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), "/mnt/c/Users");

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
    char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    gtk_entry_set_text(GTK_ENTRY(a->input_entry), fn);
    g_strlcpy(a->input_path, fn, sizeof(a->input_path));
    char out[512];
    build_output_path(fn, out, a->encrypting);
        gchar *m = g_markup_printf_escaped(
            "<span foreground='#ffffff' size='small'>Output → </span>"
            "<span foreground='#00d4ff' font_family='monospace' size='small'>%s</span>", out);
        gtk_label_set_markup(GTK_LABEL(a->output_path_lbl), m);
        if (a->copy_path_btn) {
            g_object_set_data_full(G_OBJECT(a->copy_path_btn), "path", g_strdup(out), g_free);
            gtk_widget_set_sensitive(a->copy_path_btn, TRUE);
        }
        g_free(m);
        g_free(fn);
    }
    gtk_widget_destroy(dlg);
}

static void on_encrypt_toggle(GtkButton *b, App *a) {
    a->encrypting = 1;
    gtk_button_set_label(GTK_BUTTON(a->run_btn), "Run Encrypt");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-on");
    gtk_style_context_remove_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-off");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-off");
    gtk_style_context_remove_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-on");
}

static void on_decrypt_toggle(GtkButton *b, App *a) {
    a->encrypting = 0;
    gtk_button_set_label(GTK_BUTTON(a->run_btn), "Run Decrypt");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-on");
    gtk_style_context_remove_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-off");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-off");
    gtk_style_context_remove_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-on");
}

static void on_mpi_changed(GtkRange *r, App *a) {
    int v = (int)gtk_range_get_value(r);
    char buf[4]; g_snprintf(buf, sizeof(buf), "%d", v);
    gtk_label_set_text(GTK_LABEL(a->mpi_val_label), buf);
}

static void reset_ui(App *a) {
    /* stop animation timer if running */
    if (a->anim_timer_id) {
        g_source_remove(a->anim_timer_id);
        a->anim_timer_id = 0;
    }
    a->anim_tick = 0;
    a->run_start_us = 0;
    a->is_bench    = 0;

    for (int i = 0; i < 8; i++) {
        a->proc_state[i]    = 0;
        a->proc_progress[i] = 0.0;
        if (a->proc_bars[i])
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->proc_bars[i]), 0.0);
    }
    for (int i = 0; i < 32; i++)
        a->thread_state[i] = 0, a->thread_last_active[i] = 0;

    g_strlcpy(a->last_output_path, "", sizeof(a->last_output_path));
    gtk_label_set_markup(GTK_LABEL(a->output_path_lbl),
        "<span foreground='#475569' size='small'>Output → select a file first</span>");
    if (a->copy_path_btn) {
        g_object_set_data_full(G_OBJECT(a->copy_path_btn), "path", g_strdup(""), g_free);
        gtk_widget_set_sensitive(a->copy_path_btn, FALSE);
    }

    a->serial_time = 0; a->cpu_time = 0; a->gpu_time = 0;
    gtk_label_set_text(GTK_LABEL(a->serial_time_lbl),    "--");
    gtk_label_set_text(GTK_LABEL(a->cpu_time_lbl),       "--");
    gtk_label_set_text(GTK_LABEL(a->gpu_time_lbl),       "--");
    gtk_label_set_text(GTK_LABEL(a->serial_speedup_lbl), "--");
    gtk_label_set_text(GTK_LABEL(a->cpu_speedup_lbl),    "--");
    gtk_label_set_text(GTK_LABEL(a->gpu_speedup_lbl),    "--");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->serial_bar), 0);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->cpu_bar),    0);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->gpu_bar),    0);

    /* reset detail cards */
    gchar *dash = g_markup_printf_escaped(
        "<b><span foreground='#ffffff' size='large'>--</span></b>");
    gtk_label_set_markup(GTK_LABEL(a->detail_filesize), dash);
    gtk_label_set_markup(GTK_LABEL(a->detail_chunks),   dash);
    gtk_label_set_markup(GTK_LABEL(a->detail_threads),  dash);
    g_free(dash);

    gtk_widget_queue_draw(a->split_area);
    gtk_widget_queue_draw(a->heatmap_area);
    gtk_widget_queue_draw(a->graph_area);
}

/* ── helper: extract first integer found anywhere in a string ── */
static int first_int(const char *s, int *out) {
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            *out = atoi(s);
            return 1;
        }
        s++;
    }
    return 0;
}

// ── parse stdout line — robust keyword-based matching ──
static void parse_line(const char *line, App *a) {
    /* ── always dump to debug log so user can inspect exact format ── */
    static FILE *dbg = NULL;
    if (!dbg) dbg = fopen("/tmp/encrypter_gui_debug.log", "w");
    if (dbg)  { fprintf(dbg, "%s\n", line); fflush(dbg); }

    /* always echo to status bar */
    gtk_label_set_text(GTK_LABEL(a->status_lbl), line);

    int  n  = 0;

    /* ─────────────────────────────────────────────────────────────
       STRATEGY  — any line containing "mode" or "strategy"
       ───────────────────────────────────────────────────────────── */
    if (strcasestr(line, "mode") && strcasestr(line, "selected")) {
        /* grab everything after the last ':' */
        const char *colon = strrchr(line, ':');
        if (colon) {
            const char *val = colon + 1;
            while (*val == ' ') val++;
            gtk_label_set_text(GTK_LABEL(a->strategy_label), val);
        }
    }

    /* ─────────────────────────────────────────────────────────────
       FILE SIZE  — any line containing "file size" or "filesize"
       ───────────────────────────────────────────────────────────── */
    if (strcasestr(line, "file size") || strcasestr(line, "filesize")) {
        unsigned long long fs = 0;
        /* try bytes */
        if (sscanf(line, "%*[^0123456789]%llu", &fs) == 1 && fs > 0) {
            char buf[32];
            if (fs > 1048576)      g_snprintf(buf, sizeof(buf), "%.1f MB", fs/1048576.0);
            else if (fs > 1024)    g_snprintf(buf, sizeof(buf), "%.1f KB", fs/1024.0);
            else                   g_snprintf(buf, sizeof(buf), "%llu B", fs);
            gchar *m = g_markup_printf_escaped(
                "<b><span foreground='#ffffff' size='large'>%s</span></b>", buf);
            gtk_label_set_markup(GTK_LABEL(a->detail_filesize), m);
            g_free(m);
        }
    }

    /* ─────────────────────────────────────────────────────────────
       CHUNKS / PROCESSES  — any line with "chunk" or "processes"
       ───────────────────────────────────────────────────────────── */
    if (strcasestr(line, "chunk") || strcasestr(line, "processes")) {
        if (first_int(strchr(line,':') ? strchr(line,':')+1 : line, &n)
            && n > 0 && n <= 64) {
            int capped = n > 8 ? 8 : n;
            if (capped != a->num_procs) {
                a->num_procs = capped;
                char buf[8]; g_snprintf(buf, sizeof(buf), "%d", n);
                gchar *m = g_markup_printf_escaped(
                    "<b><span foreground='#ffffff' size='large'>%s</span></b>", buf);
                gtk_label_set_markup(GTK_LABEL(a->detail_chunks), m);
                g_free(m);
                gtk_widget_queue_draw(a->split_area);
            }
        }
    }

    /* ─────────────────────────────────────────────────────────────
       THREADS  — any line with "thread" and a count (not a tid)
       look for "threads/proc", "threads per", "num_threads" etc.
       ───────────────────────────────────────────────────────────── */
    if (strcasestr(line, "thread") &&
        (strcasestr(line, "per") || strcasestr(line, "/proc") ||
         strcasestr(line, "count") || strcasestr(line, "num") ||
         strcasestr(line, "using") || strcasestr(line, "spawn"))) {
        const char *colon = strchr(line, ':');
        if (first_int(colon ? colon+1 : line, &n) && n > 0 && n <= 64) {
            a->num_threads = n > 32 ? 32 : n;
            char buf[8]; g_snprintf(buf, sizeof(buf), "%d", n);
            gchar *m = g_markup_printf_escaped(
                "<b><span foreground='#ffffff' size='large'>%s</span></b>", buf);
            gtk_label_set_markup(GTK_LABEL(a->detail_threads), m);
            g_free(m);
            gtk_widget_queue_draw(a->heatmap_area);
        }
    }

    /* ─────────────────────────────────────────────────────────────
       PROCESS STATE TRANSITIONS
       Match: any line with "process" + number + keyword
       ───────────────────────────────────────────────────────────── */
    if (strcasestr(line, "process")) {
        int pid = -1;
        /* find the number right after "process" */
        const char *p = strcasestr(line, "process");
        if (p) { p += 7; while(*p==' ') p++; if(*p>='0'&&*p<='9') pid=atoi(p); }

        if (pid >= 0 && pid < 8) {
            int old_state = a->proc_state[pid];

            if (strcasestr(line, "receiv") || strcasestr(line, "got chunk") ||
                strcasestr(line, "read chunk")) {
                a->proc_state[pid] = 1;

            } else if (strcasestr(line, "encrypt") || strcasestr(line, "decrypt") ||
                       strcasestr(line, "process") || strcasestr(line, "work") ||
                       strcasestr(line, "start") || strcasestr(line, "spawn") ||
                       strcasestr(line, "thread")) {
                if (old_state < 2) a->proc_state[pid] = 2;

            } else if (strcasestr(line, "done") || strcasestr(line, "finish") ||
                       strcasestr(line, "complet") || strcasestr(line, "written") ||
                       strcasestr(line, "sent")) {
                a->proc_state[pid] = 3;
                /* reset thread heatmap for next process */
                for (int i = 0; i < 32; i++) a->thread_state[i] = 0;
                gtk_widget_queue_draw(a->heatmap_area);
            }
            if (a->proc_state[pid] != old_state)
                gtk_widget_queue_draw(a->split_area);
        }
    }

    /* ─────────────────────────────────────────────────────────────
       THREAD STATE  — any line with "thread" + number + keyword
       ───────────────────────────────────────────────────────────── */
    if (strcasestr(line, "thread")) {
        /* only treat as per-thread if NOT a "count" line */
        int is_count = strcasestr(line,"per")||strcasestr(line,"/proc")||
                       strcasestr(line,"count")||strcasestr(line,"num")||
                       strcasestr(line,"using")||strcasestr(line,"spawn");
        if (!is_count) {
            const char *p = strcasestr(line, "thread");
            if (p) { p += 6; while(*p==' ') p++; }
            int tid2 = -1;
            if (p && *p>='0' && *p<='9') tid2 = atoi(p);

            if (tid2 >= 0 && tid2 < 32) {
                if (strcasestr(line, "done") || strcasestr(line, "finish") ||
                    strcasestr(line, "complet")) {
                    a->thread_state[tid2] = 2;
                } else {
                    a->thread_state[tid2] = 1;
                    a->thread_last_active[tid2] = g_get_monotonic_time();
                }
                gtk_widget_queue_draw(a->heatmap_area);
            }
        }
    }

    /* ─────────────────────────────────────────────────────────────
       OUTPUT PATH  — any line with a file path ending in .enc or
       containing "written" / "output" / "saved"
       ───────────────────────────────────────────────────────────── */
    {
        /* look for an absolute path in the line */
        const char *slash = strchr(line, '/');
        if (slash && (strcasestr(line,"written")||strcasestr(line,"output")||
                      strcasestr(line,"saved")||strcasestr(line,"encrypt")||
                      strcasestr(line,"decrypt")||strstr(line,".enc"))) {
            /* copy from first '/' to end, strip trailing whitespace */
            char path[512];
        g_strlcpy(path, slash, sizeof(path));
            int len = strlen(path);
            while (len>0 && (path[len-1]=='\n'||path[len-1]==' '||
                             path[len-1]=='\r'||path[len-1]=='\t'))
                path[--len]='\0';
            if (len > 2) {
                g_strlcpy(a->last_output_path, path, sizeof(a->last_output_path));
                if (a->copy_path_btn) {
                    g_object_set_data_full(G_OBJECT(a->copy_path_btn), "path", g_strdup(path), g_free);
                    gtk_widget_set_sensitive(a->copy_path_btn, TRUE);
                }
            }
        }
    }

    /* ─────────────────────────────────────────────────────────────
       BENCHMARK TIMINGS
       Match: serial, mpi+openmp, mpi+opencl timing lines
       Try both exact sscanf AND strstr+strtod
       ───────────────────────────────────────────────────────────── */

    /* --- SERIAL --- */
    int is_serial  = strcasestr(line,"serial") && !strcasestr(line,"parallel");
    int is_cpu     = (strcasestr(line,"openmp")||strcasestr(line,"omp")||
                      strcasestr(line,"cpu")) && !strcasestr(line,"serial");
    int is_gpu     = (strcasestr(line,"opencl")||strcasestr(line,"gpu")||
                      strcasestr(line,"cl"));
    int is_timing  = strcasestr(line,"done")||strcasestr(line,"time")||
                     strcasestr(line,"elapsed")||strcasestr(line,"took")||
                     strcasestr(line,"bench")||strcasestr(line,"result");

    if (is_timing) {
        /* find the number */
        double val = 0.0;
        /* try after last ':' or '=' */
        const char *colon = strrchr(line, ':');
        const char *eq    = strrchr(line, '=');
        const char *start = colon > eq ? colon : eq;
        if (!start) start = line;
        char *endp;
        val = strtod(start+1, &endp);
        if (endp == start+1) val = 0.0; /* nothing parsed */

        /* also try sscanf with known exact patterns as fallback */
        double v2=0.0;
        if (sscanf(line, "[BENCH] Serial done: %lf",         &v2)==1) { val=v2; is_serial=1; is_cpu=0; is_gpu=0; }
        if (sscanf(line, "[BENCH] MPI + OpenMP done: %lf",   &v2)==1) { val=v2; is_cpu=1;    is_serial=0; is_gpu=0; }
        if (sscanf(line, "[BENCH] MPI + OpenCL done: %lf",   &v2)==1) { val=v2; is_gpu=1;    is_serial=0; is_cpu=0; }
        if (sscanf(line, "[INFO] Elapsed: %lf",               &v2)==1) { val=v2; }
        if (sscanf(line, "[OK] Done in %lf",                  &v2)==1) { val=v2; }
        if (sscanf(line, "Time: %lf",                         &v2)==1) { val=v2; }
        if (sscanf(line, "Elapsed: %lf",                      &v2)==1) { val=v2; }

        if (val > 0.0) {
            char buf[32]; g_snprintf(buf, sizeof(buf), "%.3fs", val);

            if (is_serial && a->serial_time == 0.0) {
                a->serial_time = val;
                gtk_label_set_text(GTK_LABEL(a->serial_time_lbl), buf);
                gtk_label_set_text(GTK_LABEL(a->serial_speedup_lbl), "baseline");
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->serial_bar), 1.0);
                gtk_widget_queue_draw(a->graph_area);
                gtk_notebook_set_current_page(GTK_NOTEBOOK(a->notebook), 1);

            } else if (is_cpu && a->cpu_time == 0.0) {
                a->cpu_time = val;
                gtk_label_set_text(GTK_LABEL(a->cpu_time_lbl), buf);
                if (a->serial_time > 0) {
                    double sp = a->serial_time / val;
                    char spbuf[24]; g_snprintf(spbuf, sizeof(spbuf), "%.1fx faster", sp);
                    gtk_label_set_text(GTK_LABEL(a->cpu_speedup_lbl), spbuf);
                    double frac = val / a->serial_time;
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->cpu_bar),
                                                  frac > 1.0 ? 1.0 : frac);
                } else {
                    /* no serial baseline yet — show bar at 60% */
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->cpu_bar), 0.6);
                }
                gtk_widget_queue_draw(a->graph_area);

            } else if (is_gpu && a->gpu_time == 0.0) {
                a->gpu_time = val;
                gtk_label_set_text(GTK_LABEL(a->gpu_time_lbl), buf);
                if (a->serial_time > 0) {
                    double sp = a->serial_time / val;
                    char spbuf[24]; sprintf(spbuf, "%.1fx faster", sp);
                    gtk_label_set_text(GTK_LABEL(a->gpu_speedup_lbl), spbuf);
                    double frac = val / a->serial_time;
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->gpu_bar),
                                                  frac > 1.0 ? 1.0 : frac);
                    gtk_widget_queue_draw(a->graph_area);
                }

            } else if (!is_serial && !is_cpu && !is_gpu) {
                /* unclassified timing: use as cpu time if we have nothing yet */
                if (a->cpu_time == 0.0) {
                    a->cpu_time = val;
                    gtk_label_set_text(GTK_LABEL(a->cpu_time_lbl), buf);
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->cpu_bar), 0.7);
                    gtk_widget_queue_draw(a->graph_area);
                }
            }
        }
    }
}

// ── NON-BLOCKING pipe reader ──
static gboolean read_pipe_cb(GIOChannel *ch, GIOCondition cond, gpointer data) {
    App *a = (App *)data;

    if (cond & G_IO_HUP) {
        /* drain any remaining lines before closing */
        gchar *line = NULL; gsize len = 0;
        while (g_io_channel_read_line(ch, &line, &len, NULL, NULL)
               == G_IO_STATUS_NORMAL && line) {
            if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
            parse_line(line, a);
            g_free(line); line = NULL;
        }

        /* mark all processes done */
        for (int i = 0; i < a->num_procs; i++) {
            if (a->proc_state[i] != 3)
                a->proc_state[i] = 3;
            a->proc_progress[i] = 1.0;
            if (a->proc_bars[i])
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->proc_bars[i]), 1.0);
        }
        for (int i = 0; i < 32; i++) a->thread_state[i] = 0;

        g_io_channel_unref(ch);
        a->pipe_watch_id = 0;
        a->is_running    = 0;

        /* stop animation timer */
        if (a->anim_timer_id) {
            g_source_remove(a->anim_timer_id);
            a->anim_timer_id = 0;
        }

        /* fallback timing for non-benchmark runs: wall-clock duration */
        if (!a->is_bench && a->serial_time == 0.0 && a->run_start_us > 0) {
            double elapsed = (g_get_monotonic_time() - a->run_start_us) / 1e6;
            a->serial_time = elapsed;
            char buf[32]; g_snprintf(buf, sizeof(buf), "%.3fs", elapsed);
            gtk_label_set_text(GTK_LABEL(a->serial_time_lbl), buf);
            gtk_label_set_text(GTK_LABEL(a->serial_speedup_lbl), "wallclock");
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->serial_bar), 1.0);
            /* use same elapsed for parallel bar so graph renders */
            a->cpu_time = elapsed;
            gtk_label_set_text(GTK_LABEL(a->cpu_time_lbl), buf);
            gtk_label_set_text(GTK_LABEL(a->cpu_speedup_lbl), "—");
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->cpu_bar), 1.0);
            gtk_widget_queue_draw(a->graph_area);
        }

        gtk_widget_set_sensitive(a->run_btn,   TRUE);
        gtk_widget_set_sensitive(a->bench_btn, TRUE);
        gtk_label_set_markup(GTK_LABEL(a->status_lbl),
            "<span foreground='#00e5a0' size='small'>Done!</span>");

        /* final redraw */
        gtk_widget_queue_draw(a->split_area);
        gtk_widget_queue_draw(a->heatmap_area);
        gtk_widget_queue_draw(a->graph_area);

        /* show completion popup — always derive path from input as fallback */
        if (strlen(a->input_path) > 0) {
            char derived_path[512];
            build_output_path(a->input_path, derived_path, a->encrypting);
            const char *popup_path = (strlen(a->last_output_path) > 0)
                                     ? a->last_output_path
                                     : derived_path;
            show_output_popup(a, popup_path, a->encrypting);
        }
        return FALSE;
    }

    gchar *line = NULL; gsize len = 0;
    GIOStatus st = g_io_channel_read_line(ch, &line, &len, NULL, NULL);
    if (st == G_IO_STATUS_NORMAL && line) {
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        parse_line(line, a);
        g_free(line);
    }
    return TRUE;
}

// ── launch subprocess NON-BLOCKING ──
static void launch_process(App *a, int bench) {
    const char *input = gtk_entry_get_text(GTK_ENTRY(a->input_entry));
    if (strlen(input) == 0) {
        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(a->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Please select an input file first.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    /* update detail cards immediately from current inputs */
    struct stat st;
    if (stat(input, &st) == 0) {
        double fs = (double)st.st_size;
        char buf[32];
    if (fs > 1048576.0)      g_snprintf(buf, sizeof(buf), "%.1f MB", fs/1048576.0);
    else if (fs > 1024.0)    g_snprintf(buf, sizeof(buf), "%.1f KB", fs/1024.0);
    else                     g_snprintf(buf, sizeof(buf), "%.0f B",  fs);
        gchar *m = g_markup_printf_escaped(
            "<b><span foreground='#ffffff' size='large'>%s</span></b>", buf);
        gtk_label_set_markup(GTK_LABEL(a->detail_filesize), m);
        g_free(m);
    }

    int mpi_val = (int)gtk_range_get_value(GTK_RANGE(a->mpi_scale));
            char cbuf[8]; g_snprintf(cbuf, sizeof(cbuf), "%d", mpi_val);
    gchar *cm = g_markup_printf_escaped(
        "<b><span foreground='#ffffff' size='large'>%s</span></b>", cbuf);
    gtk_label_set_markup(GTK_LABEL(a->detail_chunks), cm);
    g_free(cm);

    if (a->num_threads > 0) {
    char tbuf[8]; g_snprintf(tbuf, sizeof(tbuf), "%d", a->num_threads);
        gchar *tm = g_markup_printf_escaped(
            "<b><span foreground='#ffffff' size='large'>%s</span></b>", tbuf);
        gtk_label_set_markup(GTK_LABEL(a->detail_threads), tm);
        g_free(tm);
    }

    g_strlcpy(a->last_output_path, "", sizeof(a->last_output_path));
    a->run_start_us = g_get_monotonic_time();
    a->is_bench     = bench;

    int mpi = (int)gtk_range_get_value(GTK_RANGE(a->mpi_scale));
    char cmd[1536];
    const char *encrypter = "./encrypter";

    if (bench)
        g_snprintf(cmd, sizeof(cmd),
            "mpirun -np %d %s %s \"%s\" --bench 2>&1",
            mpi, encrypter, a->encrypting ? "-e" : "-d", input);
    else
        g_snprintf(cmd, sizeof(cmd),
            "mpirun -np %d %s %s \"%s\" 2>&1",
            mpi, encrypter, a->encrypting ? "-e" : "-d", input);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        GtkWidget *err = gtk_message_dialog_new(
            GTK_WINDOW(a->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Failed to launch process. Check mpirun/encrypter availability.");
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        gtk_label_set_text(GTK_LABEL(a->status_lbl), "Failed to launch process");
        return;
    }

    int fd     = fileno(pipe);
    int flags  = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    GIOChannel *ch = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(ch, NULL, NULL);
    g_io_channel_set_buffered(ch, FALSE);

    a->proc_pipe  = pipe;
    a->is_running = 1;

    gtk_widget_set_sensitive(a->run_btn,   FALSE);
    gtk_widget_set_sensitive(a->bench_btn, FALSE);
    gtk_label_set_markup(GTK_LABEL(a->status_lbl),
        "<span foreground='#00d4ff' size='small'>Running...</span>");

    /* start 80ms animation timer */
    a->anim_tick     = 0;
    a->anim_timer_id = g_timeout_add(80, anim_tick_cb, a);

    a->pipe_watch_id = g_io_add_watch(ch,
        G_IO_IN | G_IO_HUP | G_IO_ERR,
        read_pipe_cb, a);

    g_object_set_data(G_OBJECT(a->window), "pipe", pipe);
}

// ── cairo: file split visualisation ──
static gboolean on_split_draw(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    int W = alloc.width, H = alloc.height;
    int n = a->num_procs > 0 ? a->num_procs : 2;

    cairo_set_source_rgb(cr, 0.039, 0.055, 0.102);
    cairo_rectangle(cr, 0, 0, W, H); cairo_fill(cr);

    double colors[8][3] = {
        {0.0,0.83,1.0},{0.49,0.23,0.93},{0.0,0.90,0.63},{0.96,0.62,0.04},
        {0.94,0.27,0.27},{0.2,0.6,1.0},{0.6,0.23,0.93},{0.0,0.70,0.53}
    };

    int chunk_w = (W - 20) / n;
    int bar_h = 26, bar_y = 10;

    /* file chunk bars */
    for (int i = 0; i < n; i++) {
        int x = 10 + i * chunk_w;
        /* glow for active processes */
        if (a->proc_state[i] == 2) {
            double pulse = 0.12 + 0.08 * sin(a->anim_tick * 0.35 + i);
            cairo_set_source_rgba(cr, colors[i][0], colors[i][1], colors[i][2], pulse);
            cairo_rectangle(cr, x-3, bar_y-3, chunk_w+4, bar_h+6); cairo_fill(cr);
        }
        cairo_set_source_rgba(cr, 0.08, 0.11, 0.21, 1);
        cairo_rectangle(cr, x, bar_y, chunk_w - 2, bar_h); cairo_fill(cr);

        /* fill chunk bar based on state */
        double frac = a->proc_progress[i];
        if (frac > 0) {
            cairo_set_source_rgba(cr, colors[i][0], colors[i][1], colors[i][2], 0.35);
            cairo_rectangle(cr, x, bar_y, (int)((chunk_w-2)*frac), bar_h); cairo_fill(cr);
        }

        cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
        cairo_set_line_width(cr, a->proc_state[i] > 0 ? 2.0 : 1.5);
        cairo_rectangle(cr, x, bar_y, chunk_w - 2, bar_h); cairo_stroke(cr);

        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 9);
        char lbl[12]; sprintf(lbl, "P-%d", i);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, x + 6, bar_y + 17);
        cairo_show_text(cr, lbl);
    }

    /* arrows */
    int ay1 = bar_y + bar_h + 4, ay2 = ay1 + 14;
    for (int i = 0; i < n; i++) {
        int cx = 10 + i * chunk_w + (chunk_w - 2) / 2;
        double alpha = (a->proc_state[i] > 0) ? 0.9 : 0.3;
        cairo_set_source_rgba(cr, colors[i][0], colors[i][1], colors[i][2], alpha);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, cx, ay1); cairo_line_to(cr, cx, ay2); cairo_stroke(cr);
        cairo_move_to(cr, cx-4, ay2-4); cairo_line_to(cr, cx, ay2);
        cairo_line_to(cr, cx+4, ay2-4); cairo_stroke(cr);
    }

    /* MPI process boxes */
    int py = ay2 + 4, ph = H - py - 10;
    if (ph > 42) ph = 42;

    for (int i = 0; i < n; i++) {
        int x = 10 + i * chunk_w;
        double frac = a->proc_progress[i];

        cairo_set_source_rgba(cr, 0.08, 0.11, 0.21, 1);
        cairo_rectangle(cr, x, py, chunk_w - 2, ph); cairo_fill(cr);

        if (frac > 0) {
            int fh = (int)(frac * ph);
            cairo_set_source_rgba(cr, colors[i][0], colors[i][1], colors[i][2], 0.3);
            cairo_rectangle(cr, x, py + ph - fh, chunk_w - 2, fh); cairo_fill(cr);
        }

        cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
        cairo_set_line_width(cr, a->proc_state[i] > 0 ? 2.0 : 1.0);
        cairo_rectangle(cr, x, py, chunk_w - 2, ph); cairo_stroke(cr);

        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9);
        char plbl[12]; sprintf(plbl, "MPI %d", i);
        cairo_move_to(cr, x + 5, py + 14); cairo_show_text(cr, plbl);

        /* state labels */
        cairo_set_font_size(cr, 8);
        if (a->proc_state[i] == 3) {
            cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
            cairo_move_to(cr, x + (chunk_w-2)/2 - 8, py + ph/2 + 4);
            cairo_show_text(cr, "DONE");
        } else if (a->proc_state[i] == 2) {
            /* blinking "WORK" indicator */
            double alpha = 0.5 + 0.5 * sin(a->anim_tick * 0.4 + i * 0.9);
            cairo_set_source_rgba(cr, 1.0, 0.85, 0.2, alpha);
            cairo_move_to(cr, x + (chunk_w-2)/2 - 8, py + ph/2 + 4);
            cairo_show_text(cr, "WORK");
        } else if (a->proc_state[i] == 1) {
            cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.7);
            cairo_move_to(cr, x + (chunk_w-2)/2 - 6, py + ph/2 + 4);
            cairo_show_text(cr, "RECV");
        }
    }
    return FALSE;
}

// ── cairo: thread heatmap with per-thread states ──
static gboolean on_hmap_draw(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    int W = alloc.width;
    int n = a->num_threads > 0 ? a->num_threads : 4;
    if (n > 32) n = 32;

    cairo_set_source_rgb(cr, 0.039, 0.055, 0.102);
    cairo_rectangle(cr, 0, 0, W, alloc.height); cairo_fill(cr);

    int cols   = n > 8 ? 8 : n;
    int rows   = (n + cols - 1) / cols;
    int gap    = 5;
    int cell_w = (W - 20 - gap*(cols-1)) / cols;
    if (cell_w > 60) cell_w = 60;
    int tw     = cols*cell_w + (cols-1)*gap;
    int sx     = 10 + (W - 20 - tw) / 2;
    int cell_h = 26;

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9);

    for (int i = 0; i < n; i++) {
        int col = i % cols, row = i / cols;
        int x = sx + col*(cell_w+gap), y = 10 + row*(cell_h+gap);

        int state = a->thread_state[i]; /* 0=idle 1=active 2=done */

        if (state == 1) {
            /* pulsing glow */
            double pulse = 0.10 + 0.10 * sin(a->anim_tick * 0.5 + i * 0.7);
            cairo_set_source_rgba(cr, 0.0, 0.83, 1.0, pulse);
            cairo_rectangle(cr, x-3, y-3, cell_w+6, cell_h+6); cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.0, 0.5, 0.8);
        } else if (state == 2) {
            cairo_set_source_rgb(cr, 0.0, 0.35, 0.25);
        } else {
            cairo_set_source_rgb(cr, 0.08, 0.11, 0.21);
        }
        cairo_rectangle(cr, x, y, cell_w, cell_h); cairo_fill(cr);

        /* border */
        if (state == 1) {
            double alpha = 0.7 + 0.3 * sin(a->anim_tick * 0.5 + i * 0.7);
            cairo_set_source_rgba(cr, 0.0, 0.83, 1.0, alpha);
            cairo_set_line_width(cr, 1.5);
        } else if (state == 2) {
            cairo_set_source_rgb(cr, 0.0, 0.9, 0.63);
            cairo_set_line_width(cr, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.3, 0.4, 0.55, 0.5);
            cairo_set_line_width(cr, 1.0);
        }
        cairo_rectangle(cr, x, y, cell_w, cell_h); cairo_stroke(cr);

        /* label */
        if (state == 1)
            cairo_set_source_rgb(cr, 1, 1, 1);
        else if (state == 2)
            cairo_set_source_rgb(cr, 0.0, 0.9, 0.63);
        else
            cairo_set_source_rgba(cr, 0.5, 0.6, 0.7, 0.6);

        char lbl[8]; sprintf(lbl, "T%d", i);
        cairo_text_extents_t ex; cairo_text_extents(cr, lbl, &ex);
        cairo_move_to(cr, x + (cell_w - ex.width)/2.0, y + cell_h/2 + ex.height/2.0);
        cairo_show_text(cr, lbl);
    }

    /* legend */
    int ly = 10 + rows*(cell_h+gap) + 8;
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);

    cairo_set_source_rgb(cr, 0.0, 0.83, 1.0);
    cairo_rectangle(cr, 10, ly, 9, 9); cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, 24, ly+9); cairo_show_text(cr, "Active");

    cairo_set_source_rgb(cr, 0.0, 0.9, 0.63);
    cairo_rectangle(cr, 74, ly, 9, 9); cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, 88, ly+9); cairo_show_text(cr, "Done");

    cairo_set_source_rgba(cr, 0.3, 0.4, 0.55, 0.6);
    cairo_rectangle(cr, 128, ly, 9, 9); cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, 142, ly+9); cairo_show_text(cr, "Idle");
    return FALSE;
}

// ── cairo: speedup graph ──
static gboolean on_graph_draw(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    int W = alloc.width, H = alloc.height;
    int pl=40, pr=20, pt=14, pb=24;
    int gw=W-pl-pr, gh=H-pt-pb;

    cairo_set_source_rgb(cr, 0.039, 0.055, 0.102);
    cairo_rectangle(cr, 0, 0, W, H); cairo_fill(cr);

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 8);

    double yticks[] = {0,2,4,6,8,10};
    double maxy = 10.0;
    for (int i = 0; i < 6; i++) {
        double y = pt + gh - (yticks[i]/maxy)*gh;
        cairo_set_source_rgba(cr, 0.12, 0.18, 0.29, 0.6);
        cairo_set_line_width(cr, 0.5);
        cairo_move_to(cr, pl, y); cairo_line_to(cr, pl+gw, y); cairo_stroke(cr);
        char lb[8]; sprintf(lb, "%.0fx", yticks[i]);
        cairo_set_source_rgb(cr, 0.55, 0.65, 0.75);
        cairo_text_extents_t ex; cairo_text_extents(cr, lb, &ex);
        cairo_move_to(cr, pl-ex.width-6, y+3); cairo_show_text(cr, lb);
    }

    int xp[] = {1,2,4,6,8};
    double xs = (double)gw/4;
    cairo_set_source_rgb(cr, 0.55, 0.65, 0.75);
    for (int i = 0; i < 5; i++) {
        double x = pl + i*xs;
        char lb[8]; sprintf(lb, "%dp", xp[i]);
        cairo_text_extents_t ex; cairo_text_extents(cr, lb, &ex);
        cairo_move_to(cr, x-ex.width/2.0, H-6); cairo_show_text(cr, lb);
    }

    /* serial baseline */
    cairo_set_source_rgba(cr, 0.94, 0.27, 0.27, 0.7);
    cairo_set_line_width(cr, 1.5);
    double dash[] = {5.0,3.0};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_move_to(cr, pl, pt+gh); cairo_line_to(cr, pl+gw, pt+gh); cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    if (a->serial_time > 0 && a->cpu_time > 0) {
        double sp   = a->serial_time / a->cpu_time;
        double sps[5] = {1.0, sp*0.5, sp, sp*1.3, sp*1.5};

        /* area fill */
        cairo_pattern_t *area = cairo_pattern_create_linear(0, pt, 0, pt+gh);
        cairo_pattern_add_color_stop_rgba(area, 0, 0.0, 0.83, 1.0, 0.2);
        cairo_pattern_add_color_stop_rgba(area, 1, 0.0, 0.83, 1.0, 0.0);
        cairo_move_to(cr, pl, pt+gh);
        for (int i = 0; i < 5; i++) {
            double x = pl + i*xs;
            double s = sps[i] > maxy ? maxy : sps[i];
            cairo_line_to(cr, x, pt+gh-(s/maxy)*gh);
        }
        cairo_line_to(cr, pl+gw, pt+gh);
        cairo_close_path(cr);
        cairo_set_source(cr, area); cairo_fill(cr);
        cairo_pattern_destroy(area);

        /* GPU line if available */
        if (a->gpu_time > 0) {
            double gsp  = a->serial_time / a->gpu_time;
            double gsps[5] = {1.0, gsp*0.5, gsp, gsp*1.3, gsp*1.5};
            cairo_set_source_rgba(cr, 0.49, 0.23, 0.93, 0.85);
            cairo_set_line_width(cr, 2.0);
            for (int i = 0; i < 5; i++) {
                double x = pl + i*xs;
                double s = gsps[i] > maxy ? maxy : gsps[i];
                double y = pt+gh-(s/maxy)*gh;
                if (i==0) cairo_move_to(cr, x, y);
                else      cairo_line_to(cr, x, y);
            }
            cairo_stroke(cr);
        }

        /* CPU line */
        cairo_set_source_rgb(cr, 0.0, 0.83, 1.0);
        cairo_set_line_width(cr, 2.5);
        for (int i = 0; i < 5; i++) {
            double x = pl + i*xs;
            double s = sps[i] > maxy ? maxy : sps[i];
            double y = pt+gh-(s/maxy)*gh;
            if (i==0) cairo_move_to(cr, x, y);
            else      cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);

        /* dots */
        for (int i = 1; i < 5; i++) {
            double x = pl + i*xs;
            double s = sps[i] > maxy ? maxy : sps[i];
            double y = pt+gh-(s/maxy)*gh;
            cairo_set_source_rgb(cr, 0.0, 0.83, 1.0);
            cairo_arc(cr, x, y, 4.0, 0, 2*G_PI); cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_arc(cr, x, y, 1.5, 0, 2*G_PI); cairo_fill(cr);
        }

        /* annotate speedup at peak */
        double peak_x = pl + 2*xs;
        double peak_s = sps[2] > maxy ? maxy : sps[2];
        double peak_y = pt+gh-(peak_s/maxy)*gh - 14;
        cairo_set_source_rgba(cr, 0.0, 0.83, 1.0, 0.9);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 9);
        char sp_lbl[24]; sprintf(sp_lbl, "%.1fx", sp);
        cairo_move_to(cr, peak_x - 10, peak_y);
        cairo_show_text(cr, sp_lbl);
    }

    /* legend */
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    double d2[] = {4.0,2.0};

    cairo_set_source_rgba(cr, 0.94, 0.27, 0.27, 0.7);
    cairo_set_dash(cr, d2, 2, 0); cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, pl+gw-160, pt+10); cairo_line_to(cr, pl+gw-140, pt+10); cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);
    cairo_move_to(cr, pl+gw-134, pt+14); cairo_show_text(cr, "serial");

    cairo_set_source_rgb(cr, 0.0, 0.83, 1.0);
    cairo_set_line_width(cr, 2.5);
    cairo_move_to(cr, pl+gw-100, pt+10); cairo_line_to(cr, pl+gw-80, pt+10); cairo_stroke(cr);
    cairo_move_to(cr, pl+gw-74, pt+14); cairo_show_text(cr, "CPU");

    if (a->gpu_time > 0) {
        cairo_set_source_rgba(cr, 0.49, 0.23, 0.93, 0.85);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, pl+gw-44, pt+10); cairo_line_to(cr, pl+gw-24, pt+10); cairo_stroke(cr);
        cairo_move_to(cr, pl+gw-18, pt+14); cairo_show_text(cr, "GPU");
    }
    return FALSE;
}

// ── helpers ──
static GtkWidget *make_section(const char *text) {
    GtkWidget *l = gtk_label_new(NULL);
    gchar *m = g_markup_printf_escaped(
        "<span size='small' foreground='#475569' weight='bold'>%s</span>", text);
    gtk_label_set_markup(GTK_LABEL(l), m); g_free(m);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(l, 4);
    return l;
}

static GtkWidget *make_detail_card(const char *label, const char *val,
                                    const char *sub, GtkWidget **val_ref) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "detail-card");
    gtk_widget_set_hexpand(box, TRUE);

    GtkWidget *lbl = gtk_label_new(NULL);
    gchar *lm = g_markup_printf_escaped(
        "<span size='small' foreground='#64748b'>%s</span>", label);
    gtk_label_set_markup(GTK_LABEL(lbl), lm); g_free(lm);
    gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);

    GtkWidget *v = gtk_label_new(NULL);
    gchar *vm = g_markup_printf_escaped(
        "<b><span size='large' foreground='#ffffff'>%s</span></b>", val);
    gtk_label_set_markup(GTK_LABEL(v), vm); g_free(vm);
    gtk_widget_set_halign(v, GTK_ALIGN_CENTER);
    if (val_ref) *val_ref = v;

    GtkWidget *s = gtk_label_new(NULL);
    gchar *sm = g_markup_printf_escaped(
        "<span size='small' foreground='#475569'>%s</span>", sub);
    gtk_label_set_markup(GTK_LABEL(s), sm); g_free(sm);
    gtk_widget_set_halign(s, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), v,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), s,   FALSE, FALSE, 0);
    return box;
}

// ── build left panel ──
static GtkWidget *build_left(App *a) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "left-panel");
    gtk_widget_set_size_request(box, 250, -1);

    gtk_box_pack_start(GTK_BOX(box), make_section("INPUT FILE"), FALSE, FALSE, 0);
    GtkWidget *frow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    a->input_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->input_entry), "Select file...");
    gtk_widget_set_hexpand(a->input_entry, TRUE);
    GtkWidget *browse = gtk_button_new_with_label("Browse");
    g_signal_connect(browse, "clicked", G_CALLBACK(on_browse), a);
    gtk_box_pack_start(GTK_BOX(frow), a->input_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(frow), browse, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), frow, FALSE, FALSE, 0);

    GtkWidget *opath_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *opath_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    a->output_path_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(a->output_path_lbl),
        "<span foreground='#475569' size='small'>Output → select a file first</span>");
    gtk_widget_set_halign(a->output_path_lbl, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(a->output_path_lbl), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(a->output_path_lbl), PANGO_WRAP_CHAR);
    gtk_widget_set_hexpand(a->output_path_lbl, TRUE);
    gtk_box_pack_start(GTK_BOX(opath_row), a->output_path_lbl, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(opath_box), opath_row, FALSE, FALSE, 0);

    a->copy_path_btn = gtk_button_new_with_label("Copy Path");
    g_object_set_data_full(G_OBJECT(a->copy_path_btn), "path", g_strdup(""), g_free);
    g_signal_connect(a->copy_path_btn, "clicked", G_CALLBACK(on_copy_path), NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(a->copy_path_btn), "bench-btn");
    gtk_widget_set_halign(a->copy_path_btn, GTK_ALIGN_START);
    gtk_widget_set_sensitive(a->copy_path_btn, FALSE);
    gtk_box_pack_start(GTK_BOX(opath_box), a->copy_path_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), opath_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), make_section("MODE"), FALSE, FALSE, 0);
    GtkWidget *trow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    a->encrypt_btn = gtk_button_new_with_label("Encrypt");
    a->decrypt_btn = gtk_button_new_with_label("Decrypt");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-on");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-off");
    gtk_widget_set_hexpand(a->encrypt_btn, TRUE);
    gtk_widget_set_hexpand(a->decrypt_btn, TRUE);
    g_signal_connect(a->encrypt_btn, "clicked", G_CALLBACK(on_encrypt_toggle), a);
    g_signal_connect(a->decrypt_btn, "clicked", G_CALLBACK(on_decrypt_toggle), a);
    gtk_box_pack_start(GTK_BOX(trow), a->encrypt_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(trow), a->decrypt_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), trow, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), make_section("MPI PROCESSES"), FALSE, FALSE, 0);
    GtkWidget *srow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    a->mpi_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 8, 1);
    gtk_range_set_value(GTK_RANGE(a->mpi_scale), 4);
    gtk_scale_set_draw_value(GTK_SCALE(a->mpi_scale), FALSE);
    gtk_widget_set_hexpand(a->mpi_scale, TRUE);
    a->mpi_val_label = gtk_label_new("4");
    g_signal_connect(a->mpi_scale, "value-changed", G_CALLBACK(on_mpi_changed), a);
    gtk_box_pack_start(GTK_BOX(srow), a->mpi_scale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(srow), a->mpi_val_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), srow, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), make_section("STRATEGY"), FALSE, FALSE, 0);
    GtkWidget *sbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(sbox), "strategy-box");
    a->strategy_label = gtk_label_new("Auto");
    gtk_widget_set_hexpand(a->strategy_label, TRUE);
    gtk_widget_set_halign(a->strategy_label, GTK_ALIGN_START);
    GtkWidget *info = gtk_label_new("?");
    gtk_widget_set_tooltip_text(info,
        "Strategy is auto-selected based on file size,\nCPU cores and GPU availability.");
    gtk_box_pack_start(GTK_BOX(sbox), a->strategy_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(sbox), info, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), sbox, FALSE, FALSE, 0);

    a->status_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(a->status_lbl),
        "<span foreground='#475569' size='small'>Ready</span>");
    gtk_widget_set_halign(a->status_lbl, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(a->status_lbl), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(a->status_lbl), PANGO_WRAP_CHAR);
    gtk_box_pack_start(GTK_BOX(box), a->status_lbl, FALSE, FALSE, 0);

    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(box), spacer, TRUE, TRUE, 0);

    a->run_btn   = gtk_button_new_with_label("Run Encrypt");
    a->bench_btn = gtk_button_new_with_label("Run Benchmark");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->run_btn),   "run-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->bench_btn), "bench-btn");
    g_signal_connect(a->run_btn,   "clicked", G_CALLBACK(on_run_clicked),   a);
    g_signal_connect(a->bench_btn, "clicked", G_CALLBACK(on_bench_clicked), a);
    gtk_box_pack_end(GTK_BOX(box), a->bench_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(box), a->run_btn,   FALSE, FALSE, 0);

    return box;
}

static void on_run_clicked(GtkButton *b, App *a)   { reset_ui(a); launch_process(a, 0); }
static void on_bench_clicked(GtkButton *b, App *a) { reset_ui(a); launch_process(a, 1); }

// ── build tab 1 ──
static GtkWidget *build_tab1(App *a) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *s1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(s1), "section-row");
    gtk_box_pack_start(GTK_BOX(s1), make_section("FILE SPLITTING"), FALSE, FALSE, 0);
    a->split_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(a->split_area, -1, 120);
    g_signal_connect(a->split_area, "draw", G_CALLBACK(on_split_draw), a);
    gtk_box_pack_start(GTK_BOX(s1), a->split_area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), s1, FALSE, FALSE, 0);

    GtkWidget *s2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(s2), "section-row-alt");
    gtk_box_pack_start(GTK_BOX(s2), make_section("THREAD HEATMAP — OPENMP"), FALSE, FALSE, 0);
    a->heatmap_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(a->heatmap_area, -1, 90);
    g_signal_connect(a->heatmap_area, "draw", G_CALLBACK(on_hmap_draw), a);
    gtk_box_pack_start(GTK_BOX(s2), a->heatmap_area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), s2, FALSE, FALSE, 0);

    /* proc_bars: hidden data-tracking widgets (not in visual layout) */
    for (int i = 0; i < 8; i++)
        a->proc_bars[i] = gtk_progress_bar_new();

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), vbox);
    return scrolled;
}

// ── build tab 2 ──
static GtkWidget *build_tab2(App *a) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* row 1 — encryption details */
    GtkWidget *r1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(r1), "section-row");
    gtk_box_pack_start(GTK_BOX(r1), make_section("ENCRYPTION DETAILS"), FALSE, FALSE, 0);
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
    gtk_box_pack_start(GTK_BOX(r1), dgrid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), r1, FALSE, FALSE, 0);

    /* row 2 — time comparison */
    GtkWidget *r2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(r2), "section-row-alt");
    gtk_box_pack_start(GTK_BOX(r2), make_section("TIME COMPARISON"), FALSE, FALSE, 0);

    #define BAR_ROW(label_str, bar_w, time_l, sp_l, css_cls, pill_cls) \
    { \
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); \
        GtkWidget *lbl = gtk_label_new(NULL); \
        gchar *_lm = g_markup_printf_escaped( \
            "<span foreground='#94a3b8' font_family='monospace' size='small'>%s</span>", \
            label_str); \
        gtk_label_set_markup(GTK_LABEL(lbl), _lm); g_free(_lm); \
        gtk_widget_set_size_request(lbl, 110, -1); \
        gtk_widget_set_halign(lbl, GTK_ALIGN_END); \
        bar_w = gtk_progress_bar_new(); \
        gtk_widget_set_hexpand(bar_w, TRUE); \
        gtk_widget_set_size_request(bar_w, -1, 10); \
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar_w), 0.0); \
        gtk_style_context_add_class(gtk_widget_get_style_context(bar_w), css_cls); \
        time_l = gtk_label_new(NULL); \
        gchar *_tm = g_markup_printf_escaped( \
            "<span foreground='#ffffff' font_family='monospace' size='small'>--</span>"); \
        gtk_label_set_markup(GTK_LABEL(time_l), _tm); g_free(_tm); \
        gtk_widget_set_size_request(time_l, 60, -1); \
        sp_l = gtk_label_new(NULL); \
        gchar *_sm = g_markup_printf_escaped( \
            "<span foreground='#94a3b8' size='small'>--</span>"); \
        gtk_label_set_markup(GTK_LABEL(sp_l), _sm); g_free(_sm); \
        gtk_widget_set_size_request(sp_l, 90, -1); \
        gtk_style_context_add_class(gtk_widget_get_style_context(sp_l), pill_cls); \
        gtk_box_pack_start(GTK_BOX(row), lbl,   FALSE, FALSE, 0); \
        gtk_box_pack_start(GTK_BOX(row), bar_w, TRUE,  TRUE,  0); \
        gtk_box_pack_start(GTK_BOX(row), time_l,FALSE, FALSE, 0); \
        gtk_box_pack_start(GTK_BOX(row), sp_l,  FALSE, FALSE, 0); \
        gtk_box_pack_start(GTK_BOX(r2), row, FALSE, FALSE, 0); \
    }

    BAR_ROW("SERIAL",       a->serial_bar, a->serial_time_lbl, a->serial_speedup_lbl, "serial", "speedup-pill-baseline")
    BAR_ROW("MPI + OPENMP", a->cpu_bar,    a->cpu_time_lbl,    a->cpu_speedup_lbl,    "cpu",    "speedup-pill")
    BAR_ROW("MPI + OPENCL", a->gpu_bar,    a->gpu_time_lbl,    a->gpu_speedup_lbl,    "gpu",    "speedup-pill")
    gtk_box_pack_start(GTK_BOX(vbox), r2, FALSE, FALSE, 0);

    /* row 3 — speedup graph */
    GtkWidget *r3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(r3), "section-row");
    gtk_box_pack_start(GTK_BOX(r3), make_section("SPEEDUP GRAPH"), FALSE, FALSE, 0);
    a->graph_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(a->graph_area, -1, 170);
    gtk_widget_set_vexpand(a->graph_area, TRUE);
    g_signal_connect(a->graph_area, "draw", G_CALLBACK(on_graph_draw), a);
    gtk_box_pack_start(GTK_BOX(r3), a->graph_area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), r3, TRUE, TRUE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), vbox);
    return scrolled;
}

// ── main ──
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // Force GTK to prefer the dark theme, stopping Windows/WSL from defaulting to white
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);
    }

    load_css();

    App a;
    memset(&a, 0, sizeof(a));
    a.encrypting = 1; a.num_procs = 4; a.num_threads = 4;

    a.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header),
        "Parallel File Encrypter — MPI + OpenMP + OpenCL");
    gtk_window_set_titlebar(GTK_WINDOW(a.window), header);
    gtk_window_set_default_size(GTK_WINDOW(a.window), 940, 620);
    gtk_window_set_resizable(GTK_WINDOW(a.window), TRUE);
    gtk_window_set_decorated(GTK_WINDOW(a.window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(a.window), GDK_WINDOW_TYPE_HINT_NORMAL);
    g_signal_connect(a.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(root), build_left(&a), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root),
        gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);

    a.notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(a.notebook, TRUE);
    gtk_widget_set_vexpand(a.notebook, TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(a.notebook), FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(a.notebook),
        build_tab1(&a), gtk_label_new("Real-time performance"));
    gtk_notebook_append_page(GTK_NOTEBOOK(a.notebook),
        build_tab2(&a), gtk_label_new("Analysis & speedup"));
    gtk_box_pack_start(GTK_BOX(root), a.notebook, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(a.window), root);
    gtk_widget_show_all(a.window);
    gtk_main();
    return 0;
}