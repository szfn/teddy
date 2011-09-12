CFLAGS=`pkg-config --cflags gtk+-2.0` -Wall -D_FORTIFY_SOURCE=2 -g -D_GNU_SOURCE -I/usr/include/tcl8.5
LIBS=`pkg-config --libs gtk+-2.0` -ltcl8.5 -lfontconfig -licuuc -lutil
OBJS := teddy.o buffer.o font.o editor.o buffers.o columns.o column.o interp.o global.o undo.o reshandle.o go.o baux.o cmdcompl.o history.o jobs.o shell.o

all: teddy

clean:
	rm $(OBJS) *.d *~ teddy

teddy: $(OBJS)
	$(CC) -o $@ $^ $(LIBS) 

# pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)

# compile and generate dependency info
%.o: %.c
	gcc -c $(CFLAGS) $*.c -o $*.o
	gcc -MM $(CFLAGS) $*.c > $*.d