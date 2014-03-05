CC	:= gcc
CFLAGS	:= -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -g2
LDFLAGS := -luuid

all: vhdtool

vhdtool: vhdtool.o
	$(CC) $^ $(LDFLAGS) -o $@

vhdtool.o: vhdtool.c

clean:
	rm -f vhdtool vhdtool.o

