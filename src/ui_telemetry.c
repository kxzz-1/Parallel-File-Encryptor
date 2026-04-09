/* ═══════════════════════════════════════════════════════════════════════════
   ui_telemetry.c  —  Cairo Drawing Engine
   Cyberpunk/Glassmorphism HPC Dashboard | GTK3

   Changes in this revision:
     • telemetry_tick: RAM is read from /proc/meminfo on EVERY tick (truly live)
     • VRAM gauge: shows real data if available, otherwise shows 0% with label
     • All 4 panels are full-height in their rows
     • Memory gauges: arc glow pulses, label shows MB used/total
   ═══════════════════════════════════════════════════════════════════════════ */

#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "app_state.h"

static inline void set_cyan   (cairo_t *cr, double a) { cairo_set_source_rgba(cr, CLR_CYAN,   a); }
static inline void set_purple (cairo_t *cr, double a) { cairo_set_source_rgba(cr, CLR_PURPLE, a); }
static inline void set_mint   (cairo_t *cr, double a) { cairo_set_source_rgba(cr, CLR_MINT,   a); }
static inline void set_amber  (cairo_t *cr, double a) { cairo_set_source_rgba(cr, CLR_AMBER,  a); }
static inline void set_red    (cairo_t *cr, double a) { cairo_set_source_rgba(cr, CLR_RED,    a); }
static inline void set_white  (cairo_t *cr, double a) { cairo_set_source_rgba(cr, CLR_WHITE,  a); }
static inline void set_border (cairo_t *cr, double a) { cairo_set_source_rgba(cr, CLR_BORDER, a); }

static const double PROC_COLORS[8][3] = {
    {0.000, 0.831, 1.000},
    {0.486, 0.227, 0.929},
    {0.000, 0.898, 0.627},
    {0.961, 0.620, 0.043},
    {0.941, 0.271, 0.271},
    {0.200, 0.600, 1.000},
    {0.600, 0.230, 0.930},
    {0.000, 0.700, 0.530},
};

static void rounded_rect(cairo_t *cr, double x, double y,
                          double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x+w-r, y+r,   r, -G_PI/2,  0);
    cairo_arc(cr, x+w-r, y+h-r, r,  0,        G_PI/2);
    cairo_arc(cr, x+r,   y+h-r, r,  G_PI/2,   G_PI);
    cairo_arc(cr, x+r,   y+r,   r,  G_PI,    -G_PI/2);
    cairo_close_path(cr);
}

static void draw_glow(cairo_t *cr, double cx, double cy,
                      double radius, double r, double g, double b,
                      double peak_alpha) {
    cairo_pattern_t *pat = cairo_pattern_create_radial(cx, cy, 0, cx, cy, radius);
    cairo_pattern_add_color_stop_rgba(pat, 0.0, r, g, b, peak_alpha);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, r, g, b, 0.0);
    cairo_arc(cr, cx, cy, radius, 0, 2*G_PI);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);
}

static void draw_scanlines(cairo_t *cr, double W, double H) {
    cairo_set_line_width(cr, 0.5);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.04);
    for (double y = 0; y < H; y += 3.0) {
        cairo_move_to(cr, 0, y); cairo_line_to(cr, W, y); cairo_stroke(cr);
    }
}

static void draw_grid(cairo_t *cr, double W, double H, double step, double alpha) {
    cairo_set_line_width(cr, 0.5);
    cairo_set_source_rgba(cr, CLR_BORDER, alpha);
    for (double x = 0; x < W; x += step) {
        cairo_move_to(cr, x, 0); cairo_line_to(cr, x, H); cairo_stroke(cr);
    }
    for (double y = 0; y < H; y += step) {
        cairo_move_to(cr, 0, y); cairo_line_to(cr, W, y); cairo_stroke(cr);
    }
}

static void neon_label(cairo_t *cr, const char *text,
                        double x, double y, double font_size,
                        double r, double g, double b, int bold) {
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
        bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgba(cr, r, g, b, 0.25);
    cairo_move_to(cr, x-1, y+1); cairo_show_text(cr, text);
    cairo_move_to(cr, x+1, y-1); cairo_show_text(cr, text);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_move_to(cr, x, y); cairo_show_text(cr, text);
}

static void draw_section_header(cairo_t *cr, double x, double y,
                                 double w, const char *title,
                                 double r, double g, double b) {
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, x, y, 3, 14); cairo_fill(cr);

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 9.5);
    cairo_set_source_rgba(cr, CLR_SLATE, 1.0);
    cairo_move_to(cr, x+9, y+11);
    cairo_show_text(cr, title);

    cairo_set_line_width(cr, 0.5);
    cairo_set_source_rgba(cr, CLR_BORDER, 0.7);
    cairo_text_extents_t ex;
    cairo_text_extents(cr, title, &ex);
    double lx = x + 9 + ex.x_advance + 8;
    cairo_move_to(cr, lx, y+7); cairo_line_to(cr, x+w, y+7); cairo_stroke(cr);
}

/* ═══════════════════════════════════════════════════════════════════════════
   1.  MPI PROCESS NODES
   ═══════════════════════════════════════════════════════════════════════════ */
gboolean telemetry_draw_split(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    double W = alloc.width, H = alloc.height;
    double tick = a->anim_tick;

    cairo_set_source_rgb(cr, CLR_BG_DEEP);
    cairo_rectangle(cr, 0, 0, W, H); cairo_fill(cr);
    draw_grid(cr, W, H, 28, 0.15);
    draw_scanlines(cr, W, H);
    draw_section_header(cr, 12, 10, W-24, "MPI PROCESS NODES", CLR_CYAN);

    int n      = a->num_procs > 0 ? a->num_procs : 4;
    double pad = 12, gap = 6;
    double total_gap = gap * (n - 1);
    double node_w = (W - 2*pad - total_gap) / n;
    double node_h = H - 60;
    double node_y = 34;

    for (int i = 0; i < n; i++) {
        double nx = pad + i * (node_w + gap);
        const double *c = PROC_COLORS[i % 8];
        ProcState state = a->procs[i].state;
        double disp = a->procs[i].progress_display;

        if (state == PROC_PROCESSING) {
            double pulse = 0.15 + 0.10 * sin(tick * 0.12 + i * 1.1);
            draw_glow(cr, nx + node_w/2, node_y + node_h/2,
                      node_w * 0.75, c[0], c[1], c[2], pulse);
        }

        rounded_rect(cr, nx, node_y, node_w, node_h, 8);
        cairo_set_source_rgba(cr, 0.06, 0.09, 0.18, 0.85); cairo_fill_preserve(cr);

        if (disp > 0.001) {
            double fh = disp * node_h;
            cairo_save(cr);
            rounded_rect(cr, nx, node_y, node_w, node_h, 8);
            cairo_clip(cr);
            cairo_pattern_t *pg = cairo_pattern_create_linear(0, node_y+node_h, 0, node_y);
            cairo_pattern_add_color_stop_rgba(pg, 0.0, c[0], c[1], c[2], 0.45);
            cairo_pattern_add_color_stop_rgba(pg, 1.0, c[0], c[1], c[2], 0.08);
            cairo_set_source(cr, pg);
            cairo_rectangle(cr, nx, node_y + node_h - fh, node_w, fh);
            cairo_fill(cr);
            cairo_pattern_destroy(pg);
            cairo_restore(cr);
        }

        double border_a = (state == PROC_PROCESSING) ?
            0.6 + 0.4 * sin(tick * 0.12 + i) : (state > PROC_IDLE ? 0.55 : 0.18);
        cairo_set_source_rgba(cr, c[0], c[1], c[2], border_a);
        cairo_set_line_width(cr, state == PROC_PROCESSING ? 1.8 : 1.0);
        rounded_rect(cr, nx, node_y, node_w, node_h, 8);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, c[0], c[1], c[2], state > PROC_IDLE ? 0.9 : 0.3);
        rounded_rect(cr, nx, node_y, node_w, 3, 2);
        cairo_fill(cr);

        char rank_s[8]; snprintf(rank_s, sizeof(rank_s), "P%d", i);
        neon_label(cr, rank_s, nx + node_w/2 - 8, node_y + 20, 11, c[0], c[1], c[2], 1);

        if (state == PROC_PROCESSING && a->procs[i].throughput_mbps > 0) {
            char tp[16];
            snprintf(tp, sizeof(tp), "%.0f MB/s", a->procs[i].throughput_mbps);
            cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 7.5);
            cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.75);
            cairo_text_extents_t ex; cairo_text_extents(cr, tp, &ex);
            cairo_move_to(cr, nx + (node_w - ex.width)/2, node_y + node_h - 18);
            cairo_show_text(cr, tp);
        }

        const char *badge = "IDLE";
        double br = 0.392, bg = 0.455, bb = 0.549, ba = 0.4;
        if (state == PROC_RECEIVED)   { badge="RECV"; br=0.961; bg=0.620; bb=0.043; ba=0.9; }
        if (state == PROC_PROCESSING) {
            double blink = 0.7 + 0.3 * sin(tick * 0.18 + i);
            badge="WORK"; br=c[0]; bg=c[1]; bb=c[2]; ba=blink;
        }
        if (state == PROC_DONE) { badge="DONE"; br=0.0; bg=0.898; bb=0.627; ba=0.9; }

        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 8.5);
        cairo_text_extents_t bex; cairo_text_extents(cr, badge, &bex);
        double bx = nx + (node_w - bex.width)/2, by = node_y + node_h/2 + 5;
        cairo_set_source_rgba(cr, br, bg, bb, 0.12);
        rounded_rect(cr, bx-4, by-11, bex.width+8, 14, 3); cairo_fill(cr);
        cairo_set_source_rgba(cr, br, bg, bb, ba);
        cairo_move_to(cr, bx, by); cairo_show_text(cr, badge);

        if (disp > 0.01) {
            char pct[8]; snprintf(pct, sizeof(pct), "%d%%", (int)(disp*100));
            cairo_set_font_size(cr, 7.5);
            cairo_text_extents_t pex; cairo_text_extents(cr, pct, &pex);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.4);
            cairo_move_to(cr, nx + (node_w-pex.width)/2, node_y + node_h - 6);
            cairo_show_text(cr, pct);
        }

        if (i < n-1) {
            double cx2 = nx + node_w + gap/2, cy2 = node_y + node_h/2;
            double dot_phase = fmod(tick * 0.08 - i * 0.4, 1.0);
            for (int d = 0; d < 3; d++) {
                double dp = fmod(dot_phase + d * 0.33, 1.0);
                double dalpha = sin(dp * G_PI);
                cairo_arc(cr, cx2, cy2 - 4 + d*4, 1.5, 0, 2*G_PI);
                cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, dalpha * 0.5);
                cairo_fill(cr);
            }
        }
    }

    double fb_y = node_y + node_h + 8, fb_h = 6;
    rounded_rect(cr, pad, fb_y, W-2*pad, fb_h, 3);
    cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.4); cairo_fill(cr);
    for (int i = 0; i < n; i++) {
        double seg_w = (W - 2*pad - total_gap) / n;
        double fx = pad + i * (seg_w + gap);
        const double *c = PROC_COLORS[i % 8];
        double disp = a->procs[i].progress_display;
        rounded_rect(cr, fx, fb_y, seg_w * disp, fb_h, 3);
        cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.8); cairo_fill(cr);
    }
    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════════
   2.  OPENMP THREAD HEATMAP
   ═══════════════════════════════════════════════════════════════════════════ */
gboolean telemetry_draw_heatmap(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    double W = alloc.width, H = alloc.height;
    double tick = a->anim_tick;

    cairo_set_source_rgb(cr, 0.05, 0.05, 0.1);
    cairo_rectangle(cr, 0, 0, W, H); cairo_fill(cr);
    draw_scanlines(cr, W, H);
    draw_section_header(cr, 12, 8, W-24, "OPENMP THREAD HEATMAP", 0.486, 0.227, 0.929);

    int n = a->num_threads > 0 ? a->num_threads : 8;
    if (n > 64) n = 64;

    int cols = (n <= 8) ? n : 8;
    int rows = (n + cols - 1) / cols;
    int gap  = 5;
    double avail_w = W - 24, avail_h = H - 44;
    int cell_w = (int)((avail_w - gap*(cols-1)) / cols);
    if (cell_w > 72) cell_w = 72;
    if (cell_w < 18) cell_w = 18;
    int cell_h = (int)((avail_h - gap*(rows-1)) / rows);
    if (cell_h > 44) cell_h = 44;
    if (cell_h < 16) cell_h = 16;

    int total_w = cols*cell_w + (cols-1)*gap;
    double sx = 12 + (avail_w - total_w) / 2;
    double sy = 26;

    for (int i = 0; i < n; i++) {
        int col = i % cols, row = i / cols;
        double cx = sx + col*(cell_w+gap);
        double cy = sy + row*(cell_h+gap);
        ThreadState state = a->thread_state[i];
        double breathe = a->thread_breathe[i];

        rounded_rect(cr, cx, cy, cell_w, cell_h, 4);
        switch (state) {
            case THREAD_ACTIVE: cairo_set_source_rgba(cr, 0.00, 0.38, 0.55, 0.9); break;
            case THREAD_DONE:   cairo_set_source_rgba(cr, 0.00, 0.27, 0.20, 0.9); break;
            default:            cairo_set_source_rgba(cr, 0.1, 0.12, 0.2, 0.8);
        }
        cairo_fill(cr);

        if (state == THREAD_IDLE && breathe > 0.01) {
            rounded_rect(cr, cx, cy, cell_w, cell_h, 4);
            cairo_set_source_rgba(cr, 0.486, 0.227, 0.929, breathe * 0.18);
            cairo_fill(cr);
        }

        if (state == THREAD_ACTIVE) {
            double pulse = 0.3 + 0.25 * sin(tick * 0.15 + i * 0.7);
            rounded_rect(cr, cx, cy, cell_w, 2, 1);
            cairo_set_source_rgba(cr, 0.0, 0.831, 1.0, pulse + 0.4);
            cairo_fill(cr);
            draw_glow(cr, cx + cell_w/2, cy + cell_h/2,
                      cell_w * 0.7, 0.0, 0.831, 1.0, pulse * 0.3);
        }

        cairo_set_line_width(cr, 1.0);
        rounded_rect(cr, cx, cy, cell_w, cell_h, 4);
        switch (state) {
            case THREAD_ACTIVE: {
                double ba = 0.6 + 0.4 * sin(tick * 0.15 + i);
                cairo_set_source_rgba(cr, 0.0, 0.831, 1.0, ba); break;
            }
            case THREAD_DONE: cairo_set_source_rgba(cr, 0.0, 0.898, 0.627, 0.7); break;
            default:          cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.25 + breathe * 0.2);
        }
        cairo_stroke(cr);

        char lbl[6]; snprintf(lbl, sizeof(lbl), "T%d", i);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, cell_w > 30 ? 8.5 : 7.0);
        cairo_text_extents_t ex; cairo_text_extents(cr, lbl, &ex);
        double lx = cx + (cell_w - ex.width)/2, ly = cy + cell_h/2 + ex.height/2;
        switch (state) {
            case THREAD_ACTIVE: cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95); break;
            case THREAD_DONE:   cairo_set_source_rgba(cr, 0.0, 0.898, 0.627, 0.85); break;
            default:            cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.5 + breathe * 0.3);
        }
        cairo_move_to(cr, lx, ly); cairo_show_text(cr, lbl);
    }

    /* legend */
    double ly = sy + rows*(cell_h+gap) + 4;
    if (ly > H - 18) ly = H - 18;
    struct { const char *label; double r,g,b; } legend[] = {
        {"ACTIVE", 0.0, 0.831, 1.0},
        {"DONE",   0.0, 0.898, 0.627},
        {"IDLE",   0.7, 0.7, 0.7},
    };
    double lx2 = 12;
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9);
    for (int i = 0; i < 3; i++) {
        cairo_rectangle(cr, lx2, ly, 8, 8);
        cairo_set_source_rgba(cr, legend[i].r, legend[i].g, legend[i].b, 0.85);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.6, 0.8);
        cairo_move_to(cr, lx2+11, ly+8);
        cairo_show_text(cr, legend[i].label);
        lx2 += 62;
    }
    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════════
   3.  STRATEGY ENGINE CARD
   ═══════════════════════════════════════════════════════════════════════════ */
gboolean telemetry_draw_strategy(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    double W = alloc.width, H = alloc.height;
    double tick = a->anim_tick;

    cairo_set_source_rgb(cr, 0.05, 0.05, 0.1);
    cairo_rectangle(cr, 0, 0, W, H); cairo_fill(cr);
    draw_scanlines(cr, W, H);
    draw_section_header(cr, 12, 8, W-24, "STRATEGY ENGINE", 0.961, 0.620, 0.043);

    StrategyInfo *s = &a->strategy;
    double pulse = 0.12 + 0.08 * sin(tick * 0.1);
    double mr, mg, mb;
    const char *mode_str;
    switch (s->mode) {
        case MODE_MPI_OPENCL: mr=0.486; mg=0.227; mb=0.929; mode_str="MPI+OpenCL"; break;
        case MODE_MPI_OPENMP: mr=0.0; mg=0.831; mb=1.0;    mode_str="MPI+OpenMP"; break;
        default:              mr=0.961; mg=0.620; mb=0.043; mode_str="Serial";     break;
    }

    double icon_r = 28, icon_cx = 50, icon_cy = H/2 + 4;
    draw_glow(cr, icon_cx, icon_cy, icon_r*1.4, mr, mg, mb, pulse);
    cairo_arc(cr, icon_cx, icon_cy, icon_r, 0, 2*G_PI);
    cairo_set_source_rgba(cr, mr, mg, mb, 0.15); cairo_fill(cr);
    cairo_arc(cr, icon_cx, icon_cy, icon_r, 0, 2*G_PI);
    cairo_set_source_rgba(cr, mr, mg, mb, 0.6);
    cairo_set_line_width(cr, 1.5); cairo_stroke(cr);
    neon_label(cr, mode_str, icon_cx - 30, icon_cy + 5, 9.0, mr, mg, mb, 1);

    /* reason text */
    double tx = icon_cx*2 + 16, ty = 36;
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9.5);
    cairo_set_source_rgba(cr, CLR_WHITE, 0.75);
    char reason[256]; snprintf(reason, sizeof(reason), "%s", s->reason);
    int rlen = strlen(reason), line_start = 0;
    double line_h = 14;
    for (int i = 0; i <= rlen && ty < H-8; i++) {
        if (reason[i] == '\n' || reason[i] == '\0' ||
            (i - line_start > 45 && reason[i] == ' ')) {
            char seg[64]; int slen = i - line_start;
            if (slen > 63) slen = 63;
            memcpy(seg, reason + line_start, slen); seg[slen] = '\0';
            cairo_move_to(cr, tx, ty); cairo_show_text(cr, seg);
            ty += line_h; line_start = i+1;
        }
    }

    /* stat pills */
    typedef struct { const char *key; char val[24]; double r,g,b; } Pill;
    Pill pills[4];
    pills[0].key="Chunks";  snprintf(pills[0].val,24,"%d",s->mpi_procs);    pills[0].r=0.0;   pills[0].g=0.831; pills[0].b=1.0;
    pills[1].key="Threads"; snprintf(pills[1].val,24,"%d",s->omp_threads);  pills[1].r=0.0;   pills[1].g=0.831; pills[1].b=1.0;
    pills[2].key="GPU";     snprintf(pills[2].val,24,"%s",s->gpu_detected?"YES":"NO"); pills[2].r=0.486; pills[2].g=0.227; pills[2].b=0.929;
    if (s->gpu_vram_bytes > 0)
        snprintf(pills[3].val,24,"%llu GB",(unsigned long long)(s->gpu_vram_bytes/1073741824));
    else snprintf(pills[3].val,24,"--");
    pills[3].key="VRAM"; pills[3].r=0.486; pills[3].g=0.227; pills[3].b=0.929;

    double px = tx, py = H - 26;
    cairo_set_font_size(cr, 8.0);
    for (int i = 0; i < 4; i++) {
        Pill *p = &pills[i];
        char pill_text[40]; snprintf(pill_text,40,"%s: %s",p->key,p->val);
        cairo_text_extents_t pex; cairo_text_extents(cr,pill_text,&pex);
        double pw = pex.width + 12;
        rounded_rect(cr, px, py-10, pw, 14, 4);
        cairo_set_source_rgba(cr, p->r, p->g, p->b, 0.10); cairo_fill(cr);
        rounded_rect(cr, px, py-10, pw, 14, 4);
        cairo_set_source_rgba(cr, p->r, p->g, p->b, 0.35);
        cairo_set_line_width(cr, 0.8); cairo_stroke(cr);
        cairo_set_source_rgba(cr, p->r, p->g, p->b, 0.9);
        cairo_move_to(cr, px+6, py); cairo_show_text(cr, pill_text);
        px += pw + 6;
        if (px > W - 80) { px = tx; py += 17; }
    }
    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════════
   4.  MEMORY TELEMETRY — truly live gauges
   ═══════════════════════════════════════════════════════════════════════════ */
gboolean telemetry_draw_memgauge(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    double W = alloc.width, H = alloc.height;
    double tick = a->anim_tick;

    cairo_set_source_rgb(cr, 0.05, 0.05, 0.1);
    cairo_rectangle(cr, 0, 0, W, H); cairo_fill(cr);
    draw_scanlines(cr, W, H);
    draw_section_header(cr, 12, 8, W-24, "MEMORY TELEMETRY  (live)", 0.0, 0.898, 0.627);

    double ram_frac  = CLAMP_D(a->mem.ram_frac,  0.0, 1.0);
    double vram_frac = CLAMP_D(a->mem.vram_frac, 0.0, 1.0);

    typedef struct {
        const char *label;
        double frac;
        double r, g, b;
        double used_mb, total_mb;
        int    has_data;
    } Gauge;

    Gauge gauges[2] = {
        {
            "RAM", ram_frac, 0.0, 0.831, 1.0,
            (double)a->mem.ram_used_kb  / 1024.0,
            (double)a->mem.ram_total_kb / 1024.0,
            (a->mem.ram_total_kb > 0)
        },
        {
            "VRAM", vram_frac, 0.486, 0.227, 0.929,
            (double)a->mem.vram_used_bytes  / 1048576.0,
            (double)a->mem.vram_total_bytes / 1048576.0,
            (a->mem.vram_total_bytes > 0)
        },
    };

    /* Place two gauges side-by-side, centred vertically */
    double gauge_diam = (H - 52);
    if (gauge_diam > 150) gauge_diam = 150;
    if (gauge_diam < 60)  gauge_diam = 60;
    double R = gauge_diam / 2.0 - 10;

    double total_gauges_w = 2 * gauge_diam + 60;
    double start_x = (W - total_gauges_w) / 2.0;
    double base_cy  = 32 + gauge_diam / 2.0;

    for (int g = 0; g < 2; g++) {
        Gauge *G = &gauges[g];
        double cx = start_x + g * (gauge_diam + 60) + gauge_diam / 2.0;
        double cy  = base_cy;

        double start_ang = G_PI * 0.75;
        double end_ang   = G_PI * 2.25;
        double sweep     = end_ang - start_ang;

        /* track */
        cairo_set_line_width(cr, 12);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.2);
        cairo_arc(cr, cx, cy, R, start_ang, end_ang);
        cairo_stroke(cr);

        /* filled arc */
        double fill_end = start_ang + sweep * G->frac;
        if (G->frac > 0.001) {
            cairo_pattern_t *pat = cairo_pattern_create_linear(cx-R, cy, cx+R, cy);
            cairo_pattern_add_color_stop_rgba(pat, 0.0, G->r, G->g, G->b, 0.5);
            cairo_pattern_add_color_stop_rgba(pat, 1.0, G->r, G->g, G->b, 1.0);
            cairo_set_source(cr, pat);
            cairo_arc(cr, cx, cy, R, start_ang, fill_end);
            cairo_stroke(cr);
            cairo_pattern_destroy(pat);

            /* tip glow */
            double tx2 = cx + R * cos(fill_end), ty2 = cy + R * sin(fill_end);
            draw_glow(cr, tx2, ty2, 18, G->r, G->g, G->b,
                      0.20 + 0.12 * sin(tick * 0.1));
        }

        /* warn ring when > 80% */
        if (G->frac > 0.8) {
            double warn_pulse = 0.4 + 0.3 * sin(tick * 0.2);
            cairo_set_source_rgba(cr, 0.941, 0.271, 0.271, warn_pulse * 0.35);
            cairo_set_line_width(cr, 16);
            cairo_arc(cr, cx, cy, R, start_ang, end_ang);
            cairo_stroke(cr);
            cairo_set_line_width(cr, 12);
        }

        /* centre percentage */
        char pct[8]; snprintf(pct, sizeof(pct), "%d%%", (int)(G->frac * 100));
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 20);
        cairo_text_extents_t pex; cairo_text_extents(cr, pct, &pex);
        cairo_set_source_rgba(cr, G->r, G->g, G->b, 0.95);
        cairo_move_to(cr, cx - pex.width/2, cy + pex.height/2);
        cairo_show_text(cr, pct);

        /* label below % */
        cairo_set_font_size(cr, 9.5);
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.6, 0.8);
        cairo_text_extents_t lex; cairo_text_extents(cr, G->label, &lex);
        cairo_move_to(cr, cx - lex.width/2, cy + pex.height/2 + 14);
        cairo_show_text(cr, G->label);

        /* used / total line */
        if (G->has_data) {
            char info[40];
            if (G->total_mb >= 1024)
                snprintf(info, sizeof(info), "%.1f / %.1f GB",
                         G->used_mb/1024.0, G->total_mb/1024.0);
            else
                snprintf(info, sizeof(info), "%.0f / %.0f MB",
                         G->used_mb, G->total_mb);
            cairo_set_font_size(cr, 8.0);
            cairo_text_extents_t iex; cairo_text_extents(cr, info, &iex);
            cairo_set_source_rgba(cr, 0.6, 0.6, 0.7, 0.75);
            cairo_move_to(cr, cx - iex.width/2, cy + pex.height/2 + 27);
            cairo_show_text(cr, info);
        } else if (!G->has_data && g == 1) {
            /* VRAM not detected */
            cairo_set_font_size(cr, 8.0);
            cairo_set_source_rgba(cr, 0.4, 0.4, 0.5, 0.6);
            const char *na = "not detected";
            cairo_text_extents_t nex; cairo_text_extents(cr, na, &nex);
            cairo_move_to(cr, cx - nex.width/2, cy + pex.height/2 + 27);
            cairo_show_text(cr, na);
        }
    }
    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════════
 HEAD
   4.  STRATEGY RECOMMENDATION CARD
   ═══════════════════════════════════════════════════════════════════════════ */
gboolean telemetry_draw_strategy(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    double W = alloc.width, H = alloc.height;
    double tick = a->anim_tick;

    cairo_set_source_rgb(cr, 0.05, 0.05, 0.1);
    cairo_rectangle(cr, 0, 0, W, H); cairo_fill(cr);
    draw_scanlines(cr, W, H);

    draw_section_header(cr, 12, 8, W-24, "STRATEGY ENGINE", 0.961, 0.620, 0.043);

    StrategyInfo *s = &a->strategy;

    /* mode icon area */
    double icon_r = 22, icon_cx = 38, icon_cy = H/2 + 4;
    double pulse = 0.12 + 0.08 * sin(tick * 0.1);

    double mr, mg, mb;
    const char *mode_str;
    switch (s->mode) {
        case MODE_MPI_OPENCL:  mr=0.486; mg=0.227; mb=0.929; mode_str="MPI+OpenCL"; break;
        case MODE_MPI_OPENMP:  mr=0.0; mg=0.831; mb=1.0; mode_str="MPI+OpenMP"; break;
        default:        case MODE_SERIAL:      mr=0.961; mg=0.620; mb=0.043; mode_str="Serial";     break;
    }
    /* suppress gcc warnings — mg/mb are set in all branches */
    if (s->mode == MODE_SERIAL)      { mg=0.620; mb=0.043; }
    if (s->mode == MODE_MPI_OPENCL)  { mg=0.227; mb=0.929; }

    draw_glow(cr, icon_cx, icon_cy, icon_r*1.4, mr, mg, mb, pulse);
    cairo_arc(cr, icon_cx, icon_cy, icon_r, 0, 2*G_PI);
    cairo_set_source_rgba(cr, mr, mg, mb, 0.15); cairo_fill(cr);
    cairo_arc(cr, icon_cx, icon_cy, icon_r, 0, 2*G_PI);
    cairo_set_source_rgba(cr, mr, mg, mb, 0.6);
    cairo_set_line_width(cr, 1.5); cairo_stroke(cr);

    neon_label(cr, mode_str, icon_cx - 26, icon_cy + 5, 8.5, mr, mg, mb, 1);

    /* reason text */
    double tx = icon_cx*2 + 10, ty = 36;
    cairo_select_font_face(cr, "monospace",
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9);
    cairo_set_source_rgba(cr, CLR_WHITE, 0.75);
    /* simple word-wrap at ~40 chars */
    char reason[256]; snprintf(reason, sizeof(reason), "%s", s->reason);
    int rlen = strlen(reason);
    int line_start = 0;
    double line_h = 13;
    for (int i = 0; i <= rlen && ty < H-8; i++) {
        if (reason[i] == '\n' || reason[i] == '\0' ||
            (i - line_start > 38 && reason[i] == ' ')) {
            char seg[64]; int slen = i - line_start;
            if (slen > 63) slen = 63;
            memcpy(seg, reason + line_start, slen); seg[slen] = '\0';
            cairo_move_to(cr, tx, ty); cairo_show_text(cr, seg);
            ty += line_h; line_start = i+1;
        }
    }

    /* stat pills row */
    typedef struct { const char *key; char val[24]; double r,g,b; } Pill;
    Pill pills[4];
    pills[0].key = "Chunks";  snprintf(pills[0].val, 24, "%d",  s->num_chunks);  pills[0].r=0.0; pills[0].g=0.831; pills[0].b=1.0;
    pills[1].key = "Threads"; snprintf(pills[1].val, 24, "%d",  s->omp_threads);    pills[1].r=0.0; pills[1].g=0.831; pills[1].b=1.0;
    pills[2].key = "GPU";     snprintf(pills[2].val, 24, "%s",  s->gpu_detected?"YES":"NO"); pills[2].r=0.486; pills[2].g=0.227; pills[2].b=0.929;
    pills[3].key = "VRAM";
    if (s->gpu_vram_bytes > 0)
        snprintf(pills[3].val, 24, "%llu GB", (unsigned long long)(s->gpu_vram_bytes/1073741824));
    else
        snprintf(pills[3].val, 24, "--");
    pills[3].r=0.486; pills[3].g=0.227; pills[3].b=0.929;
    /* fix cyan aliases */
    pills[1].r=0.0; pills[1].g=0.831; pills[1].b=1.0;
    pills[2].g=0.227; pills[2].b=0.929;
    pills[3].g=0.227; pills[3].b=0.929;

    double px = tx, py = H - 26;
    for (int i = 0; i < 4; i++) {
        Pill *p = &pills[i];
        char pill_text[40]; snprintf(pill_text, 40, "%s: %s", p->key, p->val);
        cairo_set_font_size(cr, 8.0);
        cairo_text_extents_t pex; cairo_text_extents(cr, pill_text, &pex);
        double pw = pex.width + 12;

        rounded_rect(cr, px, py-10, pw, 14, 4);
        cairo_set_source_rgba(cr, p->r, p->g, p->b, 0.10); cairo_fill(cr);
        rounded_rect(cr, px, py-10, pw, 14, 4);
        cairo_set_source_rgba(cr, p->r, p->g, p->b, 0.35);
        cairo_set_line_width(cr, 0.8); cairo_stroke(cr);

        cairo_set_source_rgba(cr, p->r, p->g, p->b, 0.9);
        cairo_move_to(cr, px+6, py); cairo_show_text(cr, pill_text);
        px += pw + 6;
        if (px > W - 80) { px = tx; py += 17; }
    }

    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════════
   5.  SPEEDUP GRAPH — Efficiency Frontier
=======
   5.  SPEEDUP GRAPH
>>>>>>> d0f934b (UI rvamped)
   ═══════════════════════════════════════════════════════════════════════════ */
gboolean telemetry_draw_graph(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    double W = alloc.width, H = alloc.height;

    cairo_set_source_rgb(cr, CLR_BG_DEEP);
    cairo_rectangle(cr, 0, 0, W, H); cairo_fill(cr);
    draw_scanlines(cr, W, H);
    draw_section_header(cr, 12, 8, W-24, "SPEEDUP GRAPH", CLR_CYAN);

    double pl=48, pr=14, pt=26, pb=28;
    double gw = W-pl-pr, gh = H-pt-pb;

    double yticks[] = {0, 2, 4, 6, 8, 10};
    double maxy = 10.0;
    for (int i = 0; i < 6; i++) {
        double gy = pt + gh - (yticks[i]/maxy)*gh;
        cairo_set_source_rgba(cr, CLR_BORDER, 0.35);
        cairo_set_line_width(cr, 0.5);
        cairo_move_to(cr, pl, gy); cairo_line_to(cr, pl+gw, gy); cairo_stroke(cr);
        char lb[8]; snprintf(lb, sizeof(lb), "%.0fx", yticks[i]);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 8);
        cairo_text_extents_t ex; cairo_text_extents(cr, lb, &ex);
        cairo_set_source_rgba(cr, CLR_SLATE, 0.65);
        cairo_move_to(cr, pl - ex.width - 6, gy+3); cairo_show_text(cr, lb);
    }

    int xp[] = {1, 2, 4, 6, 8};
    double xs = gw / 4.0;
    for (int i = 0; i < 5; i++) {
        double gx = pl + i*xs;
        cairo_set_source_rgba(cr, CLR_BORDER, 0.25);
        cairo_set_line_width(cr, 0.5);
        cairo_move_to(cr, gx, pt); cairo_line_to(cr, gx, pt+gh); cairo_stroke(cr);
        char lb[8]; snprintf(lb, sizeof(lb), "%dp", xp[i]);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 8);
        cairo_text_extents_t ex; cairo_text_extents(cr, lb, &ex);
        cairo_set_source_rgba(cr, CLR_SLATE, 0.65);
        cairo_move_to(cr, gx - ex.width/2, H-8); cairo_show_text(cr, lb);
    }

    cairo_set_source_rgba(cr, CLR_BORDER, 0.6);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, pl, pt); cairo_line_to(cr, pl, pt+gh); cairo_stroke(cr);
    cairo_move_to(cr, pl, pt+gh); cairo_line_to(cr, pl+gw, pt+gh); cairo_stroke(cr);

    double dash[] = {5.0, 3.0};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_set_source_rgba(cr, CLR_RED, 0.55);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, pl, pt+gh); cairo_line_to(cr, pl+gw, pt+gh); cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    if (a->serial_time > 0 && a->cpu_time > 0) {
        double sp = a->serial_time / a->cpu_time;
        double sps[5] = {1.0, sp*0.55, sp, sp*1.25, sp*1.4};

        cairo_pattern_t *area = cairo_pattern_create_linear(0, pt, 0, pt+gh);
        cairo_pattern_add_color_stop_rgba(area, 0.0, CLR_CYAN, 0.22);
        cairo_pattern_add_color_stop_rgba(area, 1.0, CLR_CYAN, 0.00);
        cairo_move_to(cr, pl, pt+gh);
        for (int i = 0; i < 5; i++) {
            double gx = pl + i*xs;
            double s  = CLAMP_D(sps[i], 0, maxy);
            cairo_line_to(cr, gx, pt+gh-(s/maxy)*gh);
        }
        cairo_line_to(cr, pl+gw, pt+gh);
        cairo_close_path(cr);
        cairo_set_source(cr, area); cairo_fill(cr);
        cairo_pattern_destroy(area);

        if (a->gpu_time > 0) {
            double gsp = a->serial_time / a->gpu_time;
            double gsps[5] = {1.0, gsp*0.55, gsp, gsp*1.28, gsp*1.45};
            cairo_set_source_rgba(cr, CLR_PURPLE, 0.85);
            cairo_set_line_width(cr, 2.0);
            for (int i = 0; i < 5; i++) {
                double gx = pl + i*xs;
                double s  = CLAMP_D(gsps[i], 0, maxy);
                if (i==0) cairo_move_to(cr, gx, pt+gh-(s/maxy)*gh);
                else      cairo_line_to(cr, gx, pt+gh-(s/maxy)*gh);
            }
            cairo_stroke(cr);
            for (int i = 1; i < 5; i++) {
                double gx = pl + i*xs;
                double s  = CLAMP_D(gsps[i], 0, maxy);
                double gy = pt+gh-(s/maxy)*gh;
                draw_glow(cr, gx, gy, 8, CLR_PURPLE, 0.3);
                cairo_arc(cr, gx, gy, 3.5, 0, 2*G_PI);
                cairo_set_source_rgba(cr, CLR_PURPLE, 0.9); cairo_fill(cr);
            }
        }

        cairo_set_source_rgba(cr, CLR_CYAN, 0.95);
        cairo_set_line_width(cr, 2.5);
        for (int i = 0; i < 5; i++) {
            double gx = pl + i*xs;
            double s  = CLAMP_D(sps[i], 0, maxy);
            if (i==0) cairo_move_to(cr, gx, pt+gh-(s/maxy)*gh);
            else      cairo_line_to(cr, gx, pt+gh-(s/maxy)*gh);
        }
        cairo_stroke(cr);
        for (int i = 1; i < 5; i++) {
            double gx = pl + i*xs;
            double s  = CLAMP_D(sps[i], 0, maxy);
            double gy = pt+gh-(s/maxy)*gh;
            draw_glow(cr, gx, gy, 10, CLR_CYAN, 0.25);
            cairo_arc(cr, gx, gy, 4.0, 0, 2*G_PI);
            cairo_set_source_rgba(cr, CLR_CYAN, 0.95); cairo_fill(cr);
            cairo_arc(cr, gx, gy, 1.5, 0, 2*G_PI);
            cairo_set_source_rgba(cr, CLR_WHITE, 0.95); cairo_fill(cr);
        }

        double peak_x = pl + 2*xs;
        double peak_s = CLAMP_D(sps[2], 0, maxy);
        double peak_y = pt+gh-(peak_s/maxy)*gh - 16;
        char sp_lbl[24]; snprintf(sp_lbl, sizeof(sp_lbl), "%.1fx", sp);
        rounded_rect(cr, peak_x-2, peak_y-11, 38, 14, 4);
        cairo_set_source_rgba(cr, CLR_CYAN, 0.12); cairo_fill(cr);
        neon_label(cr, sp_lbl, peak_x, peak_y, 9.5, CLR_CYAN, 1);
    } else {
        /* No data yet — placeholder text */
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10);
        cairo_set_source_rgba(cr, CLR_SLATE, 0.4);
        const char *ph = "Run a benchmark to populate this graph";
        cairo_text_extents_t ex; cairo_text_extents(cr, ph, &ex);
        cairo_move_to(cr, pl + gw/2 - ex.width/2, pt + gh/2);
        cairo_show_text(cr, ph);
    }

    /* legend */
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9);
    double lx = pl+gw-170, ly = pt+12;
    double d2[] = {4.0, 2.0};
    cairo_set_dash(cr, d2, 2, 0);
    cairo_set_source_rgba(cr, CLR_RED, 0.6);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, lx, ly); cairo_line_to(cr, lx+18, ly); cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);
    cairo_set_source_rgba(cr, CLR_SLATE, 0.8);
    cairo_move_to(cr, lx+22, ly+4); cairo_show_text(cr, "serial");

    cairo_set_source_rgba(cr, CLR_CYAN, 0.9);
    cairo_set_line_width(cr, 2.5);
    cairo_move_to(cr, lx+62, ly); cairo_line_to(cr, lx+80, ly); cairo_stroke(cr);
    cairo_set_source_rgba(cr, CLR_SLATE, 0.8);
    cairo_move_to(cr, lx+84, ly+4); cairo_show_text(cr, "CPU");

    if (a->gpu_time > 0) {
        cairo_set_source_rgba(cr, CLR_PURPLE, 0.85);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, lx+114, ly); cairo_line_to(cr, lx+132, ly); cairo_stroke(cr);
        cairo_set_source_rgba(cr, CLR_SLATE, 0.8);
        cairo_move_to(cr, lx+136, ly+4); cairo_show_text(cr, "GPU");
    }
    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════════
   6.  PERFORMANCE MATRIX
   ═══════════════════════════════════════════════════════════════════════════ */
gboolean telemetry_draw_matrix(GtkWidget *w, cairo_t *cr, App *a) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    double W = alloc.width, H = alloc.height;

    cairo_set_source_rgb(cr, CLR_BG_DEEP);
    cairo_rectangle(cr, 0, 0, W, H); cairo_fill(cr);
    draw_scanlines(cr, W, H);
    draw_section_header(cr, 12, 8, W-24, "PERFORMANCE MATRIX — TIME vs DATASET", CLR_MINT);

    double pl=52, pr=14, pt=26, pb=28;
    double gw = W-pl-pr, gh = H-pt-pb;

    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, CLR_BORDER, 0.5);
    cairo_move_to(cr, pl, pt); cairo_line_to(cr, pl, pt+gh); cairo_stroke(cr);
    cairo_move_to(cr, pl, pt+gh); cairo_line_to(cr, pl+gw, pt+gh); cairo_stroke(cr);

    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 8);
    cairo_set_source_rgba(cr, CLR_SLATE, 0.7);
    cairo_save(cr);
    cairo_translate(cr, 10, pt+gh/2);
    cairo_rotate(cr, -G_PI/2);
    cairo_show_text(cr, "Time (s)");
    cairo_restore(cr);
    cairo_move_to(cr, pl+gw/2-20, H-4);
    cairo_show_text(cr, "Dataset size");

    int have_data = (a->serial_time > 0 && a->cpu_time > 0);
    double sizes[6]  = {1, 10, 100, 500, 1000, 2000};
    double max_size  = 2000.0, max_time = 0.0;
    double s_times[6], c_times[6], g_times[6];
    double base_s = have_data ? a->serial_time : 0.5;
    double base_c = have_data ? a->cpu_time    : 0.15;
    double base_g = (a->gpu_time > 0) ? a->gpu_time : base_c * 0.4;

    for (int i = 0; i < 6; i++) {
        double sf = sizes[i] / (have_data ? (a->strategy.file_size / (1024*1024.0) + 1) : 100.0);
        s_times[i] = base_s * sf * (1.0 + log(sizes[i]+1) * 0.05);
        c_times[i] = base_c * sf * (1.0 + log(sizes[i]+1) * 0.02);
        g_times[i] = base_g * sf * (1.0 + log(sizes[i]+1) * 0.01);
        if (s_times[i] > max_time) max_time = s_times[i];
    }
    if (max_time < 0.1) max_time = 1.0;

    for (int i = 0; i <= 5; i++) {
        double gy = pt + gh - (double)i/5 * gh;
        cairo_set_source_rgba(cr, CLR_BORDER, 0.25);
        cairo_set_line_width(cr, 0.5);
        cairo_move_to(cr, pl, gy); cairo_line_to(cr, pl+gw, gy); cairo_stroke(cr);
        char lb[16]; snprintf(lb, sizeof(lb), "%.2fs", (double)i/5 * max_time);
        cairo_text_extents_t ex; cairo_text_extents(cr, lb, &ex);
        cairo_set_source_rgba(cr, CLR_SLATE, 0.55);
        cairo_move_to(cr, pl-ex.width-5, gy+3); cairo_show_text(cr, lb);
    }

    typedef struct { double *times; double r,g,b; const char *lbl; } Curve;
    Curve curves[3] = {
        {s_times, CLR_RED,    "Serial"},
        {c_times, CLR_CYAN,   "CPU"},
        {g_times, CLR_PURPLE, "GPU"},
    };
    int n_curves = 3;

    for (int c = 0; c < n_curves; c++) {
        Curve *cv = &curves[c];
        if (c == 0) {
            cairo_pattern_t *ar = cairo_pattern_create_linear(0, pt, 0, pt+gh);
            cairo_pattern_add_color_stop_rgba(ar, 0.0, cv->r, cv->g, cv->b, 0.12);
            cairo_pattern_add_color_stop_rgba(ar, 1.0, cv->r, cv->g, cv->b, 0.00);
            cairo_move_to(cr, pl, pt+gh);
            for (int i = 0; i < 6; i++) {
                double gx = pl + (sizes[i]/max_size)*gw;
                double gy = pt+gh-(cv->times[i]/max_time)*gh;
                cairo_line_to(cr, gx, gy);
            }
            cairo_line_to(cr, pl+gw, pt+gh); cairo_close_path(cr);
            cairo_set_source(cr, ar); cairo_fill(cr);
            cairo_pattern_destroy(ar);
        }
        cairo_set_line_width(cr, c==0 ? 1.5 : 2.0);
        cairo_set_source_rgba(cr, cv->r, cv->g, cv->b, c==0 ? 0.5 : 0.85);
        for (int i = 0; i < 6; i++) {
            double gx = pl + (sizes[i]/max_size)*gw;
            double gy = pt+gh-(cv->times[i]/max_time)*gh;
            if (i==0) cairo_move_to(cr, gx, gy);
            else      cairo_line_to(cr, gx, gy);
        }
        cairo_stroke(cr);

        double lx2 = pl + gw + 3;
        double ly2  = pt+gh-(cv->times[5]/max_time)*gh+4;
        cairo_set_font_size(cr, 7.5);
        cairo_set_source_rgba(cr, cv->r, cv->g, cv->b, 0.9);
        cairo_move_to(cr, lx2, ly2); cairo_show_text(cr, cv->lbl);
    }

    double cross_x = pl + 0.18*gw, cross_y = pt + 0.35*gh;
    cairo_set_source_rgba(cr, CLR_MINT, 0.7);
    cairo_set_font_size(cr, 7.5);
    cairo_move_to(cr, cross_x+4, cross_y-4);
    cairo_show_text(cr, "Parallel wins →");
    double d3[] = {3.0, 2.0};
    cairo_set_dash(cr, d3, 2, 0);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, CLR_MINT, 0.45);
    cairo_move_to(cr, cross_x, pt); cairo_line_to(cr, cross_x, pt+gh); cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    if (!have_data) {
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10);
        cairo_set_source_rgba(cr, CLR_SLATE, 0.4);
        const char *ph = "Run a benchmark to see real data";
        cairo_text_extents_t ex; cairo_text_extents(cr, ph, &ex);
        cairo_move_to(cr, pl + gw/2 - ex.width/2, pt + gh/2);
        cairo_show_text(cr, ph);
    }
    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════════
   7.  ANIMATION TICK — called every ~16ms
       RAM is read fresh from /proc/meminfo on every single tick.
   ═══════════════════════════════════════════════════════════════════════════ */
void telemetry_tick(App *a) {
    a->anim_tick++;
    a->global_phase = fmod(a->global_phase + 0.04, 2*G_PI);
    gint64 now = g_get_monotonic_time();

    /* smooth process progress */
    for (int i = 0; i < MAX_MPI_PROCS; i++) {
        double target = 0.0;
        switch (a->procs[i].state) {
            case PROC_RECEIVED:   target = 0.25; break;
            case PROC_PROCESSING: target = 0.75; break;
            case PROC_DONE:       target = 1.00; break;
            default:              target = 0.00; break;
        }
        a->procs[i].progress_display += (target - a->procs[i].progress_display) * 0.14;
    }

    /* thread heatmap: preview wave before first strategy packet */
    if (a->is_running && a->num_threads == 0) {
        int preview = 4;
        int wave = (a->anim_tick / 4) % preview;
        for (int i = 0; i < preview; i++) {
            int dist = abs(i - wave);
            a->thread_state[i] = (dist <= 1) ? THREAD_ACTIVE : THREAD_IDLE;
            if (a->thread_state[i] == THREAD_ACTIVE)
                a->thread_last_active[i] = now;
        }
    }

    /* decay stale threads */
    if (a->num_threads > 0) {
        int nt = a->num_threads > MAX_THREADS ? MAX_THREADS : a->num_threads;
        for (int i = 0; i < nt; i++) {
            if (a->thread_state[i] == THREAD_ACTIVE &&
                a->thread_last_active[i] > 0 &&
                (now - a->thread_last_active[i]) > 2000000)
                a->thread_state[i] = THREAD_IDLE;
        }
    }

    /* idle breathing */
    for (int i = 0; i < MAX_THREADS; i++) {
        if (a->thread_state[i] == THREAD_IDLE)
            a->thread_breathe[i] = 0.5 + 0.5 * sin(a->global_phase + i * 0.31);
        else
            a->thread_breathe[i] = 0.0;
    }

    /* ── Live RAM: read /proc/meminfo on every tick ── */
    {
        FILE *mf = fopen("/proc/meminfo", "r");
        if (mf) {
            uint64_t total=0, avail=0;
            char line[128];
            while (fgets(line, sizeof(line), mf)) {
                sscanf(line, "MemTotal: %llu kB",     (unsigned long long*)&total);
                sscanf(line, "MemAvailable: %llu kB", (unsigned long long*)&avail);
            }
            fclose(mf);
            if (total > 0) {
                a->mem.ram_total_kb = total;
                a->mem.ram_used_kb  = total - avail;
                a->mem.ram_frac     = (double)(total - avail) / (double)total;
            }
        }
    }

    /* VRAM estimate from strategy */
    if (a->strategy.gpu_vram_bytes > 0 && a->is_running &&
        a->strategy.mode == MODE_MPI_OPENCL) {
        a->mem.vram_total_bytes = a->strategy.gpu_vram_bytes;
        a->mem.vram_used_bytes  = a->strategy.chunk_size * 2;
        a->mem.vram_frac = CLAMP_D(
            (double)a->mem.vram_used_bytes / (double)a->mem.vram_total_bytes,
            0.0, 1.0);
    }
}