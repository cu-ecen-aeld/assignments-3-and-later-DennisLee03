# Makefile

# allow user to pass CROSS_COMPILE
CC := $(CROSS_COMPILE)gcc

TARGET = writer
SRC = writer.c
OBJ = writer.o
CFLAGS = -Wall -g

.PHONY: all clean

all: $(TARGET)

# link
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# compile
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean any previous build artifacts
clean:
	rm -f $(TARGET) *.o
