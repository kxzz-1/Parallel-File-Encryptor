# ═══════════════════════════════════════════════════════════════════════════
# Makefile — Parallel File Encrypter (Backend + Cyberpunk Dashboard GUI)
# ═══════════════════════════════════════════════════════════════════════════

CC          = mpicc
CCGUI       = gcc
CFLAGS      = -Wall -O2 -Iinclude -fopenmp -std=c11
LDFLAGS     = -fopenmp -lOpenCL -lcrypto -lm

GTK_CFLAGS  = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS    = $(shell pkg-config --libs   gtk+-3.0)
GUI_CFLAGS  = -Wall -O2 -Iinclude -std=c11 $(GTK_CFLAGS)
GUI_LDFLAGS = $(GTK_LIBS) -lm

# ── backend ──────────────────────────────────────────────────────────────────
BACKEND_SRCS = \
    src/main.c           \
    src/file_io.c        \
    src/strategy.c       \
    src/serial_aes.c     \
    src/mpi_handler.c    \
    src/omp_scheduler.c  \
    src/opencl_aes.c     \
    src/benchmark.c

TARGET = encrypter

# ── GUI (modular, 4 translation units) ───────────────────────────────────────
GUI_SRCS = \
    src/ui_main.c        \
    src/ui_telemetry.c   \
    src/ui_callbacks.c   \
    src/backend_parser.c

GUI_TARGET = encrypter_gui

# ── default target ─────────────────────────────────────────────────────────
.PHONY: all clean backend gui

all: backend gui

backend: $(TARGET)

gui: $(GUI_TARGET)

$(TARGET): $(BACKEND_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(GUI_TARGET): $(GUI_SRCS)
	$(CCGUI) $(GUI_CFLAGS) $^ -o $@ $(GUI_LDFLAGS)

# ── individual object files (optional, for incremental builds) ──────────────
GUI_OBJS = $(GUI_SRCS:src/%.c=build/%.o)

$(GUI_TARGET)-incremental: $(GUI_OBJS)
	$(CCGUI) $^ -o $(GUI_TARGET) $(GUI_LDFLAGS)

build/%.o: src/%.c | build
	$(CCGUI) $(GUI_CFLAGS) -c $< -o $@

build:
	mkdir -p build

# ── clean ───────────────────────────────────────────────────────────────────
clean:
	rm -f $(TARGET) $(GUI_TARGET) *.o *.enc *.dec test_aes bigtest*
	rm -rf build

# ── convenience: run the GUI (backend must be built and in PATH or ./encrypter)
run-gui: gui
	./$(GUI_TARGET)

# ── help ────────────────────────────────────────────────────────────────────
help:
	@echo "Targets:"
	@echo "  all        — build both backend and GUI (default)"
	@echo "  backend    — build ./encrypter (MPI backend only)"
	@echo "  gui        — build ./encrypter_gui (GTK dashboard)"
	@echo "  run-gui    — build + launch the dashboard"
	@echo "  clean      — remove all build artefacts"