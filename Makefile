CC := gcc
CFLAGS := -std=c11 -O2 -Wall -W -Werror -Wno-unused-parameter
LDFLAGS :=
LIBS := -lm -lpigpio -pthread
OBJS := main.o
ALL := irtx

all: $(ALL)

irtx: irtx.o
	$(CC) -o $@ $< $(LIBS)

irtx.o: irtx.c
	$(CC) $(CFLAGS) -fpic -c -o $@ $<
