# Makefile

# allow user to pass CROSS_COMPILE
CC := $(CROSS_COMPILE)gcc

TARGET = writer
SRC = writer.c
OBJ = writer.o

.PHONY: all clean

all: $(TARGET)

# link
$(TARGET): $(OBJ)
	$(CC) $^ -o $@

# compile
%.o: %.c
	$(CC) -c $< -o $@

# Clean any previous build artifacts
clean:
	rm -f $(TARGET) *.o
