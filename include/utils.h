#ifndef _UTILS_H
#define	_UTILS_H

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

#define DEBUG

// Node in the job queue linked list
typedef struct job {
	int fd;
	struct sockaddr_storage client_addr;
	struct job * next;
	struct job * previous;
} job_t;

// Head of job queue linked list
job_t * head;

// Number of jobs available for processing
int available_jobs;
// Mutex to access the job queue
pthread_mutex_t * job_queue_lock;
/*
 * Conditional variable signalling that new job has
 * become available for processing
 */
pthread_cond_t * job_available;

//  An error function for graceful termination
void
error(const char * message);

// Print debugging messages
void
print_debug(const char * message);

// Destroy mutexes, conditional variables, and job queue
int
destroy();

// Enqueue a new job into job queue
int
enqueue(job_t *head, int fd, struct sockaddr_storage client_addr);

// Dequeue a new job from job queue
job_t
dequeue(job_t *head);

// Free all the remaining jobs in the queue; cleanup function
void
free_jobs(job_t * head);

/*
 * Gets a random port in the range [1000,65535]
 * for passive FTP connections
 */
long
get_random_port();

/*
 * Formats the local interface address and port in the
 * format prescribed by RFC for FTP protocol
 */
char *
get_formatted_local_ip_address(unsigned int port, int IPV4ONLY);

/* 
 * Connects to the client with the given 
 * IP Address
 * and Port in active mode
 */
int
get_active_client_connection(const char * ip_address,
	const char * port);

/*
 * Reads a line from the inputted file and
 * outputs the contents into the buffer
 */
int
readline(FILE *f, char *buffer, int len);

#endif
