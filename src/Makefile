SERVER_SOURCES=main_server.c ftp_functions.c utils.c
SERVER_OBJECTS=$(SERVER_SOURCES:.c=.o)
CC=gcc
CFLAGS=-c -Wall -std=c99 -I../include -MP -MMD -D_DEFAULT_SOURCE
EXECUTABLE_DIRECTORY=.
UNAME := `uname`

all: ftp2_server

.c.o:
	$(CC) $(CFLAGS) $<

ftp2_server: $(SERVER_OBJECTS)
ifeq ($(UNAME),SunOS)
	$(CC) -o $(EXECUTABLE_DIRECTORY)/$@ -pthread -lsocket -lnsl $(SERVER_OBJECTS)
else
	$(CC) -o $(EXECUTABLE_DIRECTORY)/$@ -pthread $(SERVER_OBJECTS)
endif

clean:
	-rm -f $(EXECUTABLE_DIRECTORY)/ftp2_server
	-rm -f $(SERVER_OBJECTS) 2>/dev/null
	-rm -f $(SERVER_SOURCES:.c=.d) 2>/dev/null
	