CFLAGS=`pkg-config --cflags gtk+-2.0` -Wall -D_FORTIFY_SOURCE=2
LIBS=`pkg-config --libs gtk+-2.0`

all: gtktest

clean:
	rm *.o gtktest

gtktest: gtktest.o buffer.o
	$(CC) -o $@ $^ $(LIBS) 
