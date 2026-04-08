#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <libgen.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "app_state.h"

/* Forward declare from parser module */
void backend_parse_line(const char *line, App *a);
void backend_parser_reset(App *a);
void ui_start_animation(App *a);
void ui_stop_animation(App *a);

/* ── output path helper ── */
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
        else     strncat(bc, ".dec", sizeof(bc) - strlen(bc) - 1);
        g_snprintf(output, 1024, "%s/%s", dir, bc);
    }
}

static void show_error_dialog(App *a, const char *msg) {
    GtkWidget *dlg = gtk_message_dialog_new(
        GTK_WINDOW(a->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        NULL);
    
    gtk_window_set_title(GTK_WINDOW(dlg), "Operation Failed");
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dlg),
        "<b><span size='large' foreground='#ef4444'>An error occurred during processing</span></b>");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", msg);

    GtkStyleContext *sc = gtk_widget_get_style_context(dlg);
    gtk_style_context_add_class(sc, "error-dialog");

    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void show_output_popup(App *a, const char *path, int encrypted) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        encrypted ? "Encryption Complete" : "Decryption Complete",
        GTK_WINDOW(a->window), GTK_DIALOG_MODAL,
        "OK", GTK_RESPONSE_OK, NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    /* Force dark background */
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
    gtk_widget_set_halign(path_lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), path_lbl, FALSE, FALSE, 0);

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

void on_copy_path(GtkButton *b, gpointer d) {
    const char *p = (const char *)g_object_get_data(G_OBJECT(b), "path");
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), p, -1);
}

void on_browse(GtkButton *b, App *a) {
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Select File", GTK_WINDOW(a->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",   GTK_RESPONSE_ACCEPT, NULL);

    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), "/mnt/c/Users");

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_entry_set_text(GTK_ENTRY(a->input_entry), fn);
        g_strlcpy(a->input_path, fn, sizeof(a->input_path));
        char out[512];
        build_output_path(fn, out, a->encrypting);
        gchar *m = g_markup_printf_escaped(
            "<span foreground='#475569' size='small'>Output → </span>"
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

void on_browse_dest(GtkButton *b, App *a) {
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Select Destination File", GTK_WINDOW(a->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save",   GTK_RESPONSE_ACCEPT, NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    if (strlen(a->input_path) > 0) {
        char dir[512]; g_strlcpy(dir, a->input_path, sizeof(dir));
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), dirname(dir));
    }

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_entry_set_text(GTK_ENTRY(a->dest_entry), fn);
        g_free(fn);
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

/* Callback for when the clear button in the log tab is pressed */
void on_clear_log_clicked(GtkButton *b, App *a) {
    gtk_text_buffer_set_text(a->log_buffer, "", 0);
    a->log_count = 0;
    a->log_head = 0;
}

void on_fullscreen_toggle(GtkButton *b, App *a) {
    if (a->is_fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(a->window));
        a->is_fullscreen = 0;
    } else {
        gtk_window_fullscreen(GTK_WINDOW(a->window));
        a->is_fullscreen = 1;
    }
}

/* ── IPC and Process Execution ── */

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
        a->is_running = 0;

        if (a->proc_pipe) {
            int status = pclose(a->proc_pipe);
            a->proc_pipe = NULL;
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                a->has_error = 1;
            }
        }

        gtk_widget_set_sensitive(a->run_btn,   TRUE);
        gtk_widget_set_sensitive(a->bench_btn, TRUE);
        
        if (a->has_error) {
            gtk_label_set_markup(GTK_LABEL(a->status_lbl),
                "<span foreground='#ef4444' size='small'>Failed!</span>");
        } else {
            gtk_label_set_markup(GTK_LABEL(a->status_lbl),
                "<span foreground='#00e5a0' size='small'>Done!</span>");
            for(int i=0; i<a->num_procs; i++){
                a->procs[i].state = PROC_DONE;
                a->procs[i].progress = 1.0;
            }
            if (a->split_area) gtk_widget_queue_draw(a->split_area);
        }

        if (a->has_error) {
            show_error_dialog(a, strlen(a->error_msg) > 0 ? a->error_msg : "An unknown error occurred during execution.");
        } else if (strlen(a->input_path) > 0) {
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
        backend_parse_line(line, a);
        g_free(line);
    }
    return TRUE;
}

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

    backend_parser_reset(a);
    a->is_bench = bench;
    a->run_start_us = g_get_monotonic_time();
    g_strlcpy(a->last_output_path, "", sizeof(a->last_output_path));

    struct stat st;
    if (stat(input, &st) == 0) {
        a->strategy.file_size = st.st_size;
    }

    int mpi = (int)gtk_range_get_value(GTK_RANGE(a->mpi_scale));
    char cmd[2048];
    const char *encrypter = "./encrypter";
    const char *dest = gtk_entry_get_text(GTK_ENTRY(a->dest_entry));

    char dest_arg[600] = "";
    if (strlen(dest) > 0) snprintf(dest_arg, sizeof(dest_arg), "-o \"%s\"", dest);

    if (bench)
        snprintf(cmd, sizeof(cmd),
            "mpirun -np %d %s %s \"%s\" %s --bench --json 2>&1",
            mpi, encrypter, a->encrypting ? "-e" : "-d", input, dest_arg);
    else
        snprintf(cmd, sizeof(cmd),
            "mpirun -np %d %s %s \"%s\" %s --json 2>&1",
            mpi, encrypter, a->encrypting ? "-e" : "-d", input, dest_arg);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        show_error_dialog(a, "Failed to launch process. Check mpirun/encrypter availability.");
        gtk_label_set_text(GTK_LABEL(a->status_lbl), "Failed to launch process");
        return;
    }

    int fd = fileno(pipe);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    GIOChannel *ch = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(ch, NULL, NULL);
    g_io_channel_set_buffered(ch, TRUE); /* Required for g_io_channel_read_line to work reliably */

    a->proc_pipe = pipe;
    a->is_running = 1;

    gtk_widget_set_sensitive(a->run_btn, FALSE);
    gtk_widget_set_sensitive(a->bench_btn, FALSE);
    gtk_label_set_markup(GTK_LABEL(a->status_lbl),
        "<span foreground='#00d4ff' size='small'>Running...</span>");

    a->pipe_watch_id = g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, read_pipe_cb, a);
}

void on_run_clicked(GtkButton *b, App *a)   { launch_process(a, 0); }
void on_bench_clicked(GtkButton *b, App *a) { launch_process(a, 1); }
