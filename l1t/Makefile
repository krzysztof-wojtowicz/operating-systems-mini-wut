# Compiler and flags
CC := gcc
CFLAGS := -Wall -fsanitize=address,undefined
LDFLAGS := -fsanitize=address,undefined

# Automatically find all .c files in the directory
SRC := $(wildcard *.c)
# Replace .c with .o for object files
OBJ := $(SRC:.c=.o)
# Replace .c with no extension for program names
PROGS := $(SRC:.c=)

# Default target
all: $(PROGS)

# Generic rule for building each program
%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Clean rule
.PHONY: clean all
clean:
	rm -f $(PROGS) $(OBJ)