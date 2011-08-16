CFLAGS=`pkg-config --cflags gtk+-2.0` -Wall -D_FORTIFY_SOURCE=2 -g -D_GNU_SOURCE
LIBS=`pkg-config --libs gtk+-2.0`

all: gtktest

clean:
	rm $(OBJS) *.d gtktest

OBJS := gtktest.o buffer.o font.o editor.o buffers.o

gtktest: $(OBJS)
	$(CC) -o $@ $^ $(LIBS) 

# pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)

# compile and generate dependency info
%.o: %.c
	gcc -c $(CFLAGS) $*.c -o $*.o
	gcc -MM $(CFLAGS) $*.c > $*.d