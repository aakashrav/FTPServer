#include "utils.h"

// Print debugging messages
inline void
print_debug(const char * message) {

#ifdef DEBUG
printf("%s", message);
fflush(stdout);
#endif

}

// Error function to exit gracefully
void
error(const char * message) {

	// Print the error number
	printf("Error number: %d\n", errno);
	fflush(stdout);
	// Print the error message
	perror(message);
	// Deallocate resources related to the thread pool and mutexes
	destroy();
	free_jobs(head);
	exit(1);
}

// Destroy mutexes, conditional variables, and job queue
int
destroy() {

	pthread_mutex_destroy(job_queue_lock);
	job_queue_lock = NULL;

	pthread_cond_destroy(job_available);
	job_available = NULL;

	free_jobs(head);

	return (0);
}

// Enqueue a single job into the job queue
int
enqueue(job_t *head, int fd, struct sockaddr_storage client_addr) {

	job_t * jb = head;
	while (jb->next != NULL)
	jb = jb->next;

	job_t * new_job = calloc(1, sizeof (struct job));
	new_job->fd = fd;
	new_job->client_addr = client_addr;
	new_job->previous = jb;
	new_job->next = NULL;
	jb->next = new_job;

	return (0);
}

// Dequeue a single job from the job queue
job_t
dequeue(job_t *head) {

	job_t return_job;
	job_t * jb = head;
	jb = jb->next;

	if (jb != NULL) {

		job_t * nxt_job = jb->next;
		jb->previous->next = nxt_job;
		if (nxt_job != NULL)
			nxt_job->previous = jb->previous;

		return_job.fd = jb->fd;
		return_job.client_addr = jb->client_addr;
		free(jb);
		jb = NULL;
	}
	else
	{
	printf("No jobs available!\n");
	fflush(stdout);
	return_job.fd = -1;
	}

	return (return_job);
}

// Free all the remaining jobs in the queue, cleanup function
void
free_jobs(job_t * head) {

	while (head->next != NULL) {
		job_t * next = head->next;
		free(head);
		head = next;
	}

	// For extra safety, don't want double free problems later
	head = NULL;
}

/*
 * Generates a random port number that is usable by user applications.
 * Namely, will generate a random number between 1000 - 65535
 */
long
get_random_port() {

	int a = 0;
	while (a < 1000) {
		a = rand();
		a &= 0xffff;
	}
	return (a);
}

/*
 * Return an IP Address + Port endpoint combination in a format that is
 *  normative for FTP servers; an example would be (187, 165, 1, 12, 210, 19)
 */
char *
get_formatted_local_ip_address(unsigned int port, int IPV4ONLY) {

	/*
	 * Use getifaddrs to get network interfaces of
	 * the local machine and combine it
	 * with the inputted port
	 */
	struct ifaddrs * ifAddrStruct;
	struct ifaddrs * ifa;
	void * tmpAddrPtr = NULL;
	char * complete_address_buffer = calloc(INET_ADDRSTRLEN+15, 1);

	/*
	 * IPV6 mode
	 */
	if (!IPV4ONLY) {
		strcat(complete_address_buffer, "(");
		strcat(complete_address_buffer, "|||");

		char * port_string = calloc(6, 1);
		sprintf(port_string, "%d", port);
		strcat(complete_address_buffer, port_string);

		strcat(complete_address_buffer, "|)");
		strcat(complete_address_buffer, "\0");
		free(port_string);
		return complete_address_buffer;
	}

	/*
	 * IPV4 mode
	 */
	if (getifaddrs(&ifAddrStruct) == -1) {
		error("Error in getifaddrs when getting local IP address");
	}

	char address_buffer_ipv4[INET_ADDRSTRLEN];

	for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {

		if (!ifa->ifa_addr)
			continue;
		else if ((ifa->ifa_addr->sa_family) != AF_INET)
			continue;
		else {

			if (strcmp(ifa->ifa_name, "lo0") == 0)
				continue;

			tmpAddrPtr=
			&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
			inet_ntop(ifa->ifa_addr->sa_family, tmpAddrPtr,
				address_buffer_ipv4,
				INET_ADDRSTRLEN);
		}
	}

	// Some error occured, and the search for local interfaces failed
	if (ifAddrStruct == NULL)
		return (NULL);

	/*
	 * Now we do some string manipulation so that we get the
	 * IP Address and the port into the format we need for FTP.
	 * This procedure looks ostensibly complex, but is
	 * actually just simple moving around of strings and characters
	 */
	char * buf_ptr = NULL;

	buf_ptr = address_buffer_ipv4;

	for (int i = 0; i < strlen(buf_ptr); i++) {

		if (buf_ptr[i] == '.')
			buf_ptr[i] = ',';
	}

	sprintf(complete_address_buffer,"(%s,%d,%d)",address_buffer_ipv4,\
		port/256,port%256);
	/*
	 * Deallocate resources, and return the
	 * formatted IP Address and Port
	 */
	freeifaddrs(ifAddrStruct);
	return (complete_address_buffer);
}

/*
 * Returns a file descriptor that holds
 * a socket connection to the specified IP Address and port.
 * Used in conjunction with active mode transfers where
 * we would like to connect to the same client IP Address,
 * but in a different port.
 */
int
get_active_client_connection(const char * ip_address, const char * port) {

	printf("Chosen port: %s\n",port);
	printf("Chosen IP address: %s\n", ip_address);
	fflush(stdout);
	/*
	 * Initialize various structures and parameters
	 * used for the getaddrinfo/4 function
	 */
	int err, fd = -1;
	struct addrinfo hints;
	struct addrinfo * res;
	struct addrinfo * res_original;
	memset(&hints, 0, sizeof (struct addrinfo));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	/*
	 * Get the socket address structure corresponding
	 * to the given IP Address and port
	 */
	err = getaddrinfo(ip_address, port, &hints, &res_original);
	if (err) {

		return (-1);
	}

	/*
	 * Parse the returned structures until
	 * we reach one that is either IPV4 or IPV6 format
	 */
	for (res = res_original; res != NULL; (res = res->ai_next)) {

		if (res->ai_family != AF_INET6)
			continue;
		else
			break;
	}

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd == -1)
		error("Error in IPV6 socket \
			establishment in active client connection.\n");
	err = connect(fd, res->ai_addr, res->ai_addrlen);
	if (err < 0)
		error("Error in 'connect'ing to the \
			active client port with IPV6.\n");

	/*
	 * Return the obtained socket file descriptor
	 * corresponding to our active client connection
	 */
	return (fd);
}

/*
 * Reads a line from the inputted file and outputs
 * the contents into the buffer
 */
int readline(FILE *f, char *buffer, int len) {

	int counter = 0;
	int new_length = len;

	int c = fgetc(f);
	while (!((feof(f)) || (c == '\r') || (c == '\n'))) {

		buffer[counter] = c;

		if ((counter + 1) > len) {

			buffer = realloc(buffer, counter + 4096);
			new_length = counter + 4096;
		}
		counter++;
		c = fgetc(f);
	}

	if (feof(f))
		return (-1);
	else {

		return (new_length);
	}
}
