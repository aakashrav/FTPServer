#ifndef _FTP_FUNCTIONS_H
#define		_FTP_FUNCTIONS_H

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

// Number of threads in the thread pool
#define		NUM_THREADS 5
/*
 * Maximum number of connected clients to the server;
 * used in the 'listen' system call
 */
#define		MAX_NUM_CONNECTED_CLIENTS 5

/*
 * Create a structure that holds all the parameters
 * of the current client connection context.
 * This structure will be passed between the different
 * functions within the FTP handler thread.
 */
typedef struct client_context {
	char * input_command;
	int client_comm_fd;
	int active_flag;
	int binary_flag;
	int data_port;
	int data_fd;
	char * current_working_directory;
	char * PORT;
	struct sockaddr_storage client_addr;
	int client_data_fd;
} client_context_t;

/*
 * Create a structure for matching different
 * FTP commands to their specific handlers
 */
typedef struct command_matcher {
	char * command;
	void (*handler)(client_context_t * context);
} command_matcher_t;

/*
 *  Returns the appropriate handler for a certain
 * command from the client
 */
void
(*get_handler(char * command))(client_context_t *);


// Initlialize mutexes, conditional variables, and job queue
int
init();

// A thread function that processes jobs
void *
ftp_thread(void * args);

// Initiates a 'listen'ing server socket at the given port
int
initiate_server(long port);

// Handler function for the USER FTP command
void
USER_HANDLER(client_context_t * current_context);

// Handler function for the PASS FTP command
void
PASS_HANDLER(client_context_t * current_context);

// Handler function for the SYST FTP command
void
SYST_HANDLER(client_context_t * current_context);

// Handler function for the FEAT FTP command
void
FEAT_HANDLER(client_context_t * current_context);

// Handler function for the QUIT FTP command
void
QUIT_HANDLER(client_context_t * current_context);

/*
 * Handler function if the client issues a command
 * that is not recognized by the FTP protocol.
 */
void
OTHER_HANDLER(client_context_t * current_context);

// Handler function for the PWD FTP command
void
PWD_HANDLER(client_context_t * current_context);

// Handler function for the PASV FTP command
void
PASV_HANDLER(client_context_t * current_context);

// Handler function for the EPSV FTP command
void
EPSV_HANDLER(client_context_t * current_context);

// Handler function for the CWD FTP command
void
CWD_HANDLER(client_context_t * current_context);

// Handler function for the PORT FTP command
void
PORT_HANDLER(client_context_t * current_context);

// Handler function for the EPRT FTP command
void
EPRT_HANDLER(client_context_t * current_context);

// Handler function for the TYPE FTP command
void
TYPE_HANDLER(client_context_t * current_context);

// Handle for the RETR FTP command
void
RETR_HANDLER(client_context_t * current_context);

// Used to accomplish the RETR FTP command
int
RETR(int file_fd, int data_fd, int binary_flag);

// Handle for the LIST FTP command
void
LIST_HANDLER(client_context_t * current_context);

// Used to accmplish the LIST FTP command
char *
LIST(char * dir_name);

// Handle for the STOR FTP command
void
STOR_HANDLER(client_context_t * current_context);

// Used to accomplish the STOR FTP command
int
STOR(int file_fd, int data_fd, int binary_flag);

// Handle for the APPE FTP command
void
APPE_HANDLER(client_context_t * current_context);

// Used to accomplish the RMD FTP command
void
RMD_HANDLER(client_context_t * current_context);

// Used to accomplish the MKD FTP command
void
MKD_HANDLER(client_context_t * current_context);


#endif
