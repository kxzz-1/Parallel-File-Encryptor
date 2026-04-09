/* ═══════════════════════════════════════════════════════════════════════════
   backend_parser.c  —  Stdout / JSON line parser
   YOUR original version + row-reveal calls for the Analysis tab dim/show logic
   ═══════════════════════════════════════════════════════════════════════════ */

#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <sys/stat.h>
#include "app_state.h"

/* ── forward declarations ────────────────────────────────────────────────── */
static void parse_json      (const char *json, App *a);
static void parse_legacy    (const char *line, App *a);
static void log_append      (App *a, const char *text, LogLevel level);
static void push_perf_sample(App *a);
static int  first_int       (const char *s, int *out);

/* declared in ui_callbacks.c */
void ui_reveal_time_row(App *a, GtkWidget *row);

/* ── helpers ─────────────────────────────────────────────────────────────── */
static int first_int(const char *s, int *out) {
    while (*s) {
        if (*s >= '0' && *s <= '9') { *out = atoi(s); return 1; }
        s++;
    }
    return 0;
}

static int json_str(const char *json, const char *key, char *out, int max) {
    char search[128]; snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return 0;
    int len = (int)(end - p);
    if (len >= max) len = max-1;
    memcpy(out, p, len); out[len] = '\0';
    return 1;
}

static int json_ull(const char *json, const char *key, unsigned long long *out) {
    char search[128]; snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') return 0;
    *out = strtoull(p, NULL, 10);
    return 1;
}

static int json_int(const char *json, const char *key, int *out) {
    char search[128]; snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ') p++;
    if ((*p < '0' || *p > '9') && *p != '-') return 0;
    *out = (int)strtol(p, NULL, 10);
    return 1;
}

static int json_dbl(const char *json, const char *key, double *out) {
    char search[128]; snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ') p++;
    char *endp;
    double v = strtod(p, &endp);
    if (endp == p) return 0;
    *out = v;
    return 1;
}

/* ── log terminal append ─────────────────────────────────────────────────── */
static void log_append(App *a, const char *text, LogLevel level) {
    if (!a->log_buffer) return;

    GtkTextTag *tag = NULL;
    switch (level) {
        case LOG_INFO:  tag = a->tag_info;  break;
        case LOG_WARN:  tag = a->tag_warn;  break;
        case LOG_ERROR: tag = a->tag_error; break;
        case LOG_OK:    tag = a->tag_ok;    break;
        default:        tag = a->tag_debug; break;
    }

    const char *prefix[] = {"[INFO]  ", "[WARN]  ", "[ERROR] ", "[OK]    ", "[DEBUG] "};
    const char *pfx = prefix[(int)level % 5];

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(a->log_buffer, &end);
    gtk_text_buffer_insert_with_tags(a->log_buffer, &end, pfx, -1, tag, NULL);
    gtk_text_buffer_get_end_iter(a->log_buffer, &end);
    gtk_text_buffer_insert_with_tags(a->log_buffer, &end, text, -1, tag, NULL);
    gtk_text_buffer_get_end_iter(a->log_buffer, &end);
    gtk_text_buffer_insert(a->log_buffer, &end, "\n", -1);

    if (a->log_scrolled) {
        GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(
            GTK_SCROLLED_WINDOW(a->log_scrolled));
        gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
    }

    LogEntry *e = &a->log_entries[a->log_head % MAX_LOG_LINES];
    e->level        = level;
    e->timestamp_us = g_get_monotonic_time();
    g_strlcpy(e->text, text, sizeof(e->text));
    a->log_head = (a->log_head + 1) % MAX_LOG_LINES;
    if (a->log_count < MAX_LOG_LINES) a->log_count++;
}

/* ── perf history push ───────────────────────────────────────────────────── */
static void push_perf_sample(App *a) {
    PerfSample *s = &a->perf_history[a->perf_head];
    s->serial_time  = a->serial_time;
    s->cpu_time     = a->cpu_time;
    s->gpu_time     = a->gpu_time;
    s->file_size    = a->strategy.file_size;
    s->timestamp_us = g_get_monotonic_time();
    a->perf_head    = (a->perf_head + 1) % MAX_PERF_HISTORY;
    if (a->perf_count < MAX_PERF_HISTORY) a->perf_count++;
}

/* ── update timing bars + REVEAL the appropriate row ────────────────────── */
static void update_time_labels(App *a) {
    if (!a->serial_bar) return;

    double maxT = fmax(a->serial_time, fmax(a->cpu_time, a->gpu_time));
    if (maxT <= 0) return;

    char buf[32];
    if (a->serial_time > 0) {
        snprintf(buf, sizeof(buf), "%.3fs", a->serial_time);
        gtk_label_set_text(GTK_LABEL(a->serial_time_lbl), buf);
        gtk_label_set_text(GTK_LABEL(a->serial_speedup_lbl), "baseline");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->serial_bar), 1.0);
        /* always show serial row as baseline once any timing arrives */
        ui_reveal_time_row(a, a->serial_row_widget);
    }
    if (a->cpu_time > 0 && a->cpu_bar) {
        snprintf(buf, sizeof(buf), "%.3fs", a->cpu_time);
        gtk_label_set_text(GTK_LABEL(a->cpu_time_lbl), buf);
        double frac = (a->serial_time > 0) ?
            a->cpu_time / a->serial_time : 0.6;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->cpu_bar),
            CLAMP_D(frac, 0.0, 1.0));
        if (a->serial_time > 0) {
            char sp[24]; snprintf(sp, sizeof(sp), "%.1fx faster",
                a->serial_time / a->cpu_time);
            gtk_label_set_text(GTK_LABEL(a->cpu_speedup_lbl), sp);
        }
        ui_reveal_time_row(a, a->cpu_row_widget);
    }
    if (a->gpu_time > 0 && a->gpu_bar) {
        snprintf(buf, sizeof(buf), "%.3fs", a->gpu_time);
        gtk_label_set_text(GTK_LABEL(a->gpu_time_lbl), buf);
        double frac = (a->serial_time > 0) ?
            a->gpu_time / a->serial_time : 0.3;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->gpu_bar),
            CLAMP_D(frac, 0.0, 1.0));
        if (a->serial_time > 0) {
            char sp[24]; snprintf(sp, sizeof(sp), "%.1fx faster",
                a->serial_time / a->gpu_time);
            gtk_label_set_text(GTK_LABEL(a->gpu_speedup_lbl), sp);
        }
        ui_reveal_time_row(a, a->gpu_row_widget);
    }

    if (a->graph_area)  gtk_widget_queue_draw(a->graph_area);
    if (a->matrix_area) gtk_widget_queue_draw(a->matrix_area);
}

/* ── update detail cards ─────────────────────────────────────────────────── */
static void set_card(GtkWidget *w, const char *val) {
    if (!w) return;
    char m[128];
    snprintf(m, sizeof(m),
        "<b><span foreground='#ffffff' size='large'>%s</span></b>", val);
    gtk_label_set_markup(GTK_LABEL(w), m);
}

/* ═══════════════════════════════════════════════════════════════════════════
   JSON PARSER  (unchanged from your version)
   ═══════════════════════════════════════════════════════════════════════════ */
static void parse_json(const char *json, App *a) {
    char status[64] = {0};
    json_str(json, "status", status, sizeof(status));

    if (strcmp(status, "starting") == 0) {
        unsigned long long total = 0;
        json_ull(json, "total", &total);
        a->strategy.file_size = (uint64_t)total;
        char buf[32];
        if (total > 1073741824ULL)   snprintf(buf, sizeof(buf), "%.1f GB", total/1073741824.0);
        else if (total > 1048576ULL) snprintf(buf, sizeof(buf), "%.1f MB", total/1048576.0);
        else if (total > 1024ULL)    snprintf(buf, sizeof(buf), "%.1f KB", total/1024.0);
        else                         snprintf(buf, sizeof(buf), "%llu B", total);
        set_card(a->detail_filesize, buf);
        char logmsg[128];
        snprintf(logmsg, sizeof(logmsg), "Starting: file size = %s", buf);
        log_append(a, logmsg, LOG_INFO);
        for (int i = 0; i < a->num_procs; i++)
            a->procs[i].state = PROC_RECEIVED;
        if (a->split_area) gtk_widget_queue_draw(a->split_area);
        return;
    }

    if (strcmp(status, "progress") == 0) {
        unsigned long long processed = 0;
        json_ull(json, "processed", &processed);
        double total = (double)(a->strategy.file_size > 0 ? a->strategy.file_size : 1);
        double frac  = (double)processed / total;
        for (int i = 0; i < a->num_procs; i++) {
            a->procs[i].state    = PROC_PROCESSING;
            a->procs[i].progress = CLAMP_D(frac, 0.0, 1.0);
        }
        char logmsg[128];
        snprintf(logmsg, sizeof(logmsg), "Progress: %llu / %llu bytes (%.1f%%)",
            (unsigned long long)processed,
            (unsigned long long)a->strategy.file_size, frac * 100.0);
        log_append(a, logmsg, LOG_DEBUG);
        if (a->status_lbl) {
            char s[64]; snprintf(s, sizeof(s), "Processing… %.1f%%", frac*100.0);
            gtk_label_set_text(GTK_LABEL(a->status_lbl), s);
        }
        return;
    }

    if (strcmp(status, "complete") == 0) {
        char output[512] = {0};
        json_str(json, "output", output, sizeof(output));
        if (strlen(output) > 0)
            g_strlcpy(a->last_output_path, output, sizeof(a->last_output_path));
        for (int i = 0; i < a->num_procs; i++) {
            a->procs[i].state    = PROC_DONE;
            a->procs[i].progress = 1.0;
        }
        char logmsg[600];
        snprintf(logmsg, sizeof(logmsg), "Complete → %s", output);
        log_append(a, logmsg, LOG_OK);
        if (a->split_area) gtk_widget_queue_draw(a->split_area);
        return;
    }

    if (strcmp(status, "error") == 0) {
        a->has_error = 1;
        char msg[512] = {0};
        json_str(json, "message", msg, sizeof(msg));
        g_strlcpy(a->error_msg, msg, sizeof(a->error_msg));
        log_append(a, msg, LOG_ERROR);
        return;
    }

    if (strcmp(status, "strategy") == 0) {
        char mode_name[64] = {0};
        json_str(json, "mode", mode_name, sizeof(mode_name));
        int mpi = 0, omp = 0, gpu = 0, chunks = 0;
        unsigned long long vram = 0, fsize = 0;
        json_int(json, "mpi_procs",   &mpi);
        json_int(json, "omp_threads", &omp);
        json_int(json, "num_chunks",  &chunks);
        json_int(json, "gpu",         &gpu);
        json_ull(json, "gpu_vram",    &vram);
        json_ull(json, "file_size",   &fsize);

        if (chunks > 0) a->strategy.num_chunks = chunks;
        if (mpi > 0) {
            a->strategy.mpi_procs = mpi;
            int capped = mpi > MAX_MPI_PROCS ? MAX_MPI_PROCS : mpi;
            if (capped != a->num_procs) {
                a->num_procs = capped;
                if (a->split_area) gtk_widget_queue_draw(a->split_area);
            }
            char buf[16]; snprintf(buf, sizeof(buf), "%d", mpi);
            set_card(a->detail_chunks, buf);
        }
        if (omp > 0) {
            a->strategy.omp_threads = omp;
            int capped = omp > MAX_THREADS ? MAX_THREADS : omp;
            if (capped != a->num_threads) {
                a->num_threads = capped;
                if (a->heatmap_area) gtk_widget_queue_draw(a->heatmap_area);
            }
            char buf[16]; snprintf(buf, sizeof(buf), "%d", omp);
            set_card(a->detail_threads, buf);
        }
        if (gpu >= 0) {
            a->strategy.gpu_detected   = gpu;
            a->strategy.gpu_vram_bytes = (uint64_t)vram;
            a->mem.vram_total_bytes    = (uint64_t)vram;
        }
        if (fsize > 0) {
            a->strategy.file_size = (uint64_t)fsize;
            char buf[32];
            if      (fsize > 1073741824ULL) snprintf(buf, sizeof(buf), "%.1f GB", fsize/1073741824.0);
            else if (fsize > 1048576ULL)    snprintf(buf, sizeof(buf), "%.1f MB", fsize/1048576.0);
            else if (fsize > 1024ULL)       snprintf(buf, sizeof(buf), "%.1f KB", fsize/1024.0);
            else                            snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)fsize);
            set_card(a->detail_filesize, buf);
        }
 HEAD

        /* Determine strategy mode enum and reason */
        a->strategy.mode = MODE_MPI_OPENMP;

 d0f934b (UI rvamped)
        if (strstr(mode_name, "Serial") || strstr(mode_name, "serial")) {
            a->strategy.mode = MODE_SERIAL;
            g_strlcpy(a->strategy.reason,
                "File < 1 MB.\nMPI overhead not worth it.\nRunning serial AES-CTR.", 255);
        } else if (strstr(mode_name, "OpenCL") || strstr(mode_name, "GPU") || strstr(mode_name, "gpu")) {
            a->strategy.mode = MODE_MPI_OPENCL;
            g_strlcpy(a->strategy.reason,
                "Large dataset found.\nOffloading AES-CTR to GPU via OpenCL.\nMPI workload distribution enabled.", 255);
        } else {
            g_strlcpy(a->strategy.reason,
                "Medium dataset detected.\nUsing MPI distribution + OpenMP.\nFull CPU acceleration active.", 255);
        }
 HEAD
        g_strlcpy(a->strategy.mode_name, mode_name, sizeof(a->strategy.mode_name));
        
        a->has_strategy = 1;
        if (a->strategy_label) gtk_label_set_text(GTK_LABEL(a->strategy_label), mode_name);
        if (a->strategy_card)  gtk_widget_queue_draw(a->strategy_card);
        if (a->heatmap_area)   gtk_widget_queue_draw(a->heatmap_area);
        if (a->split_area)     gtk_widget_queue_draw(a->split_area);

        char logmsg[256];
        snprintf(logmsg, sizeof(logmsg),
            "Strategy: %s | MPI=%d OMP=%d Chunks=%d",
            mode_name, mpi, omp, chunks);

        if (a->strategy_label)
            gtk_label_set_text(GTK_LABEL(a->strategy_label), mode_name);
        if (a->strategy_card) gtk_widget_queue_draw(a->strategy_card);
        if (a->heatmap_area)  gtk_widget_queue_draw(a->heatmap_area);
        char logmsg[256];
        snprintf(logmsg, sizeof(logmsg),
            "Strategy: %s | MPI=%d OMP=%d GPU=%s VRAM=%.0f MB",
            mode_name, mpi, omp, gpu ? "YES" : "NO", (double)vram / 1048576.0);
 d0f934b (UI rvamped)
        log_append(a, logmsg, LOG_OK);
        return;
    }

 HEAD
    /* ── {"status":"rank_state","rank":N,"state":"..."} ── */
    if (strcmp(status, "rank_state") == 0) {
        int r = -1;
        char state[32] = {0};
        json_int(json, "rank", &r);
        json_str(json, "state", state, sizeof(state));
        if (r >= 0 && r < MAX_MPI_PROCS) {
            if      (strcmp(state, "received") == 0) a->procs[r].state = PROC_RECEIVED;
            else if (strcmp(state, "working")  == 0) a->procs[r].state = PROC_PROCESSING;
            else if (strcmp(state, "done")     == 0) a->procs[r].state = PROC_DONE;
            if (a->split_area) gtk_widget_queue_draw(a->split_area);
        }
        return;
    }

    /* ── {"status":"rank_progress","rank":N,"progress":F} ── */
    if (strcmp(status, "rank_progress") == 0) {
        int r = -1;
        double f = 0.0;
        json_int(json, "rank", &r);
        json_dbl(json, "progress", &f);
        if (r >= 0 && r < MAX_MPI_PROCS) {
            a->procs[r].progress = CLAMP_D(f, 0.0, 1.0);
            if (a->split_area) gtk_widget_queue_draw(a->split_area);
        }
        return;
    }

    /* ── {"status":"thread", "rank":N, "tid":N, "state":"active|done"} ── */

 d0f934b (UI rvamped)
    if (strcmp(status, "thread") == 0) {
        char state[16] = {0};
        int rank = -1, tid = -1;
        json_int(json, "rank", &rank);
        json_int(json, "tid",  &tid);
        json_str(json, "state", state, sizeof(state));
        int idx = (tid >= 0) ? tid : rank;
        if (idx >= 0 && idx < MAX_THREADS) {
            if (strcmp(state, "done") == 0)
                a->thread_state[idx] = THREAD_DONE;
            else if (strcmp(state, "idle") == 0)
                a->thread_state[idx] = THREAD_IDLE;
            else {
                a->thread_state[idx]       = THREAD_ACTIVE;
                a->thread_last_active[idx] = g_get_monotonic_time();
            }
            if (a->heatmap_area) gtk_widget_queue_draw(a->heatmap_area);
        }
        return;
    }

    if (strcmp(status, "timing") == 0) {
        char mode_name[32] = {0};
        double t = 0.0;
        json_str(json, "mode", mode_name, sizeof(mode_name));
        json_dbl(json, "time", &t);
        if (t > 0) {
            if (strstr(mode_name, "serial") && a->serial_time == 0.0)
                a->serial_time = t;
            else if ((strstr(mode_name, "cpu") || strstr(mode_name, "openmp")) && a->cpu_time == 0.0)
                a->cpu_time = t;
            else if ((strstr(mode_name, "gpu") || strstr(mode_name, "opencl")) && a->gpu_time == 0.0)
                a->gpu_time = t;
            update_time_labels(a);
            push_perf_sample(a);
            char logmsg[128];
            snprintf(logmsg, sizeof(logmsg), "Timing [%s]: %.4fs", mode_name, t);
            log_append(a, logmsg, LOG_INFO);
            if (a->notebook)
                gtk_notebook_set_current_page(GTK_NOTEBOOK(a->notebook), 1);
        }
        return;
    }

    if (strncmp(status, "bench_", 6) == 0) {
        char time_str[64] = {0};
        json_str(json, "time", time_str, sizeof(time_str));
        double t = atof(time_str);
        if (strstr(status, "serial") && a->serial_time == 0.0 && t > 0) a->serial_time = t;
        else if (strstr(status, "cpu") && a->cpu_time == 0.0 && t > 0) a->cpu_time = t;
        else if ((strstr(status, "gpu") || strstr(status, "opencl")) && a->gpu_time == 0.0 && t > 0) a->gpu_time = t;
        update_time_labels(a);
        push_perf_sample(a);
        char logmsg[128];
        snprintf(logmsg, sizeof(logmsg), "Bench [%s]: %.4fs", status+6, t);
        log_append(a, logmsg, LOG_INFO);
        if (a->notebook)
            gtk_notebook_set_current_page(GTK_NOTEBOOK(a->notebook), 1);
        return;
    }

    log_append(a, json, LOG_DEBUG);
}

/* ═══════════════════════════════════════════════════════════════════════════
   LEGACY PARSER  (unchanged from your version)
   ═══════════════════════════════════════════════════════════════════════════ */
static void parse_legacy(const char *line, App *a) {
    if (a->status_lbl) gtk_label_set_text(GTK_LABEL(a->status_lbl), line);

    LogLevel level = LOG_INFO;
    if (strcasestr(line, "[ERROR]") || strcasestr(line, "error") || strcasestr(line, "fail"))
        level = LOG_ERROR;
    else if (strcasestr(line, "[WARN]") || strcasestr(line, "warn"))
        level = LOG_WARN;
    else if (strcasestr(line, "[OK]") || strcasestr(line, "complete") || strcasestr(line, "done"))
        level = LOG_OK;
    else if (strcasestr(line, "[DEBUG]"))
        level = LOG_DEBUG;
    log_append(a, line, level);

    int n = 0;

    if ((strcasestr(line, "mode") || strcasestr(line, "strategy")) && strcasestr(line, "selected")) {
        const char *colon = strrchr(line, ':');
        if (colon) {
            const char *val = colon+1;
            while (*val == ' ') val++;
            if (a->strategy_label) gtk_label_set_text(GTK_LABEL(a->strategy_label), val);
            g_strlcpy(a->strategy.mode_name, val, sizeof(a->strategy.mode_name));
        }
    }

    if (strcasestr(line, "mode selected") || strcasestr(line, "selected:")) {
        if (strcasestr(line, "serial"))
            g_strlcpy(a->strategy.reason,
                "File < 1 MB.\nMPI overhead not worth it.\nRunning serial AES-CTR.", 255);
        else if (strcasestr(line, "openmp") || strcasestr(line, "cpu"))
            g_strlcpy(a->strategy.reason,
                "File 1 MB – 1 GB.\nUsing MPI + OpenMP\nfor CPU parallelism.", 255);
        else if (strcasestr(line, "opencl") || strcasestr(line, "gpu"))
            g_strlcpy(a->strategy.reason,
                "File > 1 GB + GPU detected.\nUsing MPI + OpenCL\nfor maximum throughput.", 255);
        if (a->strategy_card) gtk_widget_queue_draw(a->strategy_card);
    }

    if (strcasestr(line, "file size") || strcasestr(line, "filesize")) {
        unsigned long long fs = 0;
        sscanf(line, "%*[^0123456789]%llu", &fs);
        if (fs > 0) {
            a->strategy.file_size = (uint64_t)fs;
            char buf[32];
            if      (fs > 1073741824ULL) snprintf(buf, sizeof(buf), "%.1f GB", fs/1073741824.0);
            else if (fs > 1048576ULL)    snprintf(buf, sizeof(buf), "%.1f MB", fs/1048576.0);
            else if (fs > 1024ULL)       snprintf(buf, sizeof(buf), "%.1f KB", fs/1024.0);
            else                         snprintf(buf, sizeof(buf), "%llu B",  fs);
            set_card(a->detail_filesize, buf);
        }
    }

    if (strcasestr(line, "chunk") || strcasestr(line, "processes")) {
        if (a->has_strategy) return; // JSON strategy takes precedence
        const char *after_colon = strchr(line, ':');
        if (first_int(after_colon ? after_colon+1 : line, &n) && n > 0 && n <= 64) {
            char buf[8]; snprintf(buf, sizeof(buf), "%d", n);
            set_card(a->detail_chunks, buf);
            int capped = n > MAX_MPI_PROCS ? MAX_MPI_PROCS : n;
            if (capped != a->num_procs) {
                a->num_procs = capped;
                a->strategy.mpi_procs = n;
                if (a->split_area) gtk_widget_queue_draw(a->split_area);
            }
        }
    }

    if (strcasestr(line, "thread") &&
HEAD
        (strcasestr(line,"per")||strcasestr(line,"/proc")||
         strcasestr(line,"count")||strcasestr(line,"num")||
         strcasestr(line,"using")||strcasestr(line,"spawn"))) {
        if (a->has_strategy && a->strategy.omp_threads > 0) return; // Keep global strategy

        (strcasestr(line,"per")||strcasestr(line,"/proc")||strcasestr(line,"count")||
         strcasestr(line,"num")||strcasestr(line,"using")||strcasestr(line,"spawn"))) {
 d0f934b (UI rvamped)
        const char *colon = strchr(line, ':');
        if (first_int(colon ? colon+1 : line, &n) && n > 0 && n <= 64) {
            a->num_threads = n > MAX_THREADS ? MAX_THREADS : n;
            a->strategy.omp_threads = n;
            char buf[8]; snprintf(buf, sizeof(buf), "%d", n);
            set_card(a->detail_threads, buf);
            if (a->heatmap_area) gtk_widget_queue_draw(a->heatmap_area);
        }
    }

    if (strcasestr(line, "gpu") && strcasestr(line, "detect")) {
        a->strategy.gpu_detected = !strcasestr(line, "not") && !strcasestr(line, "no gpu");
        unsigned long long mem = 0;
        if (sscanf(line, "%*[^0123456789]%llu", &mem) == 1 && mem > 1024)
            a->strategy.gpu_vram_bytes = mem;
        a->mem.vram_total_bytes = a->strategy.gpu_vram_bytes;
    }

    if (strcasestr(line, "process")) {
        const char *p = strcasestr(line, "process");
        if (p) { p += 7; while(*p==' ') p++; }
        int pid = -1;
        if (p && *p>='0' && *p<='9') pid = atoi(p);
        if (pid >= 0 && pid < MAX_MPI_PROCS) {
            if (strcasestr(line, "receiv") || strcasestr(line, "got chunk"))
                a->procs[pid].state = PROC_RECEIVED;
            else if (strcasestr(line,"encrypt")||strcasestr(line,"decrypt")||
                     strcasestr(line,"process")||strcasestr(line,"work")||strcasestr(line,"start"))
                { if (a->procs[pid].state < PROC_PROCESSING) a->procs[pid].state = PROC_PROCESSING; }
            else if (strcasestr(line,"done")||strcasestr(line,"finish")||
                     strcasestr(line,"complet")||strcasestr(line,"written")||strcasestr(line,"sent")) {
                a->procs[pid].state    = PROC_DONE;
                a->procs[pid].progress = 1.0;
                for (int i = 0; i < MAX_THREADS; i++) a->thread_state[i] = THREAD_IDLE;
            }
            if (a->split_area) gtk_widget_queue_draw(a->split_area);
        }
    }

    if (strcasestr(line, "thread") &&
        !(strcasestr(line,"per")||strcasestr(line,"/proc")||strcasestr(line,"count")||
          strcasestr(line,"num")||strcasestr(line,"using")||strcasestr(line,"spawn"))) {
        const char *p = strcasestr(line, "thread");
        if (p) { p += 6; while(*p==' ') p++; }
        int tid = -1;
        if (p && *p>='0' && *p<='9') tid = atoi(p);
        if (tid >= 0 && tid < MAX_THREADS) {
            if (strcasestr(line,"done")||strcasestr(line,"finish")||strcasestr(line,"complet"))
                a->thread_state[tid] = THREAD_DONE;
            else {
                a->thread_state[tid]       = THREAD_ACTIVE;
                a->thread_last_active[tid] = g_get_monotonic_time();
            }
            if (a->heatmap_area) gtk_widget_queue_draw(a->heatmap_area);
        }
    }

    if ((level == LOG_ERROR) && !a->has_error) {
        a->has_error = 1;
        const char *msg = strcasestr(line, "[ERROR]");
        if (msg) msg += 7; else msg = line;
        while (*msg==' '||*msg==':'||*msg=='[') msg++;
        g_strlcpy(a->error_msg, msg, sizeof(a->error_msg));
    }

    const char *slash = strchr(line, '/');
    if (slash && (strcasestr(line,"written")||strcasestr(line,"output")||
                  strcasestr(line,"saved")||strcasestr(line,"encrypt")||
                  strcasestr(line,"decrypt")||strstr(line,".enc"))) {
        char path[512]; g_strlcpy(path, slash, sizeof(path));
        int plen = strlen(path);
        while (plen>0 && (path[plen-1]=='\n'||path[plen-1]==' '||
                          path[plen-1]=='\r'||path[plen-1]=='\t'))
            path[--plen]='\0';
        if (plen > 2) g_strlcpy(a->last_output_path, path, sizeof(a->last_output_path));
    }

    int is_serial = (strcasestr(line,"serial") && !strcasestr(line,"parallel"));
    int is_cpu    = (strcasestr(line,"openmp")||strcasestr(line,"omp")||strcasestr(line,"cpu"))
                    && !strcasestr(line,"serial");
    int is_gpu    = (strcasestr(line,"opencl")||strcasestr(line,"gpu")||strcasestr(line,"cl"));
    int is_timing = (strcasestr(line,"done")||strcasestr(line,"time")||
                     strcasestr(line,"elapsed")||strcasestr(line,"took")||
                     strcasestr(line,"bench")||strcasestr(line,"result"));

    if (is_timing) {
        double val = 0.0, v2 = 0.0;
        if (sscanf(line,"[BENCH] Serial done: %lf",       &v2)==1) { val=v2; is_serial=1; is_cpu=0; is_gpu=0; }
        if (sscanf(line,"[BENCH] MPI + OpenMP done: %lf", &v2)==1) { val=v2; is_cpu=1; is_serial=0; is_gpu=0; }
        if (sscanf(line,"[BENCH] MPI + OpenCL done: %lf", &v2)==1) { val=v2; is_gpu=1; is_serial=0; is_cpu=0; }
        if (sscanf(line,"[INFO] Elapsed: %lf",             &v2)==1) { val=v2; }
        if (sscanf(line,"[OK] Done in %lf",                &v2)==1) { val=v2; }
        if (sscanf(line,"Time: %lf",                       &v2)==1) { val=v2; }
        if (sscanf(line,"Elapsed: %lf",                    &v2)==1) { val=v2; }
        if (sscanf(line,"[OK] Encryption complete. Elapsed: %lf",&v2)==1) { val=v2; }
        if (sscanf(line,"[OK] Decryption complete. Elapsed: %lf",&v2)==1) { val=v2; }

        if (val == 0.0) {
            const char *colon = strrchr(line, ':');
            const char *eq    = strrchr(line, '=');
            const char *start = colon > eq ? colon : eq;
            if (start) { char *endp; double tv = strtod(start+1, &endp); if (endp!=start+1) val=tv; }
        }

        if (val > 0.0) {
            if      (is_serial && a->serial_time == 0.0) a->serial_time = val;
            else if (is_cpu    && a->cpu_time    == 0.0) a->cpu_time    = val;
            else if (is_gpu    && a->gpu_time    == 0.0) a->gpu_time    = val;
            else if (!is_serial && !is_cpu && !is_gpu && a->cpu_time == 0.0) a->cpu_time = val;
            update_time_labels(a);
            push_perf_sample(a);
            if (a->notebook) gtk_notebook_set_current_page(GTK_NOTEBOOK(a->notebook), 1);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   PUBLIC ENTRY — backend_parse_line
   ═══════════════════════════════════════════════════════════════════════════ */
void backend_parse_line(const char *line, App *a) {
    if (line[0] == '{') {
        if (strchr(line, '}')) {
            parse_json(line, a);
        } else {
            a->json_buf_len = 0;
            int room = JSON_BUF_SIZE - 1;
            int len  = strlen(line);
            if (len > room) len = room;
            memcpy(a->json_buf, line, len);
            a->json_buf_len = len;
            a->json_buf[a->json_buf_len] = '\0';
        }
        return;
    }

    if (a->json_buf_len > 0) {
        int room = JSON_BUF_SIZE - a->json_buf_len - 1;
        int len  = strlen(line);
        if (len > room) len = room;
        memcpy(a->json_buf + a->json_buf_len, line, len);
        a->json_buf_len += len;
        a->json_buf[a->json_buf_len] = '\0';
        if (strchr(a->json_buf, '}')) {
            parse_json(a->json_buf, a);
            a->json_buf_len = 0;
            a->json_buf[0]  = '\0';
        }
        return;
    }

    parse_legacy(line, a);
}

/* ── backend_parser_reset ────────────────────────────────────────────────── */
void backend_parser_reset(App *a) {
    if (!a) return;
    a->has_strategy = 0;
    a->json_buf_len = 0;
    a->json_buf[0]  = '\0';
    a->has_error    = 0;
    memset(a->error_msg, 0, sizeof(a->error_msg));

    for (int i = 0; i < MAX_MPI_PROCS; i++) {
        a->procs[i].state            = PROC_IDLE;
        a->procs[i].progress         = 0.0;
        a->procs[i].progress_display = 0.0;
        a->procs[i].throughput_mbps  = 0.0;
        a->procs[i].bytes_processed  = 0;
    }
    for (int i = 0; i < MAX_THREADS; i++) {
        a->thread_state[i]       = THREAD_IDLE;
        a->thread_last_active[i] = 0;
        a->thread_breathe[i]     = 0.0;
    }

    a->serial_time  = 0.0;
    a->cpu_time     = 0.0;
    a->gpu_time     = 0.0;
    a->run_start_us = g_get_monotonic_time();

    if (a->serial_bar) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->serial_bar), 0.0);
    if (a->cpu_bar)    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->cpu_bar),    0.0);
    if (a->gpu_bar)    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->gpu_bar),    0.0);
    if (a->serial_time_lbl)    gtk_label_set_text(GTK_LABEL(a->serial_time_lbl),    "--");
    if (a->cpu_time_lbl)       gtk_label_set_text(GTK_LABEL(a->cpu_time_lbl),       "--");
    if (a->gpu_time_lbl)       gtk_label_set_text(GTK_LABEL(a->gpu_time_lbl),       "--");
    if (a->serial_speedup_lbl) gtk_label_set_text(GTK_LABEL(a->serial_speedup_lbl), "--");
    if (a->cpu_speedup_lbl)    gtk_label_set_text(GTK_LABEL(a->cpu_speedup_lbl),    "--");
    if (a->gpu_speedup_lbl)    gtk_label_set_text(GTK_LABEL(a->gpu_speedup_lbl),    "--");

    /* dim all time rows back */
    GtkWidget *rows[3] = { a->serial_row_widget, a->cpu_row_widget, a->gpu_row_widget };
    for (int i = 0; i < 3; i++) {
        if (!rows[i]) continue;
        GtkStyleContext *sc = gtk_widget_get_style_context(rows[i]);
        gtk_style_context_remove_class(sc, "time-row-visible");
        gtk_style_context_add_class(sc, "time-row-hidden");
    }

    set_card(a->detail_filesize, "--");
    set_card(a->detail_chunks,   "--");
    set_card(a->detail_threads,  "--");

    if (a->split_area)     gtk_widget_queue_draw(a->split_area);
    if (a->heatmap_area)   gtk_widget_queue_draw(a->heatmap_area);
    if (a->graph_area)     gtk_widget_queue_draw(a->graph_area);
    if (a->matrix_area)    gtk_widget_queue_draw(a->matrix_area);
    if (a->strategy_card)  gtk_widget_queue_draw(a->strategy_card);
    if (a->mem_gauge_area) gtk_widget_queue_draw(a->mem_gauge_area);

    if (a->status_lbl)
        gtk_label_set_markup(GTK_LABEL(a->status_lbl),
            "<span foreground='#334155' size='small' font_family='monospace'>Ready</span>");
}