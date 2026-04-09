#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <libgen.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "app_state.h"

void backend_parse_line(const char *line, App *a);
void backend_parser_reset(App *a);
void ui_start_animation(App *a);
void ui_stop_animation(App *a);

static void set_default_cursor_on_realize(GtkWidget *w, gpointer user_data) {
    (void)user_data;
    GdkWindow *gw = gtk_widget_get_window(w);
    if (!gw) return;

    GdkDisplay *display = gdk_window_get_display(gw);
    if (!display) return;

    GdkCursor *cursor = gdk_cursor_new_from_name(display, "default");
    if (!cursor) cursor = gdk_cursor_new_for_display(display, GDK_LEFT_PTR);
    gdk_window_set_cursor(gw, cursor);
    if (cursor) g_object_unref(cursor);
}

/* ── called from backend_parser to reveal a time row ─────────────────────── */
void ui_reveal_time_row(App *a, GtkWidget *row) {
    if (!row) return;
    GtkStyleContext *sc = gtk_widget_get_style_context(row);
    gtk_style_context_remove_class(sc, "time-row-hidden");
    gtk_style_context_add_class(sc, "time-row-visible");
}

void ui_reveal_time_row_for_mode(App *a, ExecMode mode) {
    if (!a) return;
    switch (mode) {
        case MODE_SERIAL:
            ui_reveal_time_row(a, a->serial_row_widget);
            break;
        case MODE_MPI_OPENMP:
            ui_reveal_time_row(a, a->cpu_row_widget);
            break;
        case MODE_MPI_OPENCL:
            ui_reveal_time_row(a, a->gpu_row_widget);
            break;
        default:
            break;
    }
}

/* ── output path helper ─────────────────────────────────────────────────── */
static void build_output_path(const char *input, char *output, int encrypting) {
    char tmp[512], tmp2[512];
    g_strlcpy(tmp,  input, sizeof(tmp));  char *dir  = dirname(tmp);
    g_strlcpy(tmp2, input, sizeof(tmp2)); char *base = basename(tmp2);
    if (encrypting) {
        g_snprintf(output, 512, "%s/%s.enc", dir, base);
    } else {
        char bc[1024]; g_strlcpy(bc, base, sizeof(bc));
        char *ext = strstr(bc, ".enc");
        if (ext) *ext = '\0';
        else strncat(bc, ".dec", sizeof(bc) - strlen(bc) - 1);
        g_snprintf(output, 1024, "%s/%s", dir, bc);
    }
}

/* ── resolve encrypter binary path ──────────────────────────────────────── */
static void get_encrypter_path(char *out, size_t sz) {
    /* Try: same directory as this executable */
    char exe[512] = {0};
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe)-1);
    if (len > 0) {
        exe[len] = '\0';
        char tmp[512]; g_strlcpy(tmp, exe, sizeof(tmp));
        char *dir = dirname(tmp);
        snprintf(out, sz, "%s/encrypter", dir);
        struct stat st;
        if (stat(out, &st) == 0 && (st.st_mode & S_IXUSR)) return;
    }
    /* Fallback to known WSL path */
    const char *home = getenv("HOME");
    if (!home) home = "/home/batman";
    snprintf(out, sz, "%s/encrypter_new/Parallel-File-Encryptor/encrypter", home);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Professional error dialog
   ═══════════════════════════════════════════════════════════════════════════ */
static void show_error_dialog(App *a, const char *msg) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Operation Failed", GTK_WINDOW(a->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Dismiss", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 420, -1);

    GtkCssProvider *cp = gtk_css_provider_new();
    gtk_css_provider_load_from_data(cp,
        "dialog { background-color: #0d1526; }"
        "dialog * { color: #e2e8f0; background-color: transparent; }"
        ".dialog-action-area button {"
        "  background: rgba(239,68,68,0.15); border: 1px solid #ef4444;"
        "  color: #ffffff; border-radius: 6px; padding: 7px 20px; font-weight: bold; }"
        ".dialog-action-area button:hover { background: rgba(239,68,68,0.30); }"
        , -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(dlg),
        GTK_STYLE_PROVIDER(cp), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
    g_object_unref(cp);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 24);
    gtk_box_set_spacing(GTK_BOX(content), 14);

    GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(icon), "<span size='xx-large' foreground='#ef4444'>⚠</span>");
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<b><span size='large' foreground='#ef4444'>An error occurred</span></b>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(title_row), icon,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(title_row), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), title_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    GtkWidget *msg_lbl = gtk_label_new(NULL);
    gchar *escaped = g_markup_escape_text(msg, -1);
    gchar *markup  = g_strdup_printf(
        "<span font_family='monospace' size='small' foreground='#94a3b8'>%s</span>", escaped);
    gtk_label_set_markup(GTK_LABEL(msg_lbl), markup);
    g_free(escaped); g_free(markup);
    gtk_label_set_line_wrap(GTK_LABEL(msg_lbl), TRUE);
    gtk_label_set_selectable(GTK_LABEL(msg_lbl), TRUE);
    gtk_widget_set_halign(msg_lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), msg_lbl, FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Professional success popup
   ═══════════════════════════════════════════════════════════════════════════ */
static void show_output_popup(App *a, const char *path, int encrypted) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        encrypted ? "Encryption Complete" : "Decryption Complete",
        GTK_WINDOW(a->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        encrypted ? "Decrypt Now" : "Close", GTK_RESPONSE_APPLY,
        "Close",                              GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 460, -1);

    GtkCssProvider *cp = gtk_css_provider_new();
    gtk_css_provider_load_from_data(cp,
        "dialog { background-color: #0d1526; }"
        "dialog * { color: #e2e8f0; background-color: transparent; }"
        ".dialog-action-area button {"
        "  border-radius: 6px; padding: 7px 20px; font-weight: bold; }"
        ".dialog-action-area button:first-child {"
        "  background: rgba(0,212,255,0.12); border: 1px solid #00d4ff; color: #00d4ff; }"
        ".dialog-action-area button:first-child:hover { background: rgba(0,212,255,0.25); }"
        ".dialog-action-area button:last-child {"
        "  background: rgba(30,45,74,0.8); border: 1px solid #334155; color: #94a3b8; }"
        , -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(dlg),
        GTK_STYLE_PROVIDER(cp), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
    g_object_unref(cp);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 24);
    gtk_box_set_spacing(GTK_BOX(content), 12);

    GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(icon),
        encrypted ? "<span size='xx-large' foreground='#00d4ff'>🔒</span>"
                  : "<span size='xx-large' foreground='#00e5a0'>🔓</span>");
    GtkWidget *title = gtk_label_new(NULL);
    char title_markup[128];
    snprintf(title_markup, sizeof(title_markup),
        "<b><span size='large' foreground='%s'>%s</span></b>",
        encrypted ? "#00d4ff" : "#00e5a0",
        encrypted ? "File encrypted successfully" : "File decrypted successfully");
    gtk_label_set_markup(GTK_LABEL(title), title_markup);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(title_row), icon,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(title_row), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), title_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    GtkWidget *saved = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(saved),
        "<span size='small' foreground='#475569'>Saved to:</span>");
    gtk_widget_set_halign(saved, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), saved, FALSE, FALSE, 0);

    GtkWidget *path_lbl = gtk_label_new(NULL);
    gchar *pm = g_markup_printf_escaped(
        "<span font_family='monospace' size='small' foreground='#00d4ff'>%s</span>", path);
    gtk_label_set_markup(GTK_LABEL(path_lbl), pm);
    g_free(pm);
    gtk_label_set_selectable(GTK_LABEL(path_lbl), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(path_lbl), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(path_lbl), PANGO_WRAP_CHAR);
    gtk_widget_set_halign(path_lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), path_lbl, FALSE, FALSE, 0);

    if (encrypted) {
        GtkWidget *hint = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(hint),
            "<span size='small' foreground='#475569'>"
            "Click \"Decrypt Now\" to switch mode and restore the file."
            "</span>");
        gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
        gtk_widget_set_halign(hint, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 0);
        gtk_entry_set_text(GTK_ENTRY(a->input_entry), path);
        g_strlcpy(a->input_path, path, sizeof(a->input_path));
    }

    gtk_widget_show_all(dlg);
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (resp == GTK_RESPONSE_APPLY && encrypted) {
        a->encrypting = 0;
        gtk_button_set_label(GTK_BUTTON(a->run_btn), "▶  Run Decrypt");
        gtk_style_context_add_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-on");
        gtk_style_context_remove_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-off");
        gtk_style_context_add_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-off");
        gtk_style_context_remove_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-on");
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */

void on_copy_path(GtkButton *b, gpointer d) {
    const char *p = (const char *)g_object_get_data(G_OBJECT(b), "path");
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), p, -1);
}

void on_browse(GtkButton *b, App *a) {
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Select File", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    g_signal_connect(dlg, "realize", G_CALLBACK(set_default_cursor_on_realize), NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), "/mnt/c/Users");
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_entry_set_text(GTK_ENTRY(a->input_entry), fn);
        g_strlcpy(a->input_path, fn, sizeof(a->input_path));
        char out[512]; build_output_path(fn, out, a->encrypting);
        gchar *m = g_markup_printf_escaped(
            "<span foreground='#475569' size='small'>→ </span>"
            "<span foreground='#00d4ff' font_family='monospace' size='small'>%s</span>", out);
        gtk_label_set_markup(GTK_LABEL(a->output_path_lbl), m);
        if (a->copy_path_btn) {
            g_object_set_data_full(G_OBJECT(a->copy_path_btn), "path", g_strdup(out), g_free);
            gtk_widget_set_sensitive(a->copy_path_btn, TRUE);
        }
        g_free(m); g_free(fn);
    }
    gtk_widget_destroy(dlg);
}

void on_browse_dest(GtkButton *b, App *a) {
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Select Destination File", GTK_WINDOW(a->window), GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    g_signal_connect(dlg, "realize", G_CALLBACK(set_default_cursor_on_realize), NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    if (strlen(a->input_path) > 0) {
        char dir[512]; g_strlcpy(dir, a->input_path, sizeof(dir));
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), dirname(dir));
    }
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_entry_set_text(GTK_ENTRY(a->dest_entry), fn); g_free(fn);
    }
    gtk_widget_destroy(dlg);
}

void on_encrypt_toggle(GtkButton *b, App *a) {
    a->encrypting = 1;
    gtk_button_set_label(GTK_BUTTON(a->run_btn), "▶  Run Encrypt");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-on");
    gtk_style_context_remove_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-off");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-off");
    gtk_style_context_remove_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-on");
}

void on_decrypt_toggle(GtkButton *b, App *a) {
    a->encrypting = 0;
    gtk_button_set_label(GTK_BUTTON(a->run_btn), "▶  Run Decrypt");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-on");
    gtk_style_context_remove_class(gtk_widget_get_style_context(a->decrypt_btn), "tog-off");
    gtk_style_context_add_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-off");
    gtk_style_context_remove_class(gtk_widget_get_style_context(a->encrypt_btn), "tog-on");
}

void on_mpi_changed(GtkRange *r, App *a) {
    int v = (int)gtk_range_get_value(r);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", v);
    gtk_label_set_text(GTK_LABEL(a->mpi_val_label), buf);
}

void on_clear_log_clicked(GtkButton *b, App *a) {
    gtk_text_buffer_set_text(a->log_buffer, "", 0);
    a->log_count = 0; a->log_head = 0;
}

void on_fullscreen_toggle(GtkButton *b, App *a) {
    if (a->is_fullscreen) { gtk_window_unfullscreen(GTK_WINDOW(a->window)); a->is_fullscreen = 0; }
    else                  { gtk_window_fullscreen(GTK_WINDOW(a->window));   a->is_fullscreen = 1; }
}

/* ── IPC ─────────────────────────────────────────────────────────────────── */
static gboolean read_pipe_cb(GIOChannel *ch, GIOCondition cond, gpointer data) {
    App *a = (App *)data;

    if (cond & G_IO_HUP) {
        gchar *line = NULL; gsize len = 0;
        while (g_io_channel_read_line(ch, &line, &len, NULL, NULL) == G_IO_STATUS_NORMAL && line) {
            if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
            backend_parse_line(line, a);
            g_free(line); line = NULL;
        }
        g_io_channel_unref(ch);
        a->pipe_watch_id = 0;
        a->is_running    = 0;

        if (a->proc_pipe) {
            int status = pclose(a->proc_pipe);
            a->proc_pipe = NULL;
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) a->has_error = 1;
        }

        gtk_widget_set_sensitive(a->run_btn,   TRUE);
        gtk_widget_set_sensitive(a->bench_btn, TRUE);

        if (a->has_error) {
            gtk_label_set_markup(GTK_LABEL(a->status_lbl),
                "<span foreground='#ef4444' size='small'>Failed!</span>");
        } else {
            gtk_label_set_markup(GTK_LABEL(a->status_lbl),
                "<span foreground='#00e5a0' size='small'>Done!</span>");
            for (int i = 0; i < a->num_procs; i++) {
                a->procs[i].state    = PROC_DONE;
                a->procs[i].progress = 1.0;
            }
            if (a->split_area) gtk_widget_queue_draw(a->split_area);
        }

        if (a->has_error) {
            show_error_dialog(a,
                strlen(a->error_msg) > 0 ? a->error_msg : "An unknown error occurred.");
        } else if (strlen(a->input_path) > 0) {
            char derived[512];
            build_output_path(a->input_path, derived, a->encrypting);
            const char *popup_path = strlen(a->last_output_path) > 0
                                     ? a->last_output_path : derived;
            show_output_popup(a, popup_path, a->encrypting);
        }
        return FALSE;
    }

    gchar *line = NULL; gsize len = 0;
    GIOStatus st = g_io_channel_read_line(ch, &line, &len, NULL, NULL);
    if (st == G_IO_STATUS_NORMAL && line) {
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        backend_parse_line(line, a);
        g_free(line);
    }
    return TRUE;
}

static void launch_process(App *a, int bench) {
    const char *input = gtk_entry_get_text(GTK_ENTRY(a->input_entry));
    if (strlen(input) == 0) { show_error_dialog(a, "Please select an input file first."); return; }

    backend_parser_reset(a);
    a->is_bench     = bench;
    a->run_start_us = g_get_monotonic_time();
    g_strlcpy(a->last_output_path, "", sizeof(a->last_output_path));

    struct stat st;
    if (stat(input, &st) == 0) a->strategy.file_size = st.st_size;

    int mpi = (int)gtk_range_get_value(GTK_RANGE(a->mpi_scale));
    char encrypter[512]; get_encrypter_path(encrypter, sizeof(encrypter));
    const char *dest = gtk_entry_get_text(GTK_ENTRY(a->dest_entry));
    char dest_arg[600] = "";
    if (strlen(dest) > 0) snprintf(dest_arg, sizeof(dest_arg), "-o \"%s\"", dest);

    char cmd[2048];
    if (bench)
        snprintf(cmd, sizeof(cmd), "mpirun -np %d \"%s\" %s \"%s\" %s --bench --json 2>&1",
            mpi, encrypter, a->encrypting ? "-e" : "-d", input, dest_arg);
    else
        snprintf(cmd, sizeof(cmd), "mpirun -np %d \"%s\" %s \"%s\" %s --json 2>&1",
            mpi, encrypter, a->encrypting ? "-e" : "-d", input, dest_arg);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        show_error_dialog(a, "Failed to launch process. Check mpirun/encrypter path.");
        gtk_label_set_text(GTK_LABEL(a->status_lbl), "Failed to launch process");
        return;
    }

    int fd = fileno(pipe);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    GIOChannel *ch = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(ch, NULL, NULL);
    g_io_channel_set_buffered(ch, TRUE);

    a->proc_pipe  = pipe;
    a->is_running = 1;
    gtk_widget_set_sensitive(a->run_btn,   FALSE);
    gtk_widget_set_sensitive(a->bench_btn, FALSE);
    gtk_label_set_markup(GTK_LABEL(a->status_lbl),
        "<span foreground='#00d4ff' size='small'>Running…</span>");

    a->pipe_watch_id = g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, read_pipe_cb, a);
}

void on_run_clicked(GtkButton *b, App *a)   { launch_process(a, 0); }
void on_bench_clicked(GtkButton *b, App *a) { launch_process(a, 1); }