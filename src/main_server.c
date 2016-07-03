#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include "ftp_functions.h"
#include "utils.h"

// Volatile quit variable
volatile sig_atomic_t QUIT_FLAG = 0;


// Safe signal handler
void handler(int signal) {

	QUIT_FLAG = 1;
}

void
usage() {

	printf("Usage: /sftp2_server <port>\n");
	fflush(stdout);
}

int
main(int argc, char * argv[]) {

	if (argc < 2) {

		usage();
		exit(1);
	}

	// Register our signal handler for gracefully terminating the server
	if (signal(SIGUSR1, handler) == SIG_ERR) {

		error("Error registering signal handler for SIGUSR1.\n");
	}

	if (signal(SIGUSR2, handler) == SIG_ERR) {

		error("Error registering signal handler for SIGUSR2.\n");
	}

	// Obtain the port that the server will run on
	long port = atol(argv[1]);
	int err;

	// Initiate the server socket running at the specified port
	int server_fd = initiate_server(port);
	if (server_fd < 0)
		error("Error on initiating FTP server!\
			Perhaps try a new port?\n");

	// Initialize the job queue and related resources
	init();

	// Spawn NUM_THREADS amount of threads
	pthread_t arr[NUM_THREADS];
	for (int i = 0; i < NUM_THREADS; i++) {

		pthread_create(&arr[i], NULL, ftp_thread, NULL);
	}

	/*
	 * Create a server polling architecture so that we
	 * don't waste system resources on busy waits
	 */
	struct pollfd fds[1];
	struct pollfd server_poll_structure = {server_fd, POLLIN, 0};
	fds[0] = server_poll_structure;

	while (!QUIT_FLAG) {

		print_debug("Main server: About to call poll command!\n");

		err = poll(fds, 1, -1);
		if (err == -1)
			error("Error on poll command in the main server!\n");

		print_debug("Main server received connection \
			and finished poll'ing!\n");

		struct sockaddr_storage client_addr;

		// The socket required to communicate with the client directly
		int client_fd = -1;

		if (fds[0].revents & POLLIN) {

			// Accept the latest client connection
			socklen_t len =
			(socklen_t)sizeof (struct sockaddr_storage);
			client_fd =
			accept(server_fd, (struct sockaddr *)&client_addr,
				&len);
			if (client_fd < 0) {

				destroy();
				error("Error on accepting client connection\
					in main.\n");
			}

			// Lock the job queue mutex
			err = pthread_mutex_lock(job_queue_lock);

			if (err != 0) {

				destroy();
				error("Error on locking job queue mutex!\n");
			}

			// Enqueue a new job
			enqueue(head, client_fd, client_addr);

			// Increment the job counter
			available_jobs++;

			// Unlock the job queue mutex
			err = pthread_mutex_unlock(job_queue_lock);
			if (err != 0)
				error("Error on unlocking job queue mutex!\n");

			/*
			 * Signal to any waiting threads
			 * that a new job is available
			 */
			err = pthread_cond_signal(job_available);
		}
	}

	// Deallocate resources related to the job queue and associated locks
	destroy();
	return (0);
}
