CFLAGS=`pkg-config --cflags gtk+-2.0` -Wall -D_FORTIFY_SOURCE=2 -g -D_GNU_SOURCE -I/usr/include/tcl8.5
LIBS=`pkg-config --libs gtk+-2.0` -ltcl8.5
OBJS := acmacs.o buffer.o font.o editor.o buffers.o editors.o interp.o global.o undo.o

all: acmacs

clean:
	rm $(OBJS) *.d acmacs

acmacs: $(OBJS)
	$(CC) -o $@ $^ $(LIBS) 

# pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)

# compile and generate dependency info
%.o: %.c
	gcc -c $(CFLAGS) $*.c -o $*.o
	gcc -MM $(CFLAGS) $*.c > $*.d