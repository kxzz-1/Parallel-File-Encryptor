#define CL_TARGET_OPENCL_VERSION 300
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libgen.h>

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
    int        thread_active[32];
    int        num_threads;
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
    double  serial_time;
    double  cpu_time;
    double  gpu_time;
    int     encrypting;
    char    input_path[512];
    char    last_output_path[512];
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

// ── build output path next to input file ──
static void build_output_path(const char *input, char *output, int encrypting) {
    char tmp[512];
    strncpy(tmp, input, 511);
    char *dir = dirname(tmp);

    char tmp2[512];
    strncpy(tmp2, input, 511);
    char *base = basename(tmp2);

    if (encrypting) {
        snprintf(output, 512, "%s/%s.enc", dir, base);
    } else {
        char base_copy[512];
        strncpy(base_copy, base, 511);
        char *ext = strstr(base_copy, ".enc");
        if (ext) *ext = '\0';
        snprintf(output, 512, "%s/%s.dec", dir, base_copy);
    }
}

 static void on_copy_clicked(GtkButton *b, gpointer d)
    {
        const char *p = (const char *)g_object_get_data(G_OBJECT(b), "path");
        GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(clip, p, -1);
    }
// ── success popup ──
static void show_output_popup(App *a, const char *path, int encrypted) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        encrypted ? "Encryption Complete" : "Decryption Complete",
        GTK_WINDOW(a->window),
        GTK_DIALOG_MODAL,
        "OK", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);
    gtk_box_set_spacing(GTK_BOX(content), 12);

    GtkWidget *icon_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(icon_lbl),
        encrypted
        ? "<span size='xx-large'>🔒</span>"
        : "<span size='xx-large'>🔓</span>");
    gtk_box_pack_start(GTK_BOX(content), icon_lbl, FALSE, FALSE, 0);

    GtkWidget *title = gtk_label_new(NULL);
    gchar *tm = g_markup_printf_escaped(
        "<b><span size='large' color='#ffffff'>%s</span></b>",
        encrypted ? "File encrypted successfully!" : "File decrypted successfully!");
    gtk_label_set_markup(GTK_LABEL(title), tm);
    g_free(tm);
    gtk_box_pack_start(GTK_BOX(content), title, FALSE, FALSE, 0);

    GtkWidget *path_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *saved_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(saved_lbl),
        "<span color='#64748b' size='small'>Saved to:</span>");
    gtk_widget_set_halign(saved_lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(path_box), saved_lbl, FALSE, FALSE, 0);

    GtkWidget *path_lbl = gtk_label_new(path);
    gtk_label_set_selectable(GTK_LABEL(path_lbl), TRUE);
    gtk_widget_set_halign(path_lbl, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(path_lbl), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(path_lbl), PANGO_WRAP_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(path_lbl), 50);

    gchar *pm = g_markup_printf_escaped(
        "<span color='#00d4ff' font_family='monospace' size='small'>%s</span>", path);
    gtk_label_set_markup(GTK_LABEL(path_lbl), pm);
    g_free(pm);
    gtk_box_pack_start(GTK_BOX(path_box), path_lbl, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), path_box, FALSE, FALSE, 0);

    // copy path button
    GtkWidget *copy_btn = gtk_button_new_with_label("Copy Path");
    g_object_set_data(G_OBJECT(copy_btn), "path", (gpointer)path);
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_copy_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(content), copy_btn, FALSE, FALSE, 0);

    // hint for decryption
    if (encrypted) {
        GtkWidget *hint = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(hint),
            "<span color='#64748b' size='small'>"
            "Switch to Decrypt mode to restore the original file."
            "</span>");
        gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
        gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 0);

        // auto fill decrypt field
        gtk_entry_set_text(GTK_ENTRY(a->input_entry), path);
        strncpy(a->input_path, path, 511);
    }

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// ── CSS ──
static void load_css(void) {
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "* { font-family: sans-serif; color: #ffffff; }"
        "window { background-color: #0a0e1a; }"
        "headerbar { background-color: #0f1628; border-bottom: 2px solid #00d4ff; color: #ffffff; min-height: 48px; }"
        "headerbar title { font-weight: bold; font-size: 13px; color: #ffffff; }"
        "headerbar button { background-color: #151d35; border: 1px solid #1e2d4a; color: #ffffff; padding: 6px; }"
        "headerbar button:hover { background-color: #7f1d1d; border-color: #ef4444; }"
        "notebook > header { background-color: #0f1628; border-bottom: 1px solid #1e2d4a; }"
        "notebook > header > tabs > tab { padding: 12px 28px; color: #94a3b8; background-color: transparent; border: none; font-weight: bold; font-size: 12px; }"
        "notebook > header > tabs > tab:checked { color: #00d4ff; border-bottom: 3px solid #00d4ff; }"
        "notebook > stack { background-color: #0a0e1a; }"
        "entry { background-color: #151d35; color: #ffffff; border: 1px solid #1e2d4a; border-radius: 4px; padding: 10px; font-family: monospace; }"
        "entry:focus { border-color: #00d4ff; }"
        "button { background-color: #0f1e30; color: #ffffff; border: 1px solid #00d4ff; border-radius: 4px; padding: 8px 14px; font-weight: bold; }"
        "button:hover { background-color: #003d5c; }"
        "label { color: #ffffff; }"
        "dialog { background-color: #0f1628; }"
        "dialog button { color: #ffffff; border-color: #00d4ff; background-color: #0f1e30; }"
        ".tog-on { background-color: #003344; border: 1px solid #00d4ff; color: #00d4ff; font-weight: bold; }"
        ".tog-off { background-color: #0f1628; border: 1px solid #1e2d4a; color: #94a3b8; }"
        ".run-btn { background-color: #003d5c; color: #ffffff; font-weight: bold; border: 1px solid #00d4ff; padding: 12px; font-size: 13px; }"
        ".run-btn:hover { background-color: #005580; }"
        ".bench-btn { background-color: #1a0a2e; color: #ffffff; border: 1px solid #7c3aed; padding: 10px; font-size: 12px; }"
        ".bench-btn:hover { background-color: #2e1155; }"
        "progressbar trough { background-color: #1e2d4a; min-height: 8px; border-radius: 3px; }"
        "progressbar progress { min-height: 8px; border-radius: 3px; }"
        "progressbar.serial progress { background-color: #64748b; }"
        "progressbar.cpu progress { background-color: #00d4ff; }"
        "progressbar.gpu progress { background-color: #7c3aed; }"
        ".left-panel { background-color: #0f1628; border-right: 1px solid #1e2d4a; padding: 20px; }"
        ".section-row { background-color: #0a0e1a; border-bottom: 1px solid #1e2d4a; padding: 20px 24px; }"
        ".section-row-alt { background-color: #0d1220; border-bottom: 1px solid #1e2d4a; padding: 20px 24px; }"
        ".detail-card { background-color: #151d35; border: 1px solid #1e2d4a; border-radius: 6px; padding: 14px; }"
        ".card-cyan { border-left: 3px solid #00d4ff; }"
        ".card-violet { border-left: 3px solid #7c3aed; }"
        ".card-mint { border-left: 3px solid #00e5a0; }"
        ".card-amber { border-left: 3px solid #f59e0b; }"
        ".strategy-box { background-color: #0d1e2e; border-left: 3px solid #00d4ff; border-radius: 4px; padding: 10px 14px; }"
        ".speedup-pill { background-color: #0a3d2e; color: #00e5a0; border-radius: 4px; padding: 2px 8px; font-weight: bold; font-family: monospace; font-size: 11px; }"
        ".speedup-pill-baseline { background-color: #1e2a38; color: #94a3b8; border-radius: 4px; padding: 2px 8px; font-family: monospace; font-size: 11px; }"
        ".sec-title { font-size: 10px; color: #475569; font-weight: bold; }"
        ".bar-lbl { font-family: monospace; font-size: 11px; color: #94a3b8; }"
        ".bar-time { font-family: monospace; font-size: 12px; color: #ffffff; }"
        ".val-text { font-family: monospace; font-size: 18px; font-weight: bold; color: #ffffff; }"
        ".output-path { font-family: monospace; font-size: 11px; color: #00d4ff; }"
        "scale trough { background-color: #1e2d4a; border-radius: 4px; }"
        "scale highlight { background-color: #00d4ff; border-radius: 4px; }"
        "separator { background-color: #1e2d4a; }"
        , -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
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
        strncpy(a->input_path, fn, 511);

        // show where output will be saved
        char out[512];
        build_output_path(fn, out, a->encrypting);
        gchar *m = g_markup_printf_escaped(
            "<span color='#475569' size='small'>Output → </span>"
            "<span color='#00d4ff' font_family='monospace' size='small'>%s</span>", out);
        gtk_label_set_markup(GTK_LABEL(a->output_path_lbl), m);
        g_free(m);

        g_free(fn);
    }
    gtk_widget_destroy(dlg);
}

static void on_encrypt_toggle(GtkButton *b, App *a) {
    a->encrypting = 1;
    gtk_button_set_label(GTK_BUTTON(a->run_btn), "Run Encrypt");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-on");
    gtk_style_context_remove_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-on");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-off");
}

static void on_decrypt_toggle(GtkButton *b, App *a) {
    a->encrypting = 0;
    gtk_button_set_label(GTK_BUTTON(a->run_btn), "Run Decrypt");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-on");
    gtk_style_context_remove_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-on");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-off");
}

static void on_mpi_changed(GtkRange *r, App *a) {
    int v = (int)gtk_range_get_value(r);
    char buf[4];
    sprintf(buf, "%d", v);
    gtk_label_set_text(GTK_LABEL(a->mpi_val_label), buf);
}

static void reset_ui(App *a) {
    for (int i = 0; i < 8; i++) {
        if (a->proc_bars[i])
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->proc_bars[i]), 0.0);
        a->thread_active[i] = 0;
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
}

static void on_run_clicked(GtkButton *b, App *a)   { reset_ui(a); launch_process(a, 0); }
static void on_bench_clicked(GtkButton *b, App *a) { reset_ui(a); launch_process(a, 1); }

// ── cairo: file split ──
static gboolean on_split_draw(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    int W = alloc.width, H = alloc.height;
    int n = a->num_procs > 0 ? a->num_procs : 2;

    cairo_set_source_rgb(cr, 10/255.0, 14/255.0, 26/255.0);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);

    double colors[8][3] = {
        {0.0,0.83,1.0},{0.49,0.23,0.93},{0.0,0.90,0.63},{0.96,0.62,0.04},
        {0.94,0.27,0.27},{0.2,0.6,1.0},{0.6,0.23,0.93},{0.0,0.70,0.53}
    };

    int chunk_w = (W - 20) / n;
    int bar_h = 26, bar_y = 10;

    for (int i = 0; i < n; i++) {
        int x = 10 + i * chunk_w;
        cairo_set_source_rgba(cr, 0.08, 0.11, 0.21, 1);
        cairo_rectangle(cr, x, bar_y, chunk_w - 2, bar_h);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
        cairo_set_line_width(cr, 1.5);
        cairo_rectangle(cr, x, bar_y, chunk_w - 2, bar_h);
        cairo_stroke(cr);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 9);
        char lbl[12]; sprintf(lbl, "P-%d", i);
        cairo_move_to(cr, x + 6, bar_y + 17);
        cairo_show_text(cr, lbl);
    }

    int arrow_y1 = bar_y + bar_h + 4;
    int arrow_y2 = arrow_y1 + 14;
    for (int i = 0; i < n; i++) {
        int cx = 10 + i * chunk_w + (chunk_w - 2) / 2;
        cairo_set_source_rgba(cr, 0.30, 0.42, 0.60, 0.5);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, cx, arrow_y1);
        cairo_line_to(cr, cx, arrow_y2);
        cairo_stroke(cr);
        cairo_move_to(cr, cx - 4, arrow_y2 - 4);
        cairo_line_to(cr, cx,     arrow_y2);
        cairo_line_to(cr, cx + 4, arrow_y2 - 4);
        cairo_stroke(cr);
    }

    int pbox_y = arrow_y2 + 4;
    int pbox_h = H - pbox_y - 10;
    if (pbox_h > 40) pbox_h = 40;

    for (int i = 0; i < n; i++) {
        int x = 10 + i * chunk_w;
        cairo_set_source_rgba(cr, 0.08, 0.11, 0.21, 1);
        cairo_rectangle(cr, x, pbox_y, chunk_w - 2, pbox_h);
        cairo_fill(cr);

        double frac = (a->proc_bars[i]) ?
            gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(a->proc_bars[i])) : 0.0;
        if (frac > 0) {
            cairo_set_source_rgba(cr, colors[i][0], colors[i][1], colors[i][2], 0.25);
            int fill_h = (int)(frac * pbox_h);
            cairo_rectangle(cr, x, pbox_y + pbox_h - fill_h, chunk_w - 2, fill_h);
            cairo_fill(cr);
        }

        cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, x, pbox_y, chunk_w - 2, pbox_h);
        cairo_stroke(cr);

        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9);
        char plbl[12]; sprintf(plbl, "MPI %d", i);
        cairo_move_to(cr, x + 5, pbox_y + 14);
        cairo_show_text(cr, plbl);

        if (frac >= 1.0) {
            cairo_set_source_rgb(cr, colors[i][0], colors[i][1], colors[i][2]);
            cairo_set_font_size(cr, 12);
            cairo_move_to(cr, x + (chunk_w-2)/2 - 4, pbox_y + pbox_h/2 + 5);
            cairo_show_text(cr, "OK");
        }
    }
    return FALSE;
}

// ── cairo: heatmap ──
static gboolean on_hmap_draw(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    int W = alloc.width;
    int n = a->num_threads > 0 ? a->num_threads : 4;
    if (n > 16) n = 16;

    cairo_set_source_rgb(cr, 10/255.0, 14/255.0, 26/255.0);
    cairo_rectangle(cr, 0, 0, W, alloc.height);
    cairo_fill(cr);

    int cols = n > 8 ? 8 : n;
    int rows = (n + cols - 1) / cols;
    int gap = 5;
    int cell_w = (W - 20 - gap*(cols-1)) / cols;
    if (cell_w > 60) cell_w = 60;
    int total_w = cols*cell_w + (cols-1)*gap;
    int start_x = 10 + (W - 20 - total_w) / 2;
    int cell_h = 26;

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9);

    for (int i = 0; i < n; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = start_x + col*(cell_w+gap);
        int y = 10 + row*(cell_h+gap);

        if (a->thread_active[i]) {
            cairo_set_source_rgba(cr, 0.0, 0.83, 1.0, 0.15);
            cairo_rectangle(cr, x-2, y-2, cell_w+4, cell_h+4);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.0, 0.6, 0.9);
        } else {
            cairo_set_source_rgb(cr, 0.08, 0.11, 0.21);
        }
        cairo_rectangle(cr, x, y, cell_w, cell_h);
        cairo_fill(cr);

        if (a->thread_active[i])
            cairo_set_source_rgb(cr, 0.0, 0.83, 1.0);
        else
            cairo_set_source_rgba(cr, 0.3, 0.4, 0.55, 0.6);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, x, y, cell_w, cell_h);
        cairo_stroke(cr);

        if (a->thread_active[i])
            cairo_set_source_rgb(cr, 1, 1, 1);
        else
            cairo_set_source_rgba(cr, 0.5, 0.6, 0.7, 0.7);
        char lbl[8]; sprintf(lbl, "T%d", i);
        cairo_move_to(cr, x + cell_w/2 - 6, y + cell_h/2 + 4);
        cairo_show_text(cr, lbl);
    }

    int ly = 10 + rows*(cell_h+gap) + 8;
    cairo_set_source_rgb(cr, 0.0, 0.83, 1.0);
    cairo_rectangle(cr, 10, ly, 9, 9);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    cairo_move_to(cr, 24, ly + 9); cairo_show_text(cr, "Active");
    cairo_set_source_rgba(cr, 0.3, 0.4, 0.55, 0.6);
    cairo_rectangle(cr, 72, ly, 9, 9); cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, 86, ly + 9); cairo_show_text(cr, "Idle");
    return FALSE;
}

// ── cairo: speedup graph ──
static gboolean on_graph_draw(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    int W = alloc.width, H = alloc.height;
    int pad_l=40, pad_r=20, pad_t=14, pad_b=24;
    int gw=W-pad_l-pad_r, gh=H-pad_t-pad_b;

    cairo_set_source_rgb(cr, 10/255.0, 14/255.0, 26/255.0);
    cairo_rectangle(cr, 0, 0, W, H); cairo_fill(cr);

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 8);

    double y_ticks[] = {0,2,4,6,8,10};
    double max_y = 10.0;
    for (int i = 0; i < 6; i++) {
        double y = pad_t + gh - (y_ticks[i]/max_y)*gh;
        cairo_set_source_rgba(cr, 0.12, 0.18, 0.29, 0.6);
        cairo_set_line_width(cr, 0.5);
        cairo_move_to(cr, pad_l, y); cairo_line_to(cr, pad_l+gw, y); cairo_stroke(cr);
        char lbl[8]; sprintf(lbl, "%.0fx", y_ticks[i]);
        cairo_set_source_rgb(cr, 0.55, 0.65, 0.75);
        cairo_text_extents_t ex; cairo_text_extents(cr, lbl, &ex);
        cairo_move_to(cr, pad_l-ex.width-6, y+3); cairo_show_text(cr, lbl);
    }

    int x_procs[] = {1,2,4,6,8};
    double x_step = (double)gw/4;
    cairo_set_source_rgb(cr, 0.55, 0.65, 0.75);
    for (int i = 0; i < 5; i++) {
        double x = pad_l + i*x_step;
        char lbl[8]; sprintf(lbl, "%dp", x_procs[i]);
        cairo_text_extents_t ex; cairo_text_extents(cr, lbl, &ex);
        cairo_move_to(cr, x-ex.width/2.0, H-6); cairo_show_text(cr, lbl);
    }

    cairo_set_source_rgba(cr, 0.94, 0.27, 0.27, 0.7);
    cairo_set_line_width(cr, 1.5);
    double dash[] = {5.0,3.0};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_move_to(cr, pad_l, pad_t+gh);
    cairo_line_to(cr, pad_l+gw, pad_t+gh);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    if (a->serial_time > 0 && a->cpu_time > 0) {
        double sp = a->serial_time / a->cpu_time;
        double speedups[5] = {1.0, sp*0.5, sp, sp*1.3, sp*1.5};

        cairo_pattern_t *area = cairo_pattern_create_linear(0, pad_t, 0, pad_t+gh);
        cairo_pattern_add_color_stop_rgba(area, 0, 0.0, 0.83, 1.0, 0.18);
        cairo_pattern_add_color_stop_rgba(area, 1, 0.0, 0.83, 1.0, 0.0);
        cairo_move_to(cr, pad_l, pad_t+gh);
        for (int i = 0; i < 5; i++) {
            double x = pad_l + i*x_step;
            double s = speedups[i] > max_y ? max_y : speedups[i];
            double y = pad_t + gh - (s/max_y)*gh;
            cairo_line_to(cr, x, y);
        }
        cairo_line_to(cr, pad_l+gw, pad_t+gh);
        cairo_close_path(cr);
        cairo_set_source(cr, area);
        cairo_fill(cr);
        cairo_pattern_destroy(area);

        cairo_set_source_rgb(cr, 0.0, 0.83, 1.0);
        cairo_set_line_width(cr, 2.5);
        for (int i = 0; i < 5; i++) {
            double x = pad_l + i*x_step;
            double s = speedups[i] > max_y ? max_y : speedups[i];
            double y = pad_t + gh - (s/max_y)*gh;
            if (i==0) cairo_move_to(cr, x, y);
            else      cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
        for (int i = 1; i < 5; i++) {
            double x = pad_l + i*x_step;
            double s = speedups[i] > max_y ? max_y : speedups[i];
            double y = pad_t + gh - (s/max_y)*gh;
            cairo_set_source_rgb(cr, 0.0, 0.83, 1.0);
            cairo_arc(cr, x, y, 4.0, 0, 2*G_PI); cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_arc(cr, x, y, 1.5, 0, 2*G_PI); cairo_fill(cr);
        }
    }

    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    double d2[] = {4.0,2.0};
    cairo_set_source_rgba(cr, 0.94, 0.27, 0.27, 0.7);
    cairo_set_dash(cr, d2, 2, 0); cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, pad_l+gw-110, pad_t+10);
    cairo_line_to(cr, pad_l+gw-90,  pad_t+10); cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);
    cairo_move_to(cr, pad_l+gw-84, pad_t+14); cairo_show_text(cr, "serial");
    cairo_set_source_rgb(cr, 0.0, 0.83, 1.0);
    cairo_set_line_width(cr, 2.5);
    cairo_move_to(cr, pad_l+gw-44, pad_t+10);
    cairo_line_to(cr, pad_l+gw-24, pad_t+10); cairo_stroke(cr);
    cairo_move_to(cr, pad_l+gw-18, pad_t+14); cairo_show_text(cr, "parallel");
    return FALSE;
}

// ── parse stdout ──
static void parse_line(const char *line, App *a) {
    char mode[64];
    if (sscanf(line, "[INFO] Mode selected : %63[^\n]", mode) == 1) {
        gtk_label_set_text(GTK_LABEL(a->strategy_label), mode);
        char tip[256];
        snprintf(tip, sizeof(tip), "Auto-selected: %s", mode);
        gtk_widget_set_tooltip_text(a->strategy_label, tip);
    }

    unsigned long long fs;
    if (sscanf(line, "[INFO] File size     : %llu", &fs) == 1) {
        char buf[32]; sprintf(buf, "%llu MB", fs);
        gchar *m = g_markup_printf_escaped("<b>%s</b>", buf);
        gtk_label_set_markup(GTK_LABEL(a->detail_filesize), m);
        g_free(m);
    }

    int chunks;
    if (sscanf(line, "[INFO] Chunks        : %d", &chunks) == 1) {
        a->num_procs = chunks;
        char buf[8]; sprintf(buf, "%d", chunks);
        gchar *m = g_markup_printf_escaped("<b>%s</b>", buf);
        gtk_label_set_markup(GTK_LABEL(a->detail_chunks), m);
        g_free(m);
        gtk_widget_queue_draw(a->split_area);
    }

    int threads;
    if (sscanf(line, "[INFO] Threads/proc  : %d", &threads) == 1) {
        a->num_threads = threads;
        char buf[8]; sprintf(buf, "%d", threads);
        gchar *m = g_markup_printf_escaped("<b>%s</b>", buf);
        gtk_label_set_markup(GTK_LABEL(a->detail_threads), m);
        g_free(m);
        gtk_widget_queue_draw(a->heatmap_area);
    }

    int proc;
    if (sscanf(line, "[OK] Process %d received", &proc) == 1) {
        if (proc >= 0 && proc < 8 && a->proc_bars[proc])
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->proc_bars[proc]), 0.5);
        gtk_widget_queue_draw(a->split_area);
    }

    int tid; unsigned long long s, e;
    if (sscanf(line, "[INFO] Thread %d \xe2\x86\x92 encrypted bytes %llu to %llu", &tid, &s, &e) == 3) {
        if (tid >= 0 && tid < 32) a->thread_active[tid] = 1;
        gtk_widget_queue_draw(a->heatmap_area);
    }

    if (sscanf(line, "[OK] Process %d \xe2\x80\x94 all threads done", &proc) == 1) {
        if (proc >= 0 && proc < 8 && a->proc_bars[proc])
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->proc_bars[proc]), 1.0);
        for (int i = 0; i < 32; i++) a->thread_active[i] = 0;
        gtk_widget_queue_draw(a->split_area);
        gtk_widget_queue_draw(a->heatmap_area);
    }

    // detect completion from output file written line
    char written_path[512];
    if (sscanf(line, "[OK]   Written encrypted file: %511[^\n]", written_path) == 1 ||
        sscanf(line, "[OK] Encrypted file written: %511[^\n]", written_path) == 1) {
        strncpy(a->last_output_path, written_path, 511);
    }
    if (sscanf(line, "[OK]   Written decrypted file: %511[^\n]", written_path) == 1 ||
        sscanf(line, "[OK] Decrypted file written: %511[^\n]", written_path) == 1) {
        strncpy(a->last_output_path, written_path, 511);
    }

    double t;
    if (sscanf(line, "[BENCH] Serial done: %lf", &t) == 1) {
        a->serial_time = t;
        char buf[32]; sprintf(buf, "%.3fs", t);
        gtk_label_set_text(GTK_LABEL(a->serial_time_lbl), buf);
        gtk_label_set_text(GTK_LABEL(a->serial_speedup_lbl), "baseline");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->serial_bar), 1.0);
    }
    if (sscanf(line, "[BENCH] MPI + OpenMP done: %lf", &t) == 1) {
        a->cpu_time = t;
        char buf[32]; sprintf(buf, "%.3fs", t);
        gtk_label_set_text(GTK_LABEL(a->cpu_time_lbl), buf);
        if (a->serial_time > 0) {
            char sp[16]; sprintf(sp, "%.1fx faster", a->serial_time/t);
            gtk_label_set_text(GTK_LABEL(a->cpu_speedup_lbl), sp);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->cpu_bar), t/a->serial_time);
            gtk_widget_queue_draw(a->graph_area);
        }
    }
    if (sscanf(line, "[BENCH] MPI + OpenCL done: %lf", &t) == 1) {
        a->gpu_time = t;
        char buf[32]; sprintf(buf, "%.3fs", t);
        gtk_label_set_text(GTK_LABEL(a->gpu_time_lbl), buf);
        if (a->serial_time > 0) {
            char sp[16]; sprintf(sp, "%.1fx faster", a->serial_time/t);
            gtk_label_set_text(GTK_LABEL(a->gpu_speedup_lbl), sp);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->gpu_bar), t/a->serial_time);
            gtk_widget_queue_draw(a->graph_area);
        }
    }
}

// ── launch subprocess ──
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

    // build output path next to input file
    char output[512];
    build_output_path(input, output, a->encrypting);
    strncpy(a->last_output_path, output, 511);

    int mpi = (int)gtk_range_get_value(GTK_RANGE(a->mpi_scale));
    char cmd[1536];

    if (bench) {
        snprintf(cmd, sizeof(cmd),
            "cd \"%s\" && mpirun -np %d \"%s/encrypter\" %s \"%s\" --bench 2>&1",
            getenv("HOME"), mpi, getenv("HOME"),
            a->encrypting ? "-e" : "-d", input);
    } else {
        snprintf(cmd, sizeof(cmd),
            "cd \"%s\" && mpirun -np %d \"%s/encrypter\" %s \"%s\" 2>&1",
            getenv("HOME"), mpi, getenv("HOME"),
            a->encrypting ? "-e" : "-d", input);
    }

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return;
    char line[512];
    while (fgets(line, sizeof(line), pipe)) {
        line[strcspn(line, "\n")] = 0;
        parse_line(line, a);
        while (gtk_events_pending()) gtk_main_iteration();
    }
    pclose(pipe);

    // show popup after run (not benchmark)
    if (!bench && strlen(a->last_output_path) > 0) {
        show_output_popup(a, a->last_output_path, a->encrypting);
    }
}

// ── helpers ──
static GtkWidget *make_section(const char *text) {
    GtkWidget *l = gtk_label_new(NULL);
    gchar *m = g_markup_printf_escaped(
        "<span size='small' color='#475569' weight='bold'>%s</span>", text);
    gtk_label_set_markup(GTK_LABEL(l), m);
    g_free(m);
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
        "<span size='small' color='#64748b'>%s</span>", label);
    gtk_label_set_markup(GTK_LABEL(lbl), lm); g_free(lm);
    gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);

    GtkWidget *v = gtk_label_new(NULL);
    gchar *vm = g_markup_printf_escaped(
        "<b><span size='large' color='#ffffff'>%s</span></b>", val);
    gtk_label_set_markup(GTK_LABEL(v), vm); g_free(vm);
    gtk_widget_set_halign(v, GTK_ALIGN_CENTER);
    if (val_ref) *val_ref = v;

    GtkWidget *s = gtk_label_new(NULL);
    gchar *sm = g_markup_printf_escaped(
        "<span size='small' color='#475569'>%s</span>", sub);
    gtk_label_set_markup(GTK_LABEL(s), sm); g_free(sm);
    gtk_widget_set_halign(s, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), v,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), s,   FALSE, FALSE, 0);
    return box;
}

// ── build left panel ──
static GtkWidget *build_left(App *a) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
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

    // output path preview
    a->output_path_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(a->output_path_lbl),
        "<span color='#475569' size='small'>Output → select a file first</span>");
    gtk_widget_set_halign(a->output_path_lbl, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(a->output_path_lbl), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(a->output_path_lbl), PANGO_WRAP_CHAR);
    gtk_box_pack_start(GTK_BOX(box), a->output_path_lbl, FALSE, FALSE, 0);

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
    gtk_range_set_value(GTK_RANGE(a->mpi_scale), 2);
    gtk_scale_set_draw_value(GTK_SCALE(a->mpi_scale), FALSE);
    gtk_widget_set_hexpand(a->mpi_scale, TRUE);
    a->mpi_val_label = gtk_label_new("2");
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
        "Strategy selected at runtime based on\n"
        "file size, CPU cores and GPU availability.");
    gtk_box_pack_start(GTK_BOX(sbox), a->strategy_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(sbox), info, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), sbox, FALSE, FALSE, 0);

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

// ── build tab 1 ──
static GtkWidget *build_tab1(App *a) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *s1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(s1), "section-row");
    gtk_box_pack_start(GTK_BOX(s1), make_section("FILE SPLITTING"), FALSE, FALSE, 0);
    a->split_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(a->split_area, -1, 110);
    g_signal_connect(a->split_area, "draw", G_CALLBACK(on_split_draw), a);
    gtk_box_pack_start(GTK_BOX(s1), a->split_area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), s1, FALSE, FALSE, 0);

    GtkWidget *s2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(s2), "section-row-alt");
    gtk_box_pack_start(GTK_BOX(s2), make_section("THREAD HEATMAP — OPENMP"), FALSE, FALSE, 0);
    a->heatmap_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(a->heatmap_area, -1, 80);
    g_signal_connect(a->heatmap_area, "draw", G_CALLBACK(on_hmap_draw), a);
    gtk_box_pack_start(GTK_BOX(s2), a->heatmap_area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), s2, FALSE, FALSE, 0);

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

    // row 1 — encryption details
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

    // row 2 — time comparison
    GtkWidget *r2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(r2), "section-row-alt");
    gtk_box_pack_start(GTK_BOX(r2), make_section("TIME COMPARISON"), FALSE, FALSE, 0);

    #define BAR_ROW(label_str, bar_w, time_l, sp_l, css_cls, pill_cls) \
    { \
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); \
        GtkWidget *lbl = gtk_label_new(label_str); \
        gtk_widget_set_size_request(lbl, 110, -1); \
        gtk_widget_set_halign(lbl, GTK_ALIGN_END); \
        bar_w = gtk_progress_bar_new(); \
        gtk_widget_set_hexpand(bar_w, TRUE); \
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar_w), 0.0); \
        gtk_style_context_add_class(gtk_widget_get_style_context(bar_w), css_cls); \
        time_l = gtk_label_new("--"); \
        gtk_widget_set_size_request(time_l, 60, -1); \
        sp_l = gtk_label_new("--"); \
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

    // row 3 — speedup graph
    GtkWidget *r3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(r3), "section-row");
    gtk_box_pack_start(GTK_BOX(r3), make_section("SPEEDUP GRAPH"), FALSE, FALSE, 0);
    a->graph_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(a->graph_area, -1, 160);
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
    load_css();

    App a;
    memset(&a, 0, sizeof(a));
    a.encrypting = 1; a.num_procs = 2; a.num_threads = 4;

    a.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header),
        "Parallel File Encrypter — MPI + OpenMP + OpenCL");
    gtk_window_set_titlebar(GTK_WINDOW(a.window), header);
    gtk_window_set_default_size(GTK_WINDOW(a.window), 920, 600);
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