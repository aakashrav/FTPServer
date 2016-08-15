#include "ftp_functions.h"
#include "utils.h"


static const command_matcher_t commands[] =
{ {"USER", USER_HANDLER},
	{"PASS", PASS_HANDLER},
	{"SYST", SYST_HANDLER},
	{"FEAT", FEAT_HANDLER},
	{"PWD", PWD_HANDLER},
	{"PASV", PASV_HANDLER},
	{"PASS", PASV_HANDLER},
	{"EPSV", EPSV_HANDLER},
	{"CWD", CWD_HANDLER},
	{"PORT", PORT_HANDLER},
	{"TYPE", TYPE_HANDLER},
	{"LIST", LIST_HANDLER},
	{"EPRT", LIST_HANDLER},
	{"RETR", RETR_HANDLER},
	{"STOR", STOR_HANDLER},
	{"APPE", APPE_HANDLER},
	{"RMD", RMD_HANDLER},
	{"MKD", MKD_HANDLER},
	{"QUIT", QUIT_HANDLER},
};

/*
 * Returns the appropriate handler
 * for a certain
 *	command from the client
 */
void
(*get_handler(char * command))(client_context_t *)
{
	/*
	 * Go through the handlers and return the appropriate one
	 */
	for (int i = 0; i < (sizeof (commands)/ sizeof (commands[0])); i++) {

		if (!(strcmp(command, commands[i].command)))
			return (commands[i].handler);

	}

	// Case where no handler was found for the command.
	return (OTHER_HANDLER);
}

/*
 * Initlialize mutexes,
 * conditional variables,
 * and job queue
 */
int
init() {
	int err;
	available_jobs = 0;

	job_queue_lock = calloc(1, sizeof (pthread_mutex_t));
	if (job_queue_lock == NULL)
		error("Error on allocaitng job queue lock\n");
	err = pthread_mutex_init(job_queue_lock, NULL);
	if (err != 0)
		error("Error on initializing job queue mutex\n");

	job_available = calloc(1, sizeof (pthread_cond_t));
	if (job_available == NULL)
		error("Error on allocating job available condition!\n");
	err = pthread_cond_init(job_available, NULL);
	if (err != 0)
		error("Error on initializing job_available condition\n");

	head = calloc(1, sizeof (struct job));
	if (head == NULL)
		error("Error on allocating job queue\n");

	return (0);
}

/*
 * FTP_Thread - the core business logic that
 * processes FTP commands
 * and executes them.
 */
void *
ftp_thread(void * args) {
	int err;
	// First the thread tries to retrieve a job from the job queue
	while (1) {
		// Lock the job queue
		err = pthread_mutex_lock(job_queue_lock);

		if (err != 0) {
			error("Error on locking job queue thread\n");
		}

		// Wait while there are no jobs available
		while (available_jobs == 0) {
			pthread_cond_wait(job_available, job_queue_lock);
		}

		// Dequeue the latest job and process
		job_t client = dequeue(head);

		// Decrement the number of jobs available
		available_jobs--;

		// Unlock the job queue and continue processing
		err = pthread_mutex_unlock(job_queue_lock);
		if (err != 0) {
			error("Error unlocking job queue\n");
		}

		/*
		 * Where the actual FTP processing takes place. We
		 * process client commands and
		 * respond with appropriate server actions.
		 */

		/*
		 * Keep a context structure to hold the current
		 * state of the communication with the client
		 */
		client_context_t current_context;
		current_context.client_comm_fd = client.fd;
		/*
		 * Keep a boolean that indicates whether the client is currently
		 * communicating via active or passive FTP.
		 * Default is active mode.
		 */
		current_context.active_flag = 1;
		/*
		 * Keep track of whether the client has requested ASCII
		 * file transfer or binary file transfer. At the beginning,
		 * we start with ASCII file transfer mode.
		 */
		current_context.binary_flag = 0;
		// Port to listen for connections in passive mode.
		current_context.data_port = -1;
		// File descriptor for passive mode listening.
		current_context.data_fd = -1;

		/*
		 * A variable to keep track of the program's
		 * current working directory
		 */
		char * new_path = calloc(strlen(getenv("PWD"))+1, 1);
		strcat(new_path, getenv("PWD"));
		strcat(new_path, "/");

		current_context.current_working_directory = new_path;
		current_context.PORT = NULL;
		current_context.client_addr = client.client_addr;
		/*
		 * File descriptor for 'accept'ing
		 * client connection
		 * in passive mode.
		 */
		current_context.client_data_fd = 0;

		ssize_t nwrite, nread;
		// Send a welcome message to the client
		nwrite = write(current_context.client_comm_fd,
			"220 CoolFTPServer\r\n",
			strlen("200 CoolFTPServer\r\n"));
		if (nwrite < 0)
			error("Error on writing initial \
				connection success status to \
				client\n");

		// Initialize connection buffer
		char buf[4096];
		char * buf_ptr = buf;
		memset(buf_ptr, 0, sizeof (buf));

		/*
		 * While the client is sending us a message,
		 * process the message.
		 */
		while ((nread = read(client.fd, buf_ptr, sizeof (buf))) > 0) {
			/*
			 * Remove extraneous characters
			 * that don't
			 * affect the actual command
			 */
			int degenerate = 1;
			for (int i = 0; i < sizeof (buf); i++) {
				if (buf[i] != '\r')
					degenerate = 0;
			}

			if (degenerate)
				error("Error: client sent \
				degenerate message\n");

			buf[strcspn(buf_ptr, "\r\n")] = 0;
			// Obtain the command name
			char * command = strtok(buf_ptr, " ");

			print_debug("\nClient input: ");
			print_debug(command);
			current_context.input_command = command;
			void (*handler)(client_context_t * current_context) =
				get_handler(command);
			handler(&current_context);

			/*
			 * Reset the buffer to receive the
			 * next command,
			 * removing any residual characters
			 */
			memset(buf_ptr, 0, sizeof (buf));
			print_debug("Active flag\n");
			printf("%d", current_context.active_flag);
			print_debug("\n");
		}

		/*
		 * Client has either closed his/her side of
		 * the connection or we encountered a
		 * connection failure, therefore we close the connection
		 */
		close(client.fd);

		print_debug("Client connection stopped or failed!\n");

		// Deallocate certain buffers
		free(new_path);
		new_path = NULL;
		free(current_context.PORT);
		current_context.PORT = NULL;

		/*
		 * Thread will continue to wait for further
		 * client connections to process
		 */
		continue;
	}
}

// Handler function for the USER FTP command
void
USER_HANDLER(client_context_t * current_context) {
	// Ask the client for his or her password.
	int nwrite = write(current_context->client_comm_fd,
		"331 Password required for USER\r\n",
		strlen("331 Password required for USER\r\n"));
	if (nwrite < 0)
		error("Error on accepting username\n");
}

/*
 * Handler function for the PASS FTP command
 */
void
PASS_HANDLER(client_context_t * current_context) {
	/*
	 * We implement anonymous FTP,
	 * so any password is accepted
	 * without actually verifying it.
	 */
	int nwrite = write(current_context->client_comm_fd,
		"230 You are now logged in.\r\n",
		strlen("230 You are now logged in.\r\n"));
	if (nwrite < 0)
		error("Error on accepting user password\n");
}


// Handler function for the SYST FTP command
void
SYST_HANDLER(client_context_t * current_context) {
	/*
	 * Inform the client of the
	 * operating system
	 * that the server runs on
	 */
	int nwrite = write(current_context->client_comm_fd, "215 UNIX\r\n",
		strlen("215 UNIX\r\n"));
	if (nwrite < 0)
		error("Error on sending operating system type\n");
}

/*
 * Handler function for the
 * FEAT FTP command
 */
void
FEAT_HANDLER(client_context_t * current_context) {
	/*
	 * Inform the client of the
	 * FTP Extensions that our server supports
	 */
	int nwrite = write(current_context->client_comm_fd,
		"211 Extensions supported\r\n",
		strlen("211-Extensions supported\r\n"));
	if (nwrite < 0)
		error("Error on sending extensions\n");
}

// Handler function for the QUIT FTP command
void
QUIT_HANDLER(client_context_t * current_context) {
	int nwrite = write(current_context->client_comm_fd, "Goodbye.\r\n",
			strlen("Goodbye.\r\n"));
	if (nwrite < 0)
		error("Error on wishing client goodbye.\n");
	close(current_context->client_comm_fd);
}

/*
 * Handler function if the client
 * issues a command
 * that is not recognized by the FTP protocol.
 */
void
OTHER_HANDLER(client_context_t * current_context) {
	print_debug("Unsupported command issued by client\n");

	/*
	 * Inform the client that the command
	 * he/she sent is
	 * not suppported by the server
	 */
	int nwrite = write(current_context->client_comm_fd,
		"500 Command not supported\r\n",
		strlen("500 Command not supported\r\n"));
	if (nwrite < 0)
		error("Error in informing client of \
			unsupported command.\n");
}

// Handler function for the PWD FTP command
void
PWD_HANDLER(client_context_t * current_context) {
	/*
	 * Print the current working directory of the server,
	 * along with the associated status codes
	 */
	char * working_directory = current_context->current_working_directory;
	char * full_message = calloc(strlen("257 \r\n") \
		+ strlen(working_directory)+2, 1);
	strcat(full_message, "257 ");
	strcat(full_message, "\"");
	strcat(full_message, working_directory);
	strcat(full_message, "\"");
	strcat(full_message, "\r\n");
	ssize_t nwrite = write(current_context->client_comm_fd, full_message,
		strlen(full_message));
	if (nwrite < 0)
		error("Error on sending current working directory to client\n");

	print_debug("Wrote working directory to client: ");
	print_debug(full_message);
	print_debug("\n");

	free(full_message);
}

// Handler function for the PASV FTP command
void
PASV_HANDLER(client_context_t * current_context) {
	print_debug("Client issued command PASV!\n");

	// Choose a random port to listen for data connections
	current_context->data_port = get_random_port();

	// Generate a socket that is listening on the randomly generated port
	current_context->data_fd = initiate_server(current_context->data_port);
	if (current_context->data_fd < 0)
		error("Error initiating passive FTP socket!\n");

	/*
	 * Get formatted IP Address + Port to send to the client.
	 * We also specify that we require IPv4 only, since we are not in
	 * extended passive mode.
	 */
	char * local_ip_address =
		get_formatted_local_ip_address(current_context->data_port, 1);

	if (local_ip_address == NULL)
		error("Error on getting formatted local IP Address\n");

	/*
	 * Construct status message for client,
	 * informing him/her of the
	 * local endpoint of the passive FTP
	 */
	char * full_client_message =
	(char *) calloc(strlen("227 Entering passive mode. \r\n") + \
		strlen(local_ip_address), 1);
	strcat(full_client_message, "227 Entering passive mode. ");
	strcat(full_client_message, local_ip_address);
	strcat(full_client_message, "\r\n");
	ssize_t nwrite = write(current_context->client_comm_fd,
		full_client_message,
		strlen(full_client_message));
	if (nwrite < 0)
		error("Error on sending passive \
			server mode status \
			to client\n");

	// We switch active mode off and deallocate resources
	current_context->active_flag = 0;
	printf("New active flag: %d\n", current_context->active_flag);
	free(full_client_message);
	full_client_message = NULL;
	free(local_ip_address);
	local_ip_address = NULL;
}

// Handler function for the EPSV FTP command
void
EPSV_HANDLER(client_context_t * current_context) {
	print_debug("Client issued command EPSV!\n");

	// Choose a random port to listen for data connections
	current_context->data_port = get_random_port();

	/*
	 * Generate a socket that is listening on the
	 * randomly generated port
	 */
	current_context->data_fd = initiate_server(current_context->data_port);
	if (current_context->data_fd < 0)
		error("Error initiating passive FTP socket!\n");

	/*
	 * Get formatted IP Address + Port
	 * to send to the client.
	 */
	char * local_ip_address =
		get_formatted_local_ip_address(current_context->data_port, 0);

	if (local_ip_address == NULL)
		error("Error on getting formatted local IP Address\n");

	/*
	 * Construct status message for client,
	 * informing him/her of the
	 * local endpoint of the passive FTP
	 */
	char * full_client_message =
	(char *) calloc(strlen("229 Entering passive mode. \r\n")
	+ strlen(local_ip_address), 1);
	strcat(full_client_message, "229 Entering passive mode. ");
	strcat(full_client_message, local_ip_address);
	strcat(full_client_message, "\r\n");
	ssize_t nwrite = write(current_context->client_comm_fd,
		full_client_message, strlen(full_client_message));
	if (nwrite < 0)
		error("Error on sending passive server mode\n");

	// We switch active mode off and deallocate resources
	current_context->active_flag = 0;
	free(full_client_message);
	full_client_message = NULL;
	free(local_ip_address);
	local_ip_address = NULL;
}

// Handler function for the CWD FTP command
void
CWD_HANDLER(client_context_t * current_context) {
	print_debug("Client issued command CWD\n");

	/*
	 * Obtain the exact directory the client would like
	 * to switch to via strtok()
	 */
	current_context->input_command = strtok(NULL, " ");

	print_debug("Switching to directory ");
	print_debug(current_context->input_command);
	print_debug(" at the request of the client\n");

	char * new_path = calloc(
		strlen(current_context->current_working_directory)
		+ strlen(current_context->input_command) + 2, 1);

	strcat(new_path, current_context->current_working_directory);
	strcat(new_path, "/");
	strcat(new_path, current_context->input_command);
	strcat(new_path, "/");

	ssize_t nwrite = 0;
	// Switch to the inputted directory
	int err = chdir(new_path);
	if (err < 0) {
		nwrite = write(current_context->client_comm_fd, "550\r\n",
			strlen("550\r\n"));
		free(new_path);
		new_path = NULL;
	} else {
		nwrite = write(current_context->client_comm_fd,
			"250\r\n", strlen("250\r\n"));
		free(current_context->current_working_directory);
		current_context->current_working_directory = NULL;
		current_context->current_working_directory = new_path;
	}

	if (nwrite < 0)
		error("Error on writing CWD status to client\n");
}

// Handler function for the PORT FTP command
void
PORT_HANDLER(client_context_t * current_context) {
	print_debug("Client issued command PORT!\n");

	/*
	 * Get IP Address + port name, formatted according to
	 * the norms of the RFC
	 * FTP standards
	 */
	current_context->input_command = strtok(NULL, " ");

	print_debug("Client port for active FTP: ");
	print_debug(current_context->input_command);
	print_debug("\n");

	// Separate IP Address from port name based on commas
	strtok(current_context->input_command, ",");
	// Read until we get the two numbers corresponding to the port
	for (int i = 0; i < 3; i++)
		strtok(NULL, ",");

	/*
	 * Obtain the upper bits and lower bits
	 *	of the port
	 * number in binary
	 */
	int upper_bits = atoi(strtok(NULL, ","));
	int lower_bits = atoi(strtok(NULL, ","));

	// Calculate the decimal version of the port number
	int calculated_port = (upper_bits << 8) + lower_bits;

	// Free the old port
	free(current_context->PORT);
	current_context->PORT = NULL;
	// Allocate a new buffer for the new port
	current_context->PORT = calloc(6, 1);
	sprintf(current_context->PORT, "%d", calculated_port);

	// Send successful active FTP activation confirmation to client
	ssize_t nwrite = write(current_context->client_comm_fd,
		"200 Entering active mode\r\n",
		strlen("200 Entering active mode\r\n"));

	if (nwrite < 0)
		error("Error on writing active FTP success \
			status to client\n");

	// Switch the active FTP flag on
	current_context->active_flag = 1;
}

// Handler function for the TYPE FTP command
void
TYPE_HANDLER(client_context_t * current_context) {
	print_debug("Client issued command TYPE!\n");

	// Get the type of change to the binary flag
	current_context->input_command = strtok(NULL, " ");

	// ASCII Type
	if (strcmp(current_context->input_command, "A") == 0) {
		current_context->binary_flag = 0;
		// Send successful binary flag update status to client
		ssize_t nwrite = write(current_context->client_comm_fd,
			"200 Entering ASCII mode\r\n",
			strlen("200 Entering ASCII mode\r\n"));

	if (nwrite < 0)
		error("Error on writing binary type success \
			status to client\n");
	}
	// Binary type
	else {
		current_context->binary_flag = 1;
		// Send successful binary flag update status to client
		ssize_t nwrite = write(current_context->client_comm_fd,
			"200 Entering binary mode\r\n",
			strlen("200 Entering binary mode\r\n"));

	if (nwrite < 0)
		error("Error on writing binary \
			type success status to client\n");
	}
}

// Handle for the LIST FTP command
void
LIST_HANDLER(client_context_t * current_context) {
	print_debug("Client issued command LIST!\n");

	ssize_t nwrite;
	int err;
	// Get the contents of the current working directory
	char * directory_list =
		LIST(current_context->current_working_directory);

	/*
	 * Format and send the status message to the client,
	 * along with the contents
	 * of the current working directory
	 */
	char * full_message = calloc(strlen(directory_list)+strlen("\r\n"), 1);
	strcat(full_message, directory_list);
	strcat(full_message, "\r\n");

	/*
	 * Handle the case where the client has currently
	 * chosen passive mode to be their
	 * desired form of data transfer
	 */
	if (!current_context->active_flag) {
		struct sockaddr_storage temp;
		socklen_t len = (socklen_t)sizeof (struct sockaddr_storage);
		/*
		 * 'Accept' the incoming client connection to our
		 * established socket
		 */
		current_context->client_data_fd =
			accept(current_context->data_fd,
				(struct sockaddr *)&temp,
				&len);

		if (current_context->client_data_fd < 0)
			error("Error on accepting passive client connection \
				for data transfer during LIST\n");
		else
			nwrite =
			write(current_context->client_comm_fd,
				"150 Opening ASCII mode \
				data connection\r\n",
				strlen("150 Opening ASCII \
					mode data connection\r\n"));

		nwrite = write(current_context->client_data_fd,
			full_message, strlen(full_message));

		if (nwrite < 0)
			error("Error when sending directory contents to \
				client in response to LIST command\n");


		/*
		 * Close the socket descriptors that connect to the client.
		 * Note that this ends the current passive mode connection;
		 * a new connection will have to be established
		 * before performing further transfers
		 */
		close(current_context->client_data_fd);
		close(current_context->data_fd);
	}

	/*
	 * Handle the case where the client has currently chosen
	 *  active mode to be their
	 * desired form of data transfer
	 */
	else {
		socklen_t len = sizeof (struct sockaddr_storage);

		/*
		 * Keep track of the IP address and the port of the client:
		 * we will be transferring the bytes
		 * of the file to this port
		 */
		char hoststr[NI_MAXHOST];
		char portstr[NI_MAXSERV];

		/*
		 * Obtain the IP Address
		 * and port
		 * number of the current
		 * client connection
		 */
		err = getnameinfo(
			(struct sockaddr *)&(current_context->client_addr),
			len, hoststr, sizeof (hoststr),
			portstr, sizeof (portstr),
			NI_NUMERICHOST | NI_NUMERICSERV);

		/*
		 * Handle error situations in processing
		 * the client connection
		 */
		if (err != 0) {
			nwrite = write(current_context->client_comm_fd,
				"425 can't open data \
				connection\r\n",
				strlen("425 can't open data connection\r\n"));
			if (nwrite < 0)
				error("Error on communicating data connection \
					error to client\n");
			return;
		}

		/*
		 * Successfully obtained information
		 * regarding the
		 * client's connection
		 */
		else {
			/*
			 * Use a helper function to connect
			 * to the client's IP Address,
			 * but in a *different* port than the
			 * regular command transfers.
			 * This different port,
			 * used for data transfers, was provided earlier via
			 * the PORT command
			 */
			current_context->data_fd =
			get_active_client_connection(hoststr,
				current_context->PORT);

			/*
			 * If we for some reason fail
			 * when trying to connect to the
			 * client port for active FTP, we inform the client
			 */
			if (current_context->data_fd < 0) {
				nwrite = write(current_context->client_comm_fd,
					"425 can't open active \
					data connection\r\n",
					strlen("425 can't open\
					 active data connection\r\n"));
				return;
			}
			else
				nwrite = write(current_context->client_comm_fd,
					"150 Opening ASCII mode \
					data connection\r\n",
					strlen("150 Opening ASCII mode\
					 data connection\r\n"));
		}

		nwrite = write(current_context->data_fd, full_message,
			strlen(full_message));

		if (nwrite < 0)
			error("Error when sending directory contents to client \
				in response to LIST command.\n");

		// Close the associated file descriptors
		close(current_context->data_fd);
	}

	nwrite = write(current_context->client_comm_fd,
		"226 Directory contents listed\r\n",
		strlen("226 Directory contents listed\r\n"));
	if (nwrite < 0)
		error("Error when writing LIST success status to client.\n");

	// Deallocate resources
	free(full_message);
	full_message = NULL;
	free(directory_list);
	directory_list = NULL;
}

// Handle for the STOR FTP command
void
STOR_HANDLER(client_context_t * current_context) {
	print_debug("Client has issued command STOR!\n");

	ssize_t nwrite;
	int err;
	// Obtain the filename of the file to be created
	char * filename = strtok(NULL, " ");

	// Declare the file descriptor corresponding to the new file
	int file_fd;

	/*
	 * Erase the old contents of the file so
	 * that may append new client
	 * content
	 */
	err = truncate(filename, 0);

	/*
	 * If the error is of type ENOENT, it means that the
	 * truncation failed due to us passing a non-existent file as
	 * argument. Such a situation is acceptable as we will create
	 * a new file anyway. If we recieve another error however,
	 * we have to inform the client and abort
	 * the procedure
	 */
	if (err < 0) {
		if (errno != ENOENT) {
			// Inform the client of the error during truncation
			nwrite = write(current_context->client_comm_fd,
				"452 file unavailable\r\n",
				strlen("452 file unavailable\r\n"));

			if (nwrite < 0)
				error("Error in communicating truncation error \
					to the client.\n");
			return;
		}
	}

	/*
	 * Create the file descriptor with the O_EXCL
	 * flag since we don't want
	 * another process to open the file at the same time.
	 */
	file_fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0644);

	// An error occured in opening the file descriptor
	if (file_fd < 0) {
		/*
		 * The situation where another process was trying to open
		 * the same file descriptor
		 */
		if (errno == EEXIST) {
			print_debug("ERROR: Concurrent creation\
				of the same file \
				descriptor! \n");

			// Inform the client of the error
			nwrite = write(current_context->client_comm_fd,
				"452 file unavailable\r\n",
				strlen("452 file unavailable\r\n"));
			if (nwrite < 0)
				error("Error in communicating file concurrent \
					access to the client.\n");
			return;
		}
	}

	/*
	 * Inform the client that we are ready to
	 * transfer the bytes to
	 * store a file
	 */
	nwrite = write(current_context->client_comm_fd,
		"150 Opening file transfer data connection\r\n",
		strlen("150 Opening file transfer data connection\r\n"));

	if (nwrite < 0)
		error("Error on communicating ASCII \
			file transfer to client.\n");

	/*
	 * Handle the case where the client has currently chosen
	 * passive mode to be their
	 * desired form of data transfer
	 */
	if (!current_context->active_flag) {
		struct sockaddr_storage temp;
		socklen_t len = (socklen_t)sizeof (struct sockaddr_storage);
		/*
		 * 'Accept' the incoming client connection
		 * to our established socket
		 */
		current_context->client_data_fd =
			accept(current_context->data_fd,
				(struct sockaddr *)&temp, &len);

		if (current_context->client_data_fd < 0)
			error("Error on accepting\
				client channel for data transfer.\n");

		/*
		 * Call the STOR command, and if we encounter
		 * an error during
		 * data transfer, we inform the client
		 */
		err = STOR(file_fd, current_context->client_data_fd,
			current_context->binary_flag);

		if (err < 0)
			nwrite = write(current_context->client_comm_fd,
				"451 Local error in file processing\r\n",
				strlen("451 Local error\
				 in file processing\r\n"));
		else
			nwrite = write(current_context->client_comm_fd,
				"226 Transfer complete\r\n",
				strlen("226 Transfer complete\r\n"));

		/*
		 * Close the file descriptor to the file, and the
		 * socket descriptors that connect to the client.
		 * Note that this ends the current passive mode connection;
		 * a new connection will have to be established
		 * before performing further transfers
		 */
		close(file_fd);
		close(current_context->client_data_fd);
		close(current_context->data_fd);
	}

	/*
	 * Handle the case where the client has currently
	 * chosen active mode to be their
	 *	desired form of data transfer
	 */
	else {
		socklen_t len = sizeof (struct sockaddr_storage);

		/*
		 * Keep track of the IP address and the port of the client:
		 * we will be transferring the bytes
		 * of the file to this port
		 */
		char hoststr[NI_MAXHOST];
		char portstr[NI_MAXSERV];

		/*
		 * Obtain the IP Address and port
		 * number of the
		 * current client connection
		 */
		err =
		getnameinfo((struct sockaddr *)&(current_context->client_addr),
			len, hoststr,
			sizeof (hoststr), portstr, sizeof (portstr),
			NI_NUMERICHOST | NI_NUMERICSERV);

		// Handle error situations in processing the client connection
		if (err != 0) {
			nwrite = write(current_context->client_comm_fd,
				"425 can't open data connection\r\n",
				strlen("425 can't open data connection\r\n"));
			if (nwrite < 0)
				error("Error on communicating data connection \
					error to client.\n");
			return;
		}

		/*
		 * Successfully obtained information regarding the client's
		 * connection
		 */
		else {
			/*
			 * Use a helper function to connect
			 * to the client's IP Address,
			 * but in a *different* port than the
			 * regular command transfers.
			 * This different port, used for data transfers, was
			 * provided earlier via the PORT command.
			 */
			current_context->data_fd =
			get_active_client_connection(hoststr,
				current_context->PORT);

			/*
			 * If we for some reason fail when trying to connect to
			 * the client port for active FTP, we inform the client
			 */
			if (current_context->data_fd < 0) {
				nwrite = write(current_context->client_comm_fd,
					"425 can't open data connection\r\n",
					strlen("425 can't open\
						data connection\r\n"));
				return;
			}
		}

		/*
		 * After obtaining the active client connection,
		 * we call the STOR function to
		 * write the bytes of the client connection into the file
		 */
		err = STOR(file_fd, current_context->data_fd,
			current_context->binary_flag);

		if (err < 0)
			nwrite = write(current_context->client_comm_fd,
				"451 Local error in processing\r\n",
				strlen("451 Local error in processing\r\n"));
		else
			nwrite = write(current_context->client_comm_fd,
				"226 Transfer complete\r\n",
				strlen("226 Transfer complete\r\n"));

		// Close the associated file descriptors
		close(file_fd);
		close(current_context->data_fd);
	}

}

// Handle for the FTP APPE Command
void
APPE_HANDLER(client_context_t * current_context) {
	print_debug("Client has issued command APPE!\n");

	ssize_t nwrite;
	int err;
	// Obtain the filename of the file to be created
	char * filename = strtok(NULL, " ");

	// Declare the file descriptor corresponding to the new file
	int file_fd;

	// Open a new file descriptor to append to the file
	file_fd = open(filename, O_CREAT | O_APPEND, 0644);

	// Error in opening file descriptor, so we inform the client
	if (file_fd < 0) {
		nwrite =
		write(current_context->client_comm_fd,
			"452 file unavailable\r\n", strlen("452 file \
				unavailable\r\n"));
		if (nwrite < 0)
			error("Error on communicating file \
				unavailable to client.\n");
	}

	// Else we inform client of successful opening of file for appending
	else {
		nwrite = write(current_context->client_comm_fd,
			"150 Opening file transfer data connection\r\n",
			strlen("150 Opening file transfer\
				data connection\r\n"));

		if (nwrite < 0)
			error("Error on communicating APPE file opening \
				success to client.\n");
	}

	/*
	 * Handle the case where the client has currently chosen passive
	 * mode to be their
	 * desired form of data transfer
	 */
	if (!current_context->active_flag) {
		struct sockaddr_storage temp;
		socklen_t len = (socklen_t)sizeof (struct sockaddr_storage);

		/*
		 * 'Accept' the incoming client
		 * connection to our established socket
		 */
		int client_data_fd = accept(current_context->data_fd,
			(struct sockaddr *)&temp, &len);

		if (client_data_fd < 0)
			error("Error on accepting client channel\
				for data transfer.\n");

		/*
		 * Using append mode, we again call the STOR command, and
		 * if we encounter an error during data transfer, we
		 * inform the client
		 */
		err = STOR(file_fd,
			client_data_fd,
			current_context->binary_flag);
		if (err < 0)
			nwrite = write(current_context->client_comm_fd,
				"451 Local error in file processing\r\n",
				strlen("451 Local error \
					in file processing\r\n"));
		else
			nwrite = write(current_context->client_comm_fd,
				"226 Transfer complete\r\n",
				strlen("226 Transfer complete\r\n"));

		/*
		 * Close the file descriptor to the file, and the socket
		 * descriptors that connect to the client.
		 * Note that this ends the current passive mode connection;
		 * a new connection will have to be established
		 * before performing further transfers
		 */
		close(file_fd);
		close(client_data_fd);
		close(current_context->data_fd);
	}

	/*
	 * Handle the case where the client has
	 * currently chosen active mode to be their
	 * desired form of data transfer
	 */
	else {
		socklen_t len = sizeof (struct sockaddr_storage);

		/*
		 * Keep track of the IP address and the port of the client:
		 * we will be transferring the bytes
		 * of the file to this port
		 */
		char hoststr[NI_MAXHOST];
		char portstr[NI_MAXSERV];

		/*
		 * Obtain the IP Address and
		 * port number of the current
		 * client connection
		 */
		err =
		getnameinfo((struct sockaddr *)&(current_context->client_addr),
			len, hoststr,
			sizeof (hoststr), portstr, sizeof (portstr),
			NI_NUMERICHOST | NI_NUMERICSERV);

		// Handle error situations in processing the client connection
		if (err != 0) {
			nwrite = write(current_context->client_comm_fd,
				"425 can't open data connection\r\n",
				strlen("425 can't open data connection\r\n"));
			if (nwrite < 0)
				error("Error on communicating data\
				 connection error \
					to client.\n");
			return;
		}

		/*
		 * Successfully obtained information regarding the
		 * client's connection
		 */
		else {
			/*
			 * Use a helper function to connect to the
			 * client's IP Address,
			 * but in a *different* port than the
			 * regular command transfers.
			 * This different port, used for data transfers, was
			 * provided earlier via the PORT command
			 */
			current_context->data_fd =
			get_active_client_connection(hoststr,
				current_context->PORT);

			/*
			 * If we for some reason fail when trying to connect
			 * to the client port for active FTP,
			 * we inform the client
			 */
			if (current_context->data_fd < 0)
			{
				nwrite = write(current_context->client_comm_fd,
					"425 can't open data connection\r\n",
					strlen("425 can't open \
						data connection\r\n"));
				return;
			}
		}

		/*
		 * After obtaining the active
		 * client connection,
		 * we call the STOR function with
		 * our file descriptor in 'append' mode
		 * to write the bytes of the client connection into the file
		 */
		err = STOR(file_fd, current_context->data_fd,
			current_context->binary_flag);

		if (err < 0)
			nwrite = write(current_context->client_comm_fd,
				"451 Local error in processing\r\n",
				strlen("451 Local error in processing\r\n"));
		else
			nwrite = write(current_context->client_comm_fd,
				"226 Transfer complete\r\n",
				strlen("226 Transfer complete\r\n"));

		// Close the associated file descriptors
		close(file_fd);
		close(current_context->data_fd);
	}

}

// Handle for the RETR FTP command
void
RETR_HANDLER(client_context_t * current_context) {
	print_debug("Client has issued command RETR!\n");

	ssize_t nwrite;
	int err;
	// Get the specific filename for retrieval
	char * filename = strtok(NULL, " ");

	/*
	 * Open the file, returning an error to the
	 * client if something went wrong
	 */
	int file_fd = open(filename, O_RDONLY);
	if (file_fd < 0) {
		nwrite = write(current_context->client_comm_fd,
			"550 Error during file access \r\n",
			strlen("550 Error during file access \r\n"));
		if (nwrite < 0)
			error("Error on communicating \
				file access error code to client.\n");
	}

	// Inform the client that we are ready to transfer the requested file.
	else {
		nwrite = write(current_context->client_comm_fd,
			"150 Opening file transfer data connection\r\n",
			strlen("150 Opening file transfer \
				data connection\r\n"));

		if (nwrite < 0)
			error("Error on communicating file transfer \
				information to client.\n");
	}

	/*
	 * Handle the case where the client has currently
	 * chosen passive mode to be their
	 * desired form of data transfer
	 */
	if (!current_context->active_flag) {
		struct sockaddr_storage temp;
		socklen_t len = (socklen_t)sizeof (struct sockaddr_storage);
		/*
		 * 'Accept' the incoming client connection
		 * to our established socket
		 */
		int client_data_fd = accept(current_context->data_fd,
			(struct sockaddr *)&temp, &len);
		if (client_data_fd < 0)
			error("Error on accepting passive \
				client connection for data transfer\
				during RETR.\n");

		/*
		 * Call the RETR command, and if we encounter
		 * an error during data transfer,
		 * we inform the client
		 */
		err = RETR(file_fd,
			client_data_fd,
			current_context->binary_flag);
		if (err < 0)
			nwrite = write(current_context->client_comm_fd,
				"451 Local error in file\
				processing\r\n",
				strlen("451 Local error\
				 in file processing\r\n"));
		else
			nwrite = write(current_context->client_comm_fd,
				"226 Transfer complete\r\n",
				strlen("226 Transfer complete\r\n"));

		/*
		 * Close the file descriptor to the file,
		 * and the socket descriptors that connect to the client.
		 * Note that this ends the current passive mode connection;
		 * a new connection will have to be established
		 * before performing further transfers
		 */
		close(file_fd);
		close(client_data_fd);
		close(current_context->data_fd);
	}

	/*
	 * Handle the case where the client has currently chosen
	 * active mode to be their
	 * desired form of data transfer
	 */
	else {
		socklen_t len = sizeof (struct sockaddr_storage);

		/*
		 * Keep track of the IP address and the port of the client:
		 * we will be transferring the bytes
		 * of the file to this port
		 */
		char hoststr[NI_MAXHOST];
		char portstr[NI_MAXSERV];

		/*
		 * Obtain the IP Address and port number of the
		 * current client connection
		 */
		err =
		getnameinfo((struct sockaddr *)&(current_context->client_addr),
			len, hoststr, sizeof (hoststr),
			portstr, sizeof (portstr),
			NI_NUMERICHOST | NI_NUMERICSERV);

		// Handle error situations in processing the client connection
		if (err != 0) {
			nwrite = write(current_context->client_comm_fd,
				"425 can't open data connection\r\n",
				strlen("425 can't open data connection\r\n"));
			if (nwrite < 0)
				error("Error on communicating data \
					connection error to client.\n");
			return;
		}

		/*
		 * Successfully obtained information regarding the
		 * client's connection
		 */
		else {
			/*
			 * Use a helper function to connect
			 * to the client's IP Address,
			 * but in a *different* port than the
			 * regular command transfers.
			 * This different port, used for data transfers, was
			 * provided earlier via the PORT command
			 */
			current_context->data_fd =
			get_active_client_connection(hoststr,
				current_context->PORT);

			/*
			 * If we for some reason fail when trying to connect
			 * to the
			 * client port for active FTP, we inform the client
			 */
			if (current_context->data_fd < 0) {
				nwrite = write(current_context->client_comm_fd,
					"425 can't open active\
					data connection\r\n",
					strlen("425 can't open active \
						data connection\r\n"));
				return;
			}
		}

		/*
		 * After obtaining the active client connection,
		 * we call the RETR function to pass the bytes
		 * of the file to the client
		 */
		err = RETR(file_fd, current_context->data_fd,
			current_context->binary_flag);
		if (err < 0)
			nwrite = write(current_context->client_comm_fd,
				"451 Local error in file processing\r\n",
				strlen("451 Local error\
					in file processing\r\n"));
		else
			nwrite = write(current_context->client_comm_fd,
				"226 Transfer complete\r\n",
				strlen("226 Transfer complete\r\n"));

		// Close the associated file descriptors
		close(file_fd);
		close(current_context->data_fd);
	}
}

// Used to accomplish the RMD FTP command
void
RMD_HANDLER(client_context_t * current_context) {
	print_debug("Client has issued command RMD!\n");

	ssize_t nwrite;
	int err;
	// Obtain the directory name for removal
	const char * dirname = strtok(NULL, " ");

	// Then simply remove the specified directory
	err = rmdir(dirname);
	if (err < 0)
		nwrite = write(current_context->client_comm_fd,
			"451 Local error in processing\r\n",
			strlen("451 Local error in processing\r\n"));
	else
		nwrite = write(current_context->client_comm_fd,
			"226 Removal complete\r\n",
			strlen("226 Removal complete\r\n"));
	if (nwrite < 0)
		error("Error on informing client\
			about successful directory\
			removal.");
}

// Used to accomplish the MKD FTP command
void
MKD_HANDLER(client_context_t * current_context) {
	print_debug("Client has issued command MKD!\n");

	ssize_t nwrite;
	int err;
	// Obtain the directory name for removal
	const char * dirname = strtok(NULL, " ");

	// Then simply make the specified new directory
	err = mkdir(dirname, 0644);
	if (err < 0)
		nwrite = write(current_context->client_comm_fd,
			"451 Local error in processing\r\n",
			strlen("451 Local error in processing\r\n"));
	else
		nwrite = write(current_context->client_comm_fd,
			"226 Directory creation complete\r\n",
			strlen("226 Directory creation complete\r\n"));
	if (nwrite < 0)
		error("Error on informing client\
			about successful directory\
			removal.");
}


/*
 * Returns a list of the contents of the inputted working directory;
 * used in conjunction with the LIST command
 */
char *
LIST(char * dir_name) {
	// Open the inputted working directory
	DIR * d;
	d = opendir(dir_name);

	/*
	 * Initialize the variable holding the list of directory contents;
	 *  max_size is an upper limit: 4096 * 3 = 12288.
	 * We will later 'realloc' it if the need arises
	 */
	long max_size = 12288;
	char * full_list = calloc(max_size, 1);
	long cur_size = 0;

	if (d == 0) {
		print_debug("Error opening directory: ");
		print_debug(dir_name);
		print_debug("\n");

		// TODO: RETURN ERROR MESSAGE TO CLIENT INSTEAD OF EXITING
		printf("%s\n", dir_name);
		fflush(stdout);
		error("Error opening directory for client LIST command");
	}

	struct dirent * cur_dir_entry;
	cur_dir_entry = readdir(d);

	while (cur_dir_entry != NULL) {

		// Ignore hidden directories or control directories
		if (cur_dir_entry->d_name[0] == '.') {
			cur_dir_entry = readdir(d);
			continue;
		}

		/*
		 * If the directory contents length goes
		 * above the allotted size,
		 * we reallocate the buffer
		 */
		if (cur_size + strlen(cur_dir_entry->d_name) > max_size-2) {
			max_size += 4096;
			full_list = realloc(full_list, max_size);
		}

		strcat(full_list, cur_dir_entry->d_name);
		strcat(full_list, "\r\n");
		cur_dir_entry = readdir(d);
	}
	closedir(d);

	// Return the directory contents
	return (full_list);
}


/*
 * Initialize a 'listen'ing server for a
 * specific port number on the local machine;
 * used in conjunction with the Passive FTP module
 */
int
initiate_server(long port) {

	int err, fd = 0;
	struct addrinfo hints;
	struct addrinfo * res;
	struct addrinfo * res_original;
	memset(&hints, 0, sizeof (struct addrinfo));

	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	char * port_pointer = NULL;
	asprintf(&port_pointer, "%ld", port);

	print_debug("Initiating server on port: ");
	print_debug(port_pointer);
	print_debug("\n");


	/*
	 * Use getaddrinfo to fill out the IP Address
	 * and port number information
	 */
	err = getaddrinfo(NULL, port_pointer, &hints, &res_original);
	if (err) {
		error(gai_strerror(err));
	}

	for (res = res_original; res != NULL; (res = res->ai_next)) {

		// Double checking so we don't get invalid values
		if (res->ai_family != AF_INET6)
			continue;

		if (res->ai_family == AF_INET6) {

			fd = socket(AF_INET6, SOCK_STREAM, 0);
			if (fd == -1)
				error("Error on socket during\
					initiate server.\n");

			err = bind(fd, res->ai_addr, res->ai_addrlen);
			if (err == -1)
				error("Error on binding during\
					initiate server.\n");

			err = listen(fd, MAX_NUM_CONNECTED_CLIENTS);
			if (err == -1)
				error("Error on listen during\
					initiate server.\n");
			break;
		}
	}

	// Free allocated resources, and return the socket file descriptor
	freeaddrinfo(res_original);
	res_original = NULL;
	free(port_pointer);
	port_pointer = NULL;
	return (fd);
}

/*
 * Helper function to store a file; it
 * receives bytes from the data file descriptor argument,
 * and writes the bytes into the file descriptor
 */
int
STOR(int file_fd, int data_fd, int binary_flag) {

	ssize_t nwrite;

	if (!binary_flag) {

		// Instantiate buffer for holding partial contents of read input
		int current_length = 4098;
		char * buf_ptr = calloc(current_length, 1);

		FILE * data_stream = fdopen(data_fd, "r+");
		if (!data_stream)
			return (-1);

		/*
		 * ASCII Mode - read the file's contents line by line,
		 * replacing new line feeds
		 * with a \r\n
		 */
		while ((current_length =
				readline(data_stream,
					buf_ptr, current_length)) != -1) {


			char * read_content = calloc(current_length+2, 1);
			sprintf(read_content, "%s\r\n", buf_ptr);

			nwrite = write(file_fd,
				read_content,
				strlen(read_content));
			if (nwrite < 0)
				return (-1);

			free(read_content);
			read_content = NULL;
			memset(buf_ptr, 0, strlen(buf_ptr));
		}

		// Make one last write to flush all residual data
		char * read_content = calloc(current_length+2, 1);
		sprintf(read_content, "%s\r\n", buf_ptr);
		nwrite = write(file_fd, read_content, strlen(read_content));
		if (nwrite < 0)
			return (-1);
		free(read_content);
		read_content = NULL;

		fclose(data_stream);
		free(buf_ptr);
		buf_ptr = NULL;
	}

	if (binary_flag) {

		// File handler and variable to hold the contents
		FILE * data_stream;
		char * contents;
		int file_size = 0;

		// Open the file stream in read binary mode.
		data_stream = fdopen(data_fd, "rb");

		// Determine the file size
		fseek(data_stream, 0L, SEEK_END);
		file_size = ftell(data_stream);
		fseek(data_stream, 0L, SEEK_SET);

		// Allocate memory for the file's contents
		contents = malloc(file_size+1);

		/*
		 * Read the file, and dump the content
		 * into the variable 'contents'
		 */
		size_t size = fread(contents, 1, file_size, data_stream);
		contents[size] = 0; // Add terminating zero.

		// Write the file's contents
		nwrite = write(file_fd, contents, size);
		if (nwrite < 0)
			return (-1);

		// Clean up
		fclose(data_stream);
		free(contents);
		contents = NULL;
	}

	return (0);
}

/*
 * Command to obtain bytes from the file descriptor,
 * and write it into the data descriptor.
 * Used in conjunction with a client request to get a file
 */
int
RETR(int file_fd, int data_fd, int binary_flag) {

	ssize_t nwrite;

	if (!binary_flag) {

		/*
		 * Instantiate buffer for holding
		 * partial contents of read input
		 */
		int current_length = 4096;
		char * buf_ptr = calloc(current_length, 1);

		FILE * file_stream = fdopen(file_fd, "r");
		if (!file_stream)
			return (-1);

		/*
		 * ASCII Mode - read the file's contents line by line,
		 * replacing new line feeds with a \r\n
		 */
		while ((current_length = readline(file_stream,
				buf_ptr, current_length)) != -1) {

			char * read_content = calloc(current_length+2, 1);
			sprintf(read_content, "%s\r\n", buf_ptr);

			nwrite =
			write(data_fd, read_content,
				strlen(read_content));
			if (nwrite < 0)
				return (-1);

			free(read_content);
			read_content = NULL;
			memset(buf_ptr, 0, strlen(buf_ptr));
		}

		// Make one last write to flush all residual data
		char * read_content = calloc(current_length+2, 1);
		sprintf(read_content, "%s\r\n", buf_ptr);
		nwrite = write(data_fd, read_content, strlen(read_content));
		if (nwrite < 0)
			return (-1);
		free(read_content);
		read_content = NULL;

		fclose(file_stream);
		free(buf_ptr);
		buf_ptr = NULL;
	}

	if (binary_flag) {

		// File handler and variable to hold the contents
		FILE * file_stream;
		char * contents;
		int file_size = 0;

		// Open the file stream in read binary mode.
		file_stream = fdopen(file_fd, "rb");

		// Determine the file size
		fseek(file_stream, 0L, SEEK_END);
		file_size = ftell(file_stream);
		fseek(file_stream, 0L, SEEK_SET);

		// Allocate memory for the file's contents
		contents = malloc(file_size+1);

		/*
		 * Read the file, and dump the
		 * content into the variable 'contents'
		 */
		size_t size = fread(contents, 1, file_size, file_stream);
		contents[size] = 0; // Add terminating zero.

		// Write the file's contents to the client
		nwrite = write(data_fd, contents, size);
		if (nwrite < 0)
			return (-1);

		// Clean up
		fclose(file_stream);
		free(contents);
		contents = NULL;
	}

	return (0);
}
