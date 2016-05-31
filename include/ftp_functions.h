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
#include <ifaddrs.h>
#include <netinet/in.h> 
#include <string.h> 
#include <arpa/inet.h>

/* Number of threads in the thread pool */
#define NUM_THREADS 5
/* Maximum number of connected clients to the server; 
	used in the 'listen' system call */
#define MAX_NUM_CONNECTED_CLIENTS 5

/* Initlialize mutexes, conditional variables, and job queue */
int
init();

/* A thread function that processes jobs */
void *
ftp_thread(void * args);

/* Initiates a 'listen'ing server socket at the given port */
int 
initiate_server(long port);

/* Handler function for the PWD FTP command */
void
PWD_HANDLER(int client_comm_fd, char * new_path);

/* Handler function for the PASV FTP command */
void
PASV_HANDLER(int client_comm_fd, int * active_flag, long * data_port,
	int * data_fd);

/* Handler function for the EPSV FTP command */
void
EPSV_HANDLER(int client_comm_fd, int * active_flag, long * data_port,
	int * data_fd);

/* Handler function for the CWD FTP command */
void
CWD_HANDLER(char * input_command, int client_comm_fd,
	char * old_path);

/* Handler function for the PORT FTP command */
void
PORT_HANDLER(char * input_command, int client_comm_fd,
	char * PORT, int * active_flag);

/* Handler function for the TYPE FTP command */
void
TYPE_HANDLER(char * input_command, int client_comm_fd, 
	int * binary_flag);

/* Handle for the RETR FTP command */
void
RETR_HANDLER(char * input_command, int client_comm_fd,
	int binary_flag, int active_flag, char * PORT,
	int data_fd, struct sockaddr_storage client_addr);

/* Used to accomplish the RETR FTP command */
int 
RETR(int file_fd, int data_fd, int binary_flag);

/* Handle for the LIST FTP command */
void
LIST_HANDLER(char * input_command, int client_comm_fd, char * new_path,
	int active_flag, char * PORT, int data_fd,
	struct sockaddr_storage client_addr, int client_data_fd);

/* Used to accmplish the LIST FTP command */
char *
LIST(char * dir_name);

/* Handle for the STOR FTP command */
void
STOR_HANDLER(char * input_command, int client_comm_fd,
	int binary_flag, int active_flag, char * PORT,
	int data_fd, struct sockaddr_storage client_addr, int client_data_fd);

/* Used to accomplish the STOR FTP command */
int
STOR(int file_fd, int data_fd, int binary_flag);

void 
APPE_HANDLER(char * input_command, int client_comm_fd,
	int binary_flag, int active_flag, char * PORT,
	int data_fd, struct sockaddr_storage client_addr);

/* Used to accomplish the RMD FTP command */
void
RMD_HANDLER(int client_comm_fd);

/* Used to accomplish the MKD FTP command */
void
MKD_HANDLER(int client_comm_fd);


#endif
