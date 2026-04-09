#pragma once
#ifndef APP_STATE_H
#define APP_STATE_H

#include <gtk/gtk.h>
#include <stdint.h>

/* ── colour palette macros (r,g,b) for cairo ── */
#define CLR_CYAN      0.000, 0.831, 1.000
#define CLR_PURPLE    0.486, 0.227, 0.929
#define CLR_MINT      0.000, 0.898, 0.627
#define CLR_AMBER     0.961, 0.620, 0.043
#define CLR_RED       0.941, 0.271, 0.271
#define CLR_WHITE     1.000, 1.000, 1.000
#define CLR_SLATE     0.392, 0.455, 0.549
#define CLR_BG_DEEP   0.039, 0.055, 0.102
#define CLR_BG_PANEL  0.059, 0.086, 0.157
#define CLR_BORDER    0.118, 0.176, 0.290

#define CLAMP_D(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))

#define MAX_MPI_PROCS    8
#define MAX_THREADS     64
#define MAX_LOG_LINES  512
#define MAX_PERF_HISTORY 64
#define JSON_BUF_SIZE  4096

/* ── log level ── */
typedef enum {
    LOG_INFO = 0,
    LOG_WARN,
    LOG_ERROR,
    LOG_OK,
    LOG_DEBUG
} LogLevel;

/* ── log ring-buffer entry ── */
typedef struct {
    LogLevel level;
    gint64   timestamp_us;
    char     text[256];
} LogEntry;

/* ── performance history sample ── */
typedef struct {
    double   serial_time;
    double   cpu_time;
    double   gpu_time;
    uint64_t file_size;
    gint64   timestamp_us;
} PerfSample;

/* ── MPI process state ── */
typedef enum {
    PROC_IDLE = 0,
    PROC_RECEIVED,
    PROC_PROCESSING,
    PROC_DONE
} ProcState;

/* ── thread state ── */
typedef enum {
    THREAD_IDLE = 0,
    THREAD_ACTIVE,
    THREAD_DONE
} ThreadState;

/* ── execution mode ── */
typedef enum {
    MODE_SERIAL = 0,
    MODE_MPI_OPENMP,
    MODE_MPI_OPENCL
} ExecMode;

/* ── strategy info ── */
typedef struct {
HEAD
    StrategyMode mode;
    char         mode_name[32];
    char         reason[256];           /* human-readable explanation        */
    int          gpu_detected;
    uint64_t     gpu_vram_bytes;
    int          cpu_cores;
    int          mpi_procs;
    int          omp_threads;
    uint64_t     file_size;
    uint64_t     chunk_size;
    int          num_chunks;

    ExecMode  mode;
    char      mode_name[32];
    char      reason[256];
    int       mpi_procs;
    int       omp_threads;
    int       gpu_detected;
    uint64_t  gpu_vram_bytes;
    uint64_t  file_size;
    uint64_t  chunk_size;
 d0f934b (UI rvamped)
} StrategyInfo;

/* ── per-process telemetry ── */
typedef struct {
    ProcState state;
    double    progress;
    double    progress_display;
    double    throughput_mbps;
    uint64_t  bytes_processed;
} ProcInfo;

/* ── memory telemetry ── */
typedef struct {
    uint64_t ram_total_kb;
    uint64_t ram_used_kb;
    double   ram_frac;
    uint64_t vram_total_bytes;
    uint64_t vram_used_bytes;
    double   vram_frac;
} MemInfo;

/* ═══════════════════════════════════════════════════════════════════════════
   App — central state struct
   ═══════════════════════════════════════════════════════════════════════════ */
typedef struct {
    /* ── window & chrome ── */
    GtkWidget *window;
    GtkWidget *header_bar;
    GtkWidget *notebook;

    /* ── left panel widgets ── */
    GtkWidget *input_entry;
    GtkWidget *dest_entry;
    GtkWidget *output_path_lbl;
    GtkWidget *copy_path_btn;
    GtkWidget *encrypt_btn;
    GtkWidget *decrypt_btn;
    GtkWidget *mpi_scale;
    GtkWidget *mpi_val_label;
    GtkWidget *strategy_label;
    GtkWidget *status_lbl;
    GtkWidget *run_btn;
    GtkWidget *bench_btn;

    /* ── real-time drawing areas ── */
    GtkWidget *split_area;
    GtkWidget *heatmap_area;
    GtkWidget *mem_gauge_area;
    GtkWidget *strategy_card;

    /* ── analysis tab ── */
    GtkWidget *graph_area;
    GtkWidget *matrix_area;
    GtkWidget *detail_filesize;
    GtkWidget *detail_chunks;
    GtkWidget *detail_threads;

    /* Time-comparison row widgets (dimmed until data arrives) */
    GtkWidget *serial_row_widget;
    GtkWidget *cpu_row_widget;
    GtkWidget *gpu_row_widget;

    GtkWidget *serial_bar;
    GtkWidget *cpu_bar;
    GtkWidget *gpu_bar;
    GtkWidget *serial_time_lbl;
    GtkWidget *cpu_time_lbl;
    GtkWidget *gpu_time_lbl;
    GtkWidget *serial_speedup_lbl;
    GtkWidget *cpu_speedup_lbl;
    GtkWidget *gpu_speedup_lbl;

    /* ── log tab ── */
    GtkWidget     *log_scrolled;
    GtkWidget     *log_textview;
    GtkTextBuffer *log_buffer;
    GtkTextTag    *tag_info, *tag_warn, *tag_error, *tag_ok, *tag_debug;
    int            log_count;
    int            log_head;
    LogEntry       log_entries[MAX_LOG_LINES];

    /* ── performance history ring buffer ── */
    PerfSample perf_history[MAX_PERF_HISTORY];
    int        perf_head;
    int        perf_count;

    /* ── JSON fragment accumulator ── */
    char json_buf[JSON_BUF_SIZE];
    int  json_buf_len;

 HEAD
    /* ── Runtime state ──────────────────────────────────────────────────── */
    int         encrypting;         /* 1=encrypt, 0=decrypt                  */
    int         is_running;
    int         is_bench;
    int         has_strategy;
    int         has_error;
    int         is_fullscreen;
    char        error_msg[512];
    char        input_path[512];
    char        last_output_path[512];

    /* ── per-process telemetry ── */
    ProcInfo procs[MAX_MPI_PROCS];
    int      num_procs;
 d0f934b (UI rvamped)

    /* ── per-thread telemetry ── */
    ThreadState thread_state[MAX_THREADS];
    gint64      thread_last_active[MAX_THREADS];
    double      thread_breathe[MAX_THREADS];
    int         num_threads;

    /* ── strategy & memory ── */
    StrategyInfo strategy;
    MemInfo      mem;

    /* ── timing data ── */
    double serial_time;
    double cpu_time;
    double gpu_time;

    /* ── run state ── */
    int     encrypting;
    int     is_running;
    int     is_bench;
    int     is_fullscreen;
    int     has_error;
    char    error_msg[512];
    char    input_path[512];
    char    last_output_path[512];
    FILE   *proc_pipe;
    guint   pipe_watch_id;
    gint64  run_start_us;

    /* ── animation ── */
    guint   anim_timer_id;
    int     anim_tick;
    double  global_phase;
} App;

#endif /* APP_STATE_H */