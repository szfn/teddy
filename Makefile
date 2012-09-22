CFLAGS=`pkg-config --cflags gtk+-2.0` -Wall -D_FORTIFY_SOURCE=2 -g -D_GNU_SOURCE -I/usr/include/tcl8.5 -std=c99
LIBS=`pkg-config --libs gtk+-2.0` -ltcl8.5 -lfontconfig -licuuc -lutil -ltre -lm
OBJS := obj/teddy.o obj/buffer.o obj/editor.o obj/buffers.o obj/columns.o obj/column.o obj/interp.o obj/global.o obj/undo.o obj/go.o obj/baux.o obj/history.o obj/jobs.o obj/shell.o obj/colors.o obj/point.o obj/cfg_auto.o obj/cfg.o obj/research.o obj/parmatch.o obj/compl.o obj/lexy.o obj/treint.o obj/critbit.o obj/cmdcompl.o obj/tframe.o obj/foundry.o obj/top.o obj/iopen.o obj/tags.o

all: bin/teddy

clean:
	rm -Rf obj/ *~ bin cfg_auto.c cfg_auto.h colors.c builtin.h autoconf.h critbit.c

critbit-test: critbit.o critbit-test.cc
	g++ -o critbit-test critbit-test.cc critbit.o

bin/teddy: builtin.h autoconf.h cfg_auto.h cfg_auto.c colors.c $(OBJS)
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

bin/teddy32: builtin.h autoconf.h cfg_auto.h cfg_auto.c colors.c $(OBJS32)
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ -m32 $(OBJS32) $(LIBS)

colors.c: rgb.txt colors-compile.pl
	perl colors-compile.pl > colors.c

cfg_auto.c: cfg.src cfg-create.pl
	perl cfg-create.pl

cfg_auto.h: cfg.src cfg-create.pl
	perl cfg-create.pl

critbit.c: critbit.w
	ctangle critbit.w

bin/critbit.pdf: critbit.w
	cweave critbit.w
	pdftex critbit.tex

obj/critbit.o: critbit.c critbit.addenda.c

builtin.h: builtin.tcl builtin-create.pl
	perl builtin-create.pl

autoconf.h: example.teddy builtin-create.pl
	perl builtin-create.pl

git.date.h: $(OBJS)
	./make.git.date.sh

# compile and generate dependency info
obj/%.o: %.c
	mkdir -p obj
	gcc -c $(CFLAGS) $*.c -o obj/$*.o
	echo -n obj/ > obj/$*.d
	gcc -MM $(CFLAGS) $*.c >> obj/$*.d

# pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)
