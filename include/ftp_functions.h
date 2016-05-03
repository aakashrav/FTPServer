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

#define NUM_THREADS 5
#define MAX_NUM_CONNECTED_CLIENTS 5
#define DEBUG

// Number of jobs available for processing
int available_jobs; 
// Mutex to job queue
pthread_mutex_t * job_queue_lock;
// Conditional variable signalling that new job has become available for processing
pthread_cond_t * job_available;

// Node in job queue linked list
typedef struct job {
	int fd;
	struct sockaddr_storage client_addr;
	struct job * next;
	struct job * previous;
} job_t;

// Head of job queue linked list
job_t * head;

// Initlialize mutexes, conditional variables, and job queue
int
init();

// Destroy mutexes, conditional variables, and job queue
int 
destroy();

// Enqueue job into job queue
int
enqueue(job_t *head, int fd, struct sockaddr_storage client_addr);

// Dequeue job from job queue
job_t
dequeue(job_t *head);

// Free all the remaining jobs in the queue, cleanup function
void
free_jobs(job_t * head);

// Thread function that processes jobs
void *
ftp_thread(void * args);

void 
error(const char * message);

int 
initiate_server(long port);

int 
RETR(int file_fd, int data_fd);

char *
LIST(char * dir_name);

int
STOR(int file_fd, int data_fd);

long
get_random_port();

char *
get_formatted_local_ip_address(unsigned int port);

int
get_active_client_connection(const char * ip_address, const char * port);

#endif

