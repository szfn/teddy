CFLAGS=`pkg-config --cflags gtk+-2.0` -Wall -D_FORTIFY_SOURCE=2 -g -D_GNU_SOURCE -I/usr/include/tcl8.5 -std=c99
LIBS=`pkg-config --libs gtk+-2.0` -ltcl8.5 -lfontconfig -licuuc -lutil -lpcre
OBJS := obj/teddy.o obj/buffer.o obj/font.o obj/editor.o obj/buffers.o obj/columns.o obj/column.o obj/interp.o obj/global.o obj/undo.o obj/reshandle.o obj/go.o obj/baux.o obj/cmdcompl.o obj/history.o obj/jobs.o obj/shell.o obj/colors.o obj/point.o obj/editor_cmdline.o obj/cfg.o obj/research.o obj/parmatch.o obj/wordcompl.o

all: bin/teddy

clean:
	rm -Rf obj/ *.d *~ teddy cfg.c cfg.h colors.c builtin.h

bin/teddy: builtin.h cfg.h cfg.c colors.c $(OBJS)
	mkdir -p bin
	$(CC) -o $@ $(OBJS) $(LIBS)

colors.c: rgb.txt colors-compile.pl
	perl colors-compile.pl > colors.c

cfg.c: cfg.src cfg-create.pl
	perl cfg-create.pl

cfg.h: cfg.src cfg-create.pl
	perl cfg-create.pl

builtin.h: builtin.tcl builtin-create.pl
	perl builtin-create.pl

# pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)

# compile and generate dependency info
obj/%.o: %.c
	mkdir -p obj
	gcc -c $(CFLAGS) $*.c -o obj/$*.o
	gcc -MM $(CFLAGS) $*.c > obj/$*.d
