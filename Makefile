CFLAGS=`pkg-config --cflags gtk+-2.0` -Wall -D_FORTIFY_SOURCE=2 -g -D_GNU_SOURCE -I/usr/include/tcl8.5 -std=c99
LIBS=`pkg-config --libs gtk+-2.0` -ltcl8.5 -lfontconfig -licuuc -lutil -lpcre
OBJS := teddy.o buffer.o font.o editor.o buffers.o columns.o column.o interp.o global.o undo.o reshandle.o go.o baux.o cmdcompl.o history.o jobs.o shell.o colors.o point.o editor_cmdline.o cfg.o research.o parmatch.o

all: teddy

clean:
	rm $(OBJS) *.d *~ teddy cfg.c cfg.h colors.c builtin.h

teddy: builtin.h cfg.h cfg.c colors.c $(OBJS)
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
%.o: %.c
	gcc -c $(CFLAGS) $*.c -o $*.o
	gcc -MM $(CFLAGS) $*.c > $*.d
