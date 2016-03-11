#ifndef _FTP_FUNCTIONS_H
#define _FTP_FUNCTIONS_H

#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/dir.h>
#include <sys/stat.h>

#define MAX_NUM_CONNECTED_CLIENTS 5

void 
error(const char * message);

int 
initiate_server(const char * port);

int 
RETR(int file_fd, int data_fd);

char *
LIST(char * dir_name);

int
STOR(int file_fd, int data_fd);

#endif

