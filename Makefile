CC         = mpicc
CFLAGS     = -Wall -O2 -Iinclude -fopenmp
LDFLAGS    = -fopenmp -lOpenCL -lcrypto

GUI_SRC    = src/gtk_gui.c
GUI_TARGET = encrypter_gui
GTK_FLAGS  = $(shell pkg-config --cflags --libs gtk+-3.0)

SRCS = src/main.c \
       src/file_io.c \
       src/strategy.c \
       src/serial_aes.c \
       src/mpi_handler.c \
       src/omp_scheduler.c \
       src/opencl_aes.c \
       src/benchmark.c

TARGET = encrypter

all: $(TARGET) $(GUI_TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

$(GUI_TARGET): $(GUI_SRC)
	gcc -Wall -O2 $(GUI_SRC) -o $(GUI_TARGET) $(GTK_FLAGS) -lm

clean:
	rm -f $(TARGET) $(GUI_TARGET) *.o *.enc *.dec test_aes bigtest*