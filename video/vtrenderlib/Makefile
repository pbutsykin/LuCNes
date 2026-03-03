AR ?= ar
CFLAGS_COMMON = --std=c99 -Wall -Wextra -Werror

ifeq ($(BUILD),debug)
    CFLAGS = -g -Og $(CFLAGS_COMMON) $(SANITIZERS)
else
    CFLAGS = -O2 $(CFLAGS_COMMON) -DNDEBUG
endif

LIB = libvtrenderlib.a
OBJ = vtrenderlib.o

all: $(LIB)

$(OBJ): vtrenderlib.c vtrenderlib.h
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	$(AR) rcs $@ $<

clean:
	rm -f $(OBJ) $(LIB)

.PHONY: all clean
