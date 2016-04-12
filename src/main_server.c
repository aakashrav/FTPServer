#include "ftp_functions.h"
#include <getopt.h>
#include <poll.h>

int quit=0;

void 
usage()
{
	printf("/ftp_server <port>");
}

int
main(int argc, char * argv[])
{
	if (argc<2)
	{
		usage();
		exit(1);
	}
	// TODO: GETOPT THIS
	long port = atol(argv[1]);
	int err;
	int server_fd = initiate_server(port);

	init();

	pthread_t arr[NUM_THREADS];
	for (int i=0; i < NUM_THREADS; i++)
	{
		pthread_create(&arr[i], NULL, ftp_thread, NULL);
	}

	struct pollfd fds[1];
	struct pollfd server_poll_structure = {server_fd, POLLIN, 0};
	fds[0] = server_poll_structure;

	while(!quit)
	{
		printf("About to poll!");
		fflush(stdout);

		err = poll(fds, 1, -1);
		if (err == -1)
			error("Error on poll!");

		printf("Finished poll'ing!");
		fflush(stdout);

		struct sockaddr_storage client_addr;
		int client_fd = -1;

		if (fds[0].revents & POLLIN)
		{
			socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
			client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);
            if (client_fd < 0)
            {
            	destroy();
            	error("Error on accepting client connection");
            }

			// Lock the job queue
			err = pthread_mutex_lock(job_queue_lock);

			if (err != 0)
			{
				destroy();
				error("Error on locking mutex");
			}

			// Enqueue a new job
			enqueue(head, client_fd, client_addr);

			// Increment the job counter
			available_jobs++;

			// Unlock the job queue
			err = pthread_mutex_unlock(job_queue_lock);
			if (err != 0)
				error("Error on unlocking thread pool");

			//Signal to any waiting threads that a new job is available
			err = pthread_cond_signal(job_available);
		}
	}

	destroy();
}