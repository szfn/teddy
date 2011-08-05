CFLAGS=`pkg-config --cflags gtk+-2.0` -Wall -D_FORTIFY_SOURCE=2 -g -D_GNU_SOURCE
LIBS=`pkg-config --libs gtk+-2.0`

all: gtktest

clean:
	rm *.o gtktest

gtktest: gtktest.o buffer.o font.o
	$(CC) -o $@ $^ $(LIBS) 
