CC = gcc

DEBUGFLAGS = -g
CFLAGS = -Wall
LDFLAGS = -lpthread

all: threadpooldaemon 

threadpooldaemon: 
	$(CC) $(CFLAGS) threadpooldaemon.c -o threadpooldaemon $(LDFLAGS) $(DEBUGFLAGS)

threadpoolprocess: 
	$(CC) $(CFLAGS) threadpoolprocess.c -o threadpoolprocess $(LDFLAGS) $(DEBUGFLAGS)

clean:
	rm -f *.o *~ threadpooldaemon threadpoolprocess 

