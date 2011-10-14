CC	:= gcc
CFLAGS	:= -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -g2

all: vhdtool

vhdtool: vhdtool.o
vhdtool.o: vhdtool.c

clean:
	rm vhdtool vhdtool.o

