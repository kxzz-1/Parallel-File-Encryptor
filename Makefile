CC      = mpicc
CFLAGS  = -Wall -O2 -Iinclude -fopenmp
LDFLAGS = -fopenmp -lOpenCL

SRCS = src/main.c \
       src/file_io.c \
       src/strategy.c \
       src/serial_aes.c \
       src/mpi_handler.c \
       src/omp_scheduler.c \
       src/opencl_aes.c \
       src/benchmark.c

TARGET = encrypter

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o *.enc *.dec test_aes
