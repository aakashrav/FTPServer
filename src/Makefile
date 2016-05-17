SERVER_SOURCES=main_server.c ftp_functions.c
SERVER_OBJECTS=$(SERVER_SOURCES:.c=.o)
CC=gcc
CFLAGS=-c -Wall -I../include -MP -MMD
EXECUTABLE_DIRECTORY=../bin

all: sftp2_server

.c.o:
	$(CC) $(CFLAGS) $<

sftp2_server: $(SERVER_OBJECTS)
	$(CC) -o $(EXECUTABLE_DIRECTORY)/$@ -pthread $(SERVER_OBJECTS)

clean:
	rm $(SERVER_OBJECTS)
	rm $(SERVER_SOURCES:.c=.d)
	rm $(EXECUTABLE_DIRECTORY)/sftp2_server
