#ifndef APP_STATE_H
#define APP_STATE_H

/* ═══════════════════════════════════════════════════════════════════════════
   app_state.h  — Shared application state for the Parallel File Encrypter
   Cyberpunk/Glassmorphism Dashboard (GTK3)
   ═══════════════════════════════════════════════════════════════════════════ */

#include <gtk/gtk.h>
#include <stdint.h>

/* ── Capacity constants ─────────────────────────────────────────────────── */
#define MAX_MPI_PROCS        8
#define MAX_THREADS          32
#define MAX_LOG_LINES        512
#define MAX_PERF_HISTORY     256   /* data points kept for the trend graph   */
#define JSON_BUF_SIZE        4096  /* accumulator for partial JSON fragments  */

/* ── Colour tokens (match CSS variables) ───────────────────────────────── */
/* used by Cairo drawing code for consistent palette */
#define CLR_BG_DEEP          0.039, 0.055, 0.102   /* #0a0e1a               */
#define CLR_BG_PANEL         0.059, 0.086, 0.157   /* #0f1628               */
#define CLR_BORDER           0.118, 0.176, 0.290   /* #1e2d4a               */
#define CLR_CYAN             0.000, 0.831, 1.000   /* #00d4ff               */
#define CLR_PURPLE           0.486, 0.227, 0.929   /* #7c3aed               */
#define CLR_MINT             0.000, 0.898, 0.627   /* #00e5a0               */
#define CLR_AMBER            0.961, 0.620, 0.043   /* #f59e0b               */
#define CLR_RED              0.941, 0.271, 0.271   /* #ef4444               */
#define CLR_SLATE            0.392, 0.455, 0.549   /* #64748b               */
#define CLR_WHITE            1.000, 1.000, 1.000
#define CLR_TRANSPARENT      0.000, 0.000, 0.000, 0.000

/* ── Process state enum ─────────────────────────────────────────────────── */
typedef enum {
    PROC_IDLE       = 0,
    PROC_RECEIVED   = 1,
    PROC_PROCESSING = 2,
    PROC_DONE       = 3
} ProcState;

/* ── Thread state enum ──────────────────────────────────────────────────── */
typedef enum {
    THREAD_IDLE   = 0,
    THREAD_ACTIVE = 1,
    THREAD_DONE   = 2
} ThreadState;

/* ── Strategy mode (mirrors backend) ───────────────────────────────────── */
typedef enum {
    MODE_SERIAL      = 0,
    MODE_MPI_OPENMP  = 1,
    MODE_MPI_OPENCL  = 2
} StrategyMode;

/* ── A single log entry ─────────────────────────────────────────────────── */
typedef enum { LOG_INFO, LOG_WARN, LOG_ERROR, LOG_OK, LOG_DEBUG } LogLevel;

typedef struct {
    LogLevel  level;
    char      text[256];
    gint64    timestamp_us;
} LogEntry;

/* ── Per-process node ───────────────────────────────────────────────────── */
typedef struct {
    ProcState state;
    double    progress;          /* smooth 0.0–1.0 target                   */
    double    progress_display;  /* smoothly interpolated value              */
    double    throughput_mbps;   /* MB/s reported or estimated               */
    uint64_t  bytes_processed;
} ProcNode;

/* ── Performance history (ring buffer) ─────────────────────────────────── */
typedef struct {
    double serial_time;
    double cpu_time;
    double gpu_time;
    uint64_t file_size;
    gint64   timestamp_us;
} PerfSample;

/* ── Strategy recommendation card data ─────────────────────────────────── */
typedef struct {
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
} StrategyInfo;

/* ── Memory telemetry ───────────────────────────────────────────────────── */
typedef struct {
    uint64_t ram_total_kb;
    uint64_t ram_used_kb;
    uint64_t vram_total_bytes;
    uint64_t vram_used_bytes;      /* estimated from strategy                */
    double   ram_frac;             /* 0.0–1.0                                */
    double   vram_frac;
} MemTelemetry;

/* ══════════════════════════════════════════════════════════════════════════
   Master App State
   ══════════════════════════════════════════════════════════════════════════ */
typedef struct {

    /* ── GTK widgets (top-level) ─────────────────────────────────────────── */
    GtkWidget  *window;
    GtkWidget  *notebook;
    GtkWidget  *header_bar;

    /* ── Left panel widgets ─────────────────────────────────────────────── */
    GtkWidget  *input_entry;
    GtkWidget  *dest_entry;
    GtkWidget  *encrypt_btn;
    GtkWidget  *decrypt_btn;
    GtkWidget  *run_btn;
    GtkWidget  *bench_btn;
    GtkWidget  *mpi_scale;
    GtkWidget  *mpi_val_label;
    GtkWidget  *status_lbl;
    GtkWidget  *output_path_lbl;
    GtkWidget  *copy_path_btn;

    /* ── Drawing areas (Tab 1 — Real-time) ─────────────────────────────── */
    GtkWidget  *split_area;        /* file split / MPI node diagram          */
    GtkWidget  *heatmap_area;      /* thread heatmap grid                    */
    GtkWidget  *mem_gauge_area;    /* RAM + VRAM gauges                      */
    GtkWidget  *strategy_card;     /* strategy recommendation panel          */

    /* ── Drawing areas (Tab 2 — Analysis) ──────────────────────────────── */
    GtkWidget  *graph_area;        /* speedup / efficiency frontier graph    */
    GtkWidget  *matrix_area;       /* time vs dataset-size performance matrix*/

    /* ── Log terminal (Tab 3) ───────────────────────────────────────────── */
    GtkWidget  *log_textview;
    GtkTextBuffer *log_buffer;
    GtkTextTag *tag_info, *tag_warn, *tag_error, *tag_ok, *tag_debug;
    GtkWidget  *log_scrolled;

    /* ── Benchmark progress bars (Tab 2) ───────────────────────────────── */
    GtkWidget  *serial_bar;
    GtkWidget  *cpu_bar;
    GtkWidget  *gpu_bar;
    GtkWidget  *serial_time_lbl;
    GtkWidget  *cpu_time_lbl;
    GtkWidget  *gpu_time_lbl;
    GtkWidget  *serial_speedup_lbl;
    GtkWidget  *cpu_speedup_lbl;
    GtkWidget  *gpu_speedup_lbl;

    /* ── Detail cards ───────────────────────────────────────────────────── */
    GtkWidget  *detail_filesize;
    GtkWidget  *detail_chunks;
    GtkWidget  *detail_threads;
    GtkWidget  *strategy_label;     /* short mode label in left panel        */

    /* ── Runtime state ──────────────────────────────────────────────────── */
    int         encrypting;         /* 1=encrypt, 0=decrypt                  */
    int         is_running;
    int         is_bench;
    int         has_error;
    int         is_fullscreen;
    char        error_msg[512];
    char        input_path[512];
    char        last_output_path[512];

    /* ── MPI process nodes ──────────────────────────────────────────────── */
    ProcNode    procs[MAX_MPI_PROCS];
    int         num_procs;

    /* ── OpenMP thread states ───────────────────────────────────────────── */
    ThreadState thread_state[MAX_THREADS];
    gint64      thread_last_active[MAX_THREADS];
    double      thread_breathe[MAX_THREADS];   /* 0.0–1.0 idle breath phase  */
    int         num_threads;

    /* ── Strategy info ──────────────────────────────────────────────────── */
    StrategyInfo strategy;

    /* ── Benchmark timing ───────────────────────────────────────────────── */
    double      serial_time;
    double      cpu_time;
    double      gpu_time;
    gint64      run_start_us;

    /* ── Performance history ring buffer ───────────────────────────────── */
    PerfSample  perf_history[MAX_PERF_HISTORY];
    int         perf_head;          /* next write index                      */
    int         perf_count;         /* how many valid entries                */

    /* ── Memory telemetry ───────────────────────────────────────────────── */
    MemTelemetry mem;

    /* ── Log entries ────────────────────────────────────────────────────── */
    LogEntry    log_entries[MAX_LOG_LINES];
    int         log_head;
    int         log_count;

    /* ── Animation ──────────────────────────────────────────────────────── */
    guint       anim_timer_id;      /* 60 FPS timer handle                   */
    int         anim_tick;          /* monotonically increasing counter      */
    double      global_phase;       /* 0.0–2π wave phase for idle effects    */

    /* ── IPC ────────────────────────────────────────────────────────────── */
    FILE       *proc_pipe;
    guint       pipe_watch_id;

    /* ── JSON partial-fragment accumulator ─────────────────────────────── */
    char        json_buf[JSON_BUF_SIZE];
    int         json_buf_len;

} App;

/* ── Helper macros ──────────────────────────────────────────────────────── */

/* Set RGBA from CLR_* token (comma-separated triplet) */
#define CAIRO_SET_RGB(cr, r, g, b)        cairo_set_source_rgb((cr),(r),(g),(b))
#define CAIRO_SET_RGBA(cr, r, g, b, a)    cairo_set_source_rgba((cr),(r),(g),(b),(a))

/* Clamp a double to [lo, hi] */
#define CLAMP_D(v, lo, hi)  ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

/* Smooth-step interpolation */
#define SMOOTHSTEP(t)  ((t)*(t)*(3.0-2.0*(t)))

/* Linear interpolation */
#define LERP(a, b, t)  ((a) + ((b)-(a))*(t))

#endif /* APP_STATE_H */
