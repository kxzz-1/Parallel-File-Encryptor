#ifndef UI_TELEMETRY_H
#define UI_TELEMETRY_H

/* ═══════════════════════════════════════════════════════════════════════════
   ui_telemetry.h  —  Public interface for the Cairo drawing engine
   ═══════════════════════════════════════════════════════════════════════════ */

#include <gtk/gtk.h>
#include "app_state.h"

/* ── Cairo draw callbacks (connect to "draw" signal of GtkDrawingArea) ─── */

/**
 * telemetry_draw_split
 * Tab 1 — File split / MPI process node diagram.
 * Shows per-rank progress bars, state badges, and data-flow connectors.
 */
gboolean telemetry_draw_split   (GtkWidget *widget, cairo_t *cr, App *a);

/**
 * telemetry_draw_heatmap
 * Tab 1 — OpenMP thread heatmap grid.
 * Active cells pulse with cyan glow; idle cells breathe softly in purple.
 */
gboolean telemetry_draw_heatmap (GtkWidget *widget, cairo_t *cr, App *a);

/**
 * telemetry_draw_memgauge
 * Tab 1 — RAM + VRAM arc gauges.
 * Turns red when usage exceeds 80 %.
 */
gboolean telemetry_draw_memgauge(GtkWidget *widget, cairo_t *cr, App *a);

/**
 * telemetry_draw_strategy
 * Tab 1 — Strategy recommendation card.
 * Shows the chosen mode, reasoning, and key hardware facts as neon pills.
 */
gboolean telemetry_draw_strategy(GtkWidget *widget, cairo_t *cr, App *a);

/**
 * telemetry_draw_graph
 * Tab 2 — Speedup graph (Speedup vs. MPI process count).
 * Draws serial baseline, CPU curve, optional GPU curve, and peak annotation.
 */
gboolean telemetry_draw_graph   (GtkWidget *widget, cairo_t *cr, App *a);

/**
 * telemetry_draw_matrix
 * Tab 2 — Performance matrix (Time vs. Dataset size).
 * Shows efficiency frontier where parallel execution overtakes serial.
 */
gboolean telemetry_draw_matrix  (GtkWidget *widget, cairo_t *cr, App *a);

/* ── Animation driver ────────────────────────────────────────────────────── */

/**
 * telemetry_tick
 * Called once per animation frame (~60 FPS via g_timeout_add(16, ...)).
 * Updates:
 *   • smooth process progress interpolation
 *   • thread heatmap wave simulation when no real updates arrive
 *   • idle-thread breathing phase
 *   • RAM telemetry from /proc/meminfo
 *   • VRAM usage estimate from strategy
 */
void telemetry_tick(App *a);

#endif /* UI_TELEMETRY_H */
