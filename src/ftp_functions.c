#include "ftp_functions.h"

/* Error function to exit gracefully */
void
error(const char * message)
{
	/* Print the error message */
	perror(message);
	/* Deallocate resources related to the thread pool and mutexes */
	destroy();
	free_jobs(head);
	exit(1);
}

/* Initlialize mutexes, conditional variables, and job queue */
int
init()
{
	int err;
	available_jobs = 0;

	job_queue_lock = calloc(1, sizeof(pthread_mutex_t));
	if (job_queue_lock == NULL)
		error("Error on allocaitng job queue lock");
	err = pthread_mutex_init(job_queue_lock, NULL);
	if (err != 0)
		error("Error on initializing job queue mutex");

	job_available = calloc(1,sizeof(pthread_cond_t));
	if (job_available == NULL)
		error("Error on allocating job available condition!");
	err = pthread_cond_init(job_available, NULL);
	if (err != 0)
		error("Error on initializing job_available condition");

	head = calloc(1, sizeof(struct job));
	if (head == NULL)
		error("Error on allocating job queue");

	return 0;
}

/* Destroy mutexes, conditional variables, and job queue */
int
destroy()
{
	pthread_mutex_destroy(job_queue_lock);
	free(job_queue_lock);
	job_queue_lock = NULL;

	pthread_cond_destroy(job_available);
	free(job_available);
	job_available = NULL;

	free_jobs(head);

	return 0;
}

/* Enqueue a single job into the job queue */
int
enqueue(job_t *head, int fd, struct sockaddr_storage client_addr)
{
	job_t * jb = head;
	while (jb->next != NULL)
		jb= jb->next;

	job_t * new_job = calloc(1, sizeof(struct job));
	new_job->fd = fd;
	new_job->client_addr = client_addr;
	new_job->previous = jb;
	new_job->next = NULL;
	jb->next = new_job;

	return 0;
}

/* Dequeue a single job from the job queue */
job_t 
dequeue(job_t *head)
{
	job_t return_job;
	job_t * jb = head;
	jb = jb->next;

	if (jb != NULL)
	{

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

	return return_job;
}

/* Free all the remaining jobs in the queue, cleanup function */
void 
free_jobs(job_t * head)
{
	while (head!= NULL)
	{
		job_t * next = head->next;
		free(head);
		head = next;
	}

	// For extra safety, don't want double free problems later
	head = NULL;
}

/* FTP_Thread - the core business logic that processes FTP commands and executes them. */
void *
ftp_thread(void * args)
{
	int err;
	// First the thread tries to retrieve a job from the job queue
	while (1)
	{
		// Lock the job queue
		err = pthread_mutex_lock(job_queue_lock);

		if (err != 0)
		{
			error("Error on locking job queue thread");
		}

		// Wait while there are no jobs available
		while (available_jobs == 0)
		{
			pthread_cond_wait(job_available, job_queue_lock);
		}

		// Dequeue the latest job and process
		job_t client = dequeue(head);

		// Decrement the number of jobs available
		available_jobs--;

		// Unlock the job queue and continue processing
		err = pthread_mutex_unlock(job_queue_lock);
		if (err != 0)
		{
			error("Error unlocking job queue");
		}

		/*
			Where the actual FTP processing takes place. We process client commands and
			respond with appropriate server actions.
		*/
		ssize_t nwrite, nread;
		// Send a welcome message to the client
		nwrite = write(client.fd, "220 CoolFTPServer\r\n",strlen("200 CoolFTPServer\r\n"));
		if (nwrite < 0)
			error("Error on writing initial connection success status to client");

		// Initialize and set important connection variables 
		char buf[4096];
		char * buf_ptr = buf;
		memset(buf_ptr, 0, sizeof(buf));
		char * PORT = NULL;

		/* Initialize a few variables for *passive* data transfers; each one will have its own purpose. */
		long data_port = -1; // Port to listen for connections on passive mode.
		char * local_ip_address = NULL; // Formatted IP Address + Port to send to client during Passive data transfer
		int data_fd = -1; // File descriptor for passive mode listening.
		int client_data_fd = 0; // File descriptor for 'accept'ing client connection in passive mode.

		/* 
			Keep a boolean that indicates whether the client is currently communicating via 
			active or passive FTP.
			Default is active mode.
		*/
		int active = 1;

		/* While the client is sending us a message, process the message.
			TODO: Set a timeout alarm so that we aren't waiting too long on the client
			to send a command 
		*/
		while ( (nread = read(client.fd, buf_ptr, sizeof(buf))) > 0)
		{
			#ifdef DEBUG
			printf("Read command from client:%s\n",buf_ptr);
			fflush(stdout);
			#endif

			// Remove extraneous characters that don't affect the actual command
			buf[strcspn(buf_ptr, "\r\n")] = 0; 
			// Obtain the command name
			char * command = strtok(buf_ptr, " ");
            
            /* Main business logic of the thread: Go through all the commands the FTP Server
            	supports and execute the command which the client requested */

			/* USER authentication command. We will use anonymous FTP, so this command is simply for completeness */
			if (strcmp(command, "USER") == 0)
			{
				// Ask the client for his or her password.
				nwrite = write(client.fd, "331 Password required for USER\r\n", strlen("331 Password required for USER\r\n"));
				if (nwrite < 0)
					error("Error on accepting username");
			}

			/* PASS authentication command. We will use anonymous FTP, so this command is simply for completeness */
			else if(strcmp(command,"PASS") == 0)
			{
				// We implement anonymous FTP, so any password is accepted without actually verifying it.
				nwrite = write(client.fd, "230 You are now logged in.\r\n", strlen("230 You are now logged in.\r\n"));
				if (nwrite < 0)
					error("Error on accepting user password");
			}

			/* SYST command, send details about the FTP's ambient operating system */
			else if(strcmp(command,"SYST") == 0)
			{	
				/* Inform the client of the operating system that the server runs on
					TODO: What system should we return? */
				nwrite = write(client.fd,"215 UNIX\r\n",strlen("215 UNIX\r\n"));
				if (nwrite < 0)
					error("Error on sending operating system type");
			}
  			
  			/* Send details about the features of the FTP Server implementation */
  			else if (strcmp(command,"FEAT") == 0)
			{
				/* Inform the client of the FTP Extensions that our server supports
					TODO: what are extensions? */
				nwrite = write(client.fd,"211 Extensions supported\r\n",strlen("211-Extensions supported\r\n"));
				if (nwrite < 0)
					error("Error on sending extensions");
			}

			/* Send client details about the current working directory of the server executable */
			else if(strcmp(command,"PWD")==0)
			{
				/* Print the current working directory of the server, along with the associated
					status codes */
				char * working_directory = getenv("PWD");
				char * full_message = calloc(strlen("257 \r\n") + strlen(working_directory)+2,1);
				strcat(full_message,"257 ");
				strcat(full_message,"\"");
				strcat(full_message, working_directory);
				strcat(full_message,"\"");
				strcat(full_message, "\r\n");
				nwrite = write(client.fd,full_message,strlen(full_message));
				if (nwrite < 0)
					error("Error on sending extensions");

				#ifdef DEBUG
				printf("Wrote working directory to client: %s",full_message);
				fflush(stdout);
				#endif

				free(full_message);
			}

			/* Server recieves command from client directing its switch to Passive mode FTP with *IPV4 Only*/
			else if(strcmp(command, "PASV") == 0)
			{
				#ifdef DEBUG
				printf("Client issued command PASV!\n");
				fflush(stdout);
				#endif

				/* Choose a random port to listen for data connections */
				data_port = get_random_port();

				/* Generate a socket that is listening on the randomly generated port */
			 	data_fd = initiate_server(data_port);
				if (data_fd < 0)
					error("Error initiating passive FTP socket!");

				/* Get formatted IP Address + Port to send to the client.
					We also specify that we require IPv4 only, since we are not in
					extended passive mode. */
			 	local_ip_address = get_formatted_local_ip_address(data_port,1);
			 	if (local_ip_address == NULL)
			 		error("Error on getting formatted local IP Address");

			 	/* Construct status message for client, informing him/her of the 
			 		local endpoint of the passive FTP */
			 	char * full_client_message = (char *) calloc(strlen("227 Entering passive mode. \r\n") + strlen(local_ip_address),1);
				strcat(full_client_message, "227 Entering passive mode. ");
				strcat(full_client_message, local_ip_address);
				strcat(full_client_message, "\r\n");
				nwrite = write(client.fd,full_client_message,strlen(full_client_message));
				if (nwrite < 0)
					error("Error on sending passive server mode");

				/* We switch active mode off and deallocate resources */
				active = 0;
				free(full_client_message);
				full_client_message = NULL;
			}

			// /* Server recieves command from client directing its switch to Passive mode FTP with *Both IPv4 and IPv6* */
			// else if(strcmp(command, "EPSV") == 0)
			// {
			// 	#ifdef DEBUG
			// 	printf("Client issued command EPSV!\n");
			// 	fflush(stdout);
			// 	#endif

			// 	/* Choose a random port to listen for data connections */
			// 	data_port = get_random_port();

			// 	/* Generate a socket that is listening on the randomly generated port */
			//  	data_fd = initiate_server(data_port);
			// 	if (data_fd < 0)
			// 		error("Error initiating passive FTP socket!");

			// 	/* Get formatted IP Address + Port to send to the client. */
			//  	local_ip_address = get_formatted_local_ip_address(data_port,1);
			//  	if (local_ip_address == NULL)
			//  		error("Error on getting formatted local IP Address");

			//  	/* Construct status message for client, informing him/her of the 
			//  		local endpoint of the passive FTP */
			//  	char * full_client_message = (char *) calloc(strlen("229 Entering passive mode. \r\n") + strlen(local_ip_address),1);
			// 	strcat(full_client_message, "229 Entering passive mode. ");
			// 	strcat(full_client_message, local_ip_address);
			// 	strcat(full_client_message, "\r\n");
			// 	nwrite = write(client.fd,full_client_message,strlen(full_client_message));
			// 	if (nwrite < 0)
			// 		error("Error on sending passive server mode");

			// 	/* We switch active mode off and deallocate resources */
			// 	active = 0;
			// 	free(full_client_message);
			// 	full_client_message = NULL;
			// }

			/* Command to change the working directory of the server executable */
			else if (strcmp(command, "CWD") == 0)
			{
				#ifdef DEBUG
				printf("Client issued command CWD\n");
				fflush(stdout);
				#endif

				/* Obtain the exact directory the client would like to switch to via strtok() */
				command = strtok(NULL, " ");

				/* Switch to the inputted directory
					TODO: Send client failure notification instead of just quitting */
				err = chdir(command);
				if (err < 0)
					error("Error switching directories as requested by client");

				nwrite = write(client.fd, "250\r\n", strlen("250\r\n"));
			    if (nwrite < 0)
				    error("Error on writing CWD success status to client");
			}

			/* Client activates Active mode FTP by issuing the PORT command with the PORT
				that it will listen to for future data transfer connections */
			else if (strcmp(command, "PORT")==0)
			{
				#ifdef DEBUG
				printf("Client issued command PORT!\n");
				fflush(stdout);
				#endif

				/* Get IP Address + port name, formatted according to the norms of the RFC
					FTP standards */
				command = strtok(NULL, " ");

				#ifdef DEBUG
				printf("Client port for active FTP: %s\n",command);
				fflush(stdout);
				#endif
				
				/* Separate IP Address from port name based on commas */
				char * comma_separated_string = strtok(command, ",");
				// Read until we get the two numbers corresponding to the port
				for (int i=0; i < 3; i++)
					comma_separated_string = strtok(NULL, ",");

				/* Obtain the upper bits and lower bits of the port number in binary */
				int upper_bits = atoi(strtok(NULL, ","));
				int lower_bits = atoi(strtok(NULL, ","));

				/* Calculate the decimal version of the port number */
				int calculated_port = (upper_bits << 8) + lower_bits;

				// Free the old port
				free(PORT);
				PORT = NULL;
				// Allocate a new buffer for the new port
				PORT = calloc(6, 1);
				sprintf(PORT,"%d",calculated_port);
				
				/* Send successful active FTP activation confirmation to client */
				nwrite = write(client.fd, "200 Entering active mode\r\n", strlen("200 Entering active mode\r\n"));
			    if (nwrite < 0)
				    error("Error on writing active FTP success status to client");

				/* Switch the active FTP flag on */
				active = 1;
			}

			/* Client command asking to recieve a list of contents of the current working directory
				of the server executable */
			else if (strcmp(command, "LIST") == 0)
			{
				#ifdef DEBUG
				printf("Client issued command LIST!\n");
				fflush(stdout);
				#endif

				/* Get the contents of the current working directory */
				char * directory_list = LIST(getenv("PWD"));

				/* Format and send the status message to the client, along with the contents
					of the current working directory */
				char * full_message = calloc(strlen(directory_list)+strlen("\r\n"),1);
				strcat(full_message, directory_list);
				strcat(full_message,"\r\n");

				/* Handle the case where the client has currently chosen passive mode to be their
					desired form of data transfer */
				if (!active)
				{
					struct sockaddr_storage temp;
					socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
					/* 'Accept' the incoming client connection to our established socket */
					client_data_fd = accept(data_fd, (struct sockaddr *)&temp, &len);
					if (client_data_fd < 0)
						error("Error on accepting passive client connection for data transfer during LIST");	
					else
						nwrite = write(client.fd,"150 Opening ASCII mode data connection\r\n",strlen("150 Opening ASCII mode data connection\r\n"));

					nwrite = write(client_data_fd, full_message, strlen(full_message));
					if (nwrite < 0)
						error("Error when sending directory contents to client in response to LIST command");


					/* Close the socket descriptors that connect to the client.
						Note that this ends the current passive mode connection; a new connection will have to be established
						before performing further transfers */
					close(client_data_fd);
					close(data_fd);
				}

				/* Handle the case where the client has currently chosen active mode to be their
					desired form of data transfer */
				else
				{
					socklen_t len = sizeof(struct sockaddr_storage);

					/* Keep track of the IP address and the port of the client: we will be transferring the bytes
						of the file to this port */
					char hoststr[NI_MAXHOST];
					char portstr[NI_MAXSERV];

					/* Obtain the IP Address and port number of the current client connection */
					err = getnameinfo((struct sockaddr *)&(client.client_addr), len, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);

					/* Handle error situations in processing the client connection */
					if (err != 0) 
					{
						nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
						if (nwrite < 0)
							error("Error on communicating data connection error to client\n");
						continue;	
					}

					/* Successfully obtained information regarding the client's connection */
					else
					{
						/* Use a helper function to connect to the client's IP Address, but in a *different* port than the
							regular command transfers. This different port, used for data transfers, was provided earlier via
							the PORT command */
						data_fd = get_active_client_connection(hoststr, PORT);

						/* If we for some reason fail when trying to connect to the client port for active FTP, we inform the client */
						if (data_fd < 0)
						{
							nwrite = write(client.fd, "425 can't open active data connection\r\n", sizeof("425 can't open active data connection\r\n"));
							continue;
						}
						else
							nwrite = write(client.fd,"150 Opening ASCII mode data connection\r\n",strlen("150 Opening ASCII mode data connection\r\n"));
					}

					nwrite = write(data_fd, full_message, strlen(full_message));
					if (nwrite < 0)
						error("Error when sending directory contents to client in response to LIST command");

					/* Close the associated file descriptors */
					close(data_fd);
				}


				#ifdef DEBUG
				printf("Finish listing directory contents! \n");
				fflush(stdout);
				#endif

				nwrite = write(client.fd,"226 Directory contents listed\r\n",strlen("226 Directory contents listed\r\n"));
				if (nwrite < 0)
					error("Error when writing LIST success status to client");

				/* Deallocate resources */
				free(full_message);
				full_message = NULL;
				free(directory_list);
				directory_list = NULL;
			}

			/* Client has issued a command to get a certain file */
			else if (strcmp(command, "RETR") == 0)
			{
				#ifdef DEBUG
				printf("Client has issued command RETR!\n");
				fflush(stdout);
				#endif

				// Get the specific filename for retrieval 
				char * filename = strtok(NULL, " ");

				// Open the file, returning an error to the client if something went wrong
				int file_fd = open(filename, O_RDONLY);
				if (file_fd < 0)
				{
					nwrite = write(client.fd, "550 Error during file access \r\n", sizeof("550 Error during file access \r\n"));
					if (nwrite < 0)
						error("Error on communicating file access error code to client\n");
				}

				// Inform the client that we are ready to transfer the requested file.
				else
				{
					nwrite = write(client.fd, "150 Opening ASCII mode data connection\r\n", sizeof("150 Opening ASCII mode data connection\r\n"));
					if (nwrite < 0)
						error("Error on communicating ASCII mode file transfer information to client\n");
				}

				/* Handle the case where the client has currently chosen passive mode to be their
					desired form of data transfer */
				if (!active)
				{
					struct sockaddr_storage temp;
					socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
					/* 'Accept' the incoming client connection to our established socket */
					client_data_fd = accept(data_fd, (struct sockaddr *)&temp, &len);
					if (client_data_fd < 0)
						error("Error on accepting passive client connection for data transfer during RETR");	

					/* Call the RETR command, and if we encounter an error during data transfer, we 
						inform the client */
					err = RETR(file_fd, client_data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in file processing\r\n", sizeof("451 Local error in file processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));

					/* Close the file descriptor to the file, and the socket descriptors that connect to the client.
						Note that this ends the current passive mode connection; a new connection will have to be established
						before performing further transfers */
					close(file_fd);
					close(client_data_fd);
					close(data_fd);
				}

				/* Handle the case where the client has currently chosen active mode to be their
					desired form of data transfer */
				else
				{
					socklen_t len = sizeof(struct sockaddr_storage);

					/* Keep track of the IP address and the port of the client: we will be transferring the bytes
						of the file to this port */
					char hoststr[NI_MAXHOST];
					char portstr[NI_MAXSERV];

					/* Obtain the IP Address and port number of the current client connection */
					err = getnameinfo((struct sockaddr *)&(client.client_addr), len, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);

					/* Handle error situations in processing the client connection */
					if (err != 0) 
					{
						nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
						if (nwrite < 0)
							error("Error on communicating data connection error to client\n");
						continue;	
					}

					/* Successfully obtained information regarding the client's connection */
					else
					{
						/* Use a helper function to connect to the client's IP Address, but in a *different* port than the
							regular command transfers. This different port, used for data transfers, was provided earlier via
							the PORT command */
						data_fd = get_active_client_connection(hoststr, PORT);

						/* If we for some reason fail when trying to connect to the client port for active FTP, we inform the client */
						if (data_fd < 0)
						{
							nwrite = write(client.fd, "425 can't open active data connection\r\n", sizeof("425 can't open active data connection\r\n"));
							continue;
						}
					}

					/* After obtaining the active client connection, we call the RETR function to 
						pass the bytes of the file to the client */
					err = RETR(file_fd, data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in file processing\r\n", sizeof("451 Local error in file processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));

					/* Close the associated file descriptors */
					close(file_fd);
					close(data_fd);
				}
				
			}

			/* The client has issued a command requesting to store a file on the server,
				in the current working directory of the program */
			else if (strcmp(command,"STOR") == 0)
			{
				#ifdef DEBUG
				printf("Client has issued command STOR!\n");
				fflush(stdout);
				#endif 

				/* Obtain the filename of the file to be created */
				char * filename = strtok(NULL, " ");

				/* Declare the file descriptor corresponding to the new file */
				int file_fd;

				// TODO: HOW TO GET MODE

				/* Erase the old contents of the file so that may append new client conntent */
				err = truncate(filename, 0);

				/* If the error is of type ENOENT, it means that the truncation failed due to us passing
					a non-existent file as argument. Such a situation is acceptable as we will create a new file
					anyway. If we recieve another error however, we have to inform the client and abort
					the procedure */
				if (err < 0)
				{
					if (errno != ENOENT)
					{	
						/* Inform the client of the error during truncation */
						nwrite = write(client.fd, "452 file unavailable\r\n", sizeof("452 file unavailable\r\n"));
						if (nwrite < 0)
							error("Error in communicating truncation error to the client\n");
						continue;
					}
				}

				/* Create the file descriptor with the O_EXCL flag since we don't want
					another process to open the file at the same time. */
				file_fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0644);

				/* An error occured in opening the file descriptor */
				if (file_fd < 0)
				{
					/* The situation where another process was trying to open the same file descriptor */
					if (errno == EEXIST)
					{
						#ifdef DEBUG
						printf("ERROR: Concurrent creation of the same file descriptor! \n");
						fflush(stdout);
						#endif

						/* Inform the client of the error */
						nwrite = write(client.fd, "452 file unavailable\r\n", sizeof("452 file unavailable\r\n"));
						if (nwrite < 0)
							error("Error in communicating file concurrent access to the client\n");
						continue;
					}
				}
				
				/* Inform the client that we are ready to transfer the bytes to store a file */
				nwrite = write(client.fd, "150 Opening ASCII mode data connection\r\n", sizeof("150 Opening ASCII mode data connection\r\n"));
				if (nwrite < 0)
					error("Error on communicating ASCII file transfer to client\n");

				/* Handle the case where the client has currently chosen passive mode to be their
					desired form of data transfer */
				if (!active)
				{
					struct sockaddr_storage temp;
					socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
					/* 'Accept' the incoming client connection to our established socket */
					client_data_fd = accept(data_fd, (struct sockaddr *)&temp, &len);
					if (client_data_fd < 0)
						error("Error on accepting client channel for data transfer");

					/* Call the STOR command, and if we encounter an error during data transfer, we 
						inform the client */
					err = STOR(file_fd, client_data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in file processing\r\n", sizeof("451 Local error in file processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));

					/* Close the file descriptor to the file, and the socket descriptors that connect to the client.
						Note that this ends the current passive mode connection; a new connection will have to be established
						before performing further transfers */
					close(file_fd);
					close(client_data_fd);
					close(data_fd);
				}

				/* Handle the case where the client has currently chosen active mode to be their
					desired form of data transfer */
				else
				{
					socklen_t len = sizeof(struct sockaddr_storage);

					/* Keep track of the IP address and the port of the client: we will be transferring the bytes
						of the file to this port */
					char hoststr[NI_MAXHOST];
					char portstr[NI_MAXSERV];

					/* Obtain the IP Address and port number of the current client connection */
					err = getnameinfo((struct sockaddr *)&(client.client_addr), len, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);

					/* Handle error situations in processing the client connection */
					if (err != 0) 
					{
						nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
						if (nwrite < 0)
							error("Error on communicating data connection error to client\n");
						continue;	
					}

					/* Successfully obtained information regarding the client's connection */			
					else
					{
						/* Use a helper function to connect to the client's IP Address, but in a *different* port than the
							regular command transfers. This different port, used for data transfers, was provided earlier via
							the PORT command */
						data_fd = get_active_client_connection(hoststr, PORT);

						/* If we for some reason fail when trying to connect to the client port for active FTP, we inform the client */
						if (data_fd < 0)
						{
							nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
							continue;
						}
					}

					/* After obtaining the active client connection, we call the STOR function to 
						write the bytes of the client connection into the file */
					err = STOR(file_fd, data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));

					/* Close the associated file descriptors */
					close(file_fd);
					close(data_fd);
				}

			}

			else if (strcmp(command,"APPE") == 0)
			{
				#ifdef DEBUG
				printf("Client has issued command APPE!\n");
				fflush(stdout);
				#endif

				/* Obtain the filename of the file to be created */
				char * filename = strtok(NULL, " ");

				/* Declare the file descriptor corresponding to the new file */
				int file_fd;

				// TODO: HOW TO GET MODE

				/* Open a new file descriptor to append to the file */
				file_fd = open(filename, O_CREAT | O_APPEND, 0644);

				/* Error in opening file descriptor, so we inform the client */
				if (file_fd < 0)
				{
					nwrite = write(client.fd, "452 file unavailable\r\n", sizeof("452 file unavailable\r\n"));
					if (nwrite < 0)
						error("Error on communicating file unavailable to client\n");
				}

				/* Else we inform client of successful opening of file for appending */
				else
				{
					nwrite = write(client.fd, "150 Opening ASCII mode data connection\r\n", sizeof("150 Opening ASCII mode data connection\r\n"));
					if (nwrite < 0)
						error("Error on communicating APPE file opening success to client\n");
				}
                
                /* Handle the case where the client has currently chosen passive mode to be their
					desired form of data transfer */
				if (!active)
				{
					struct sockaddr_storage temp;
					socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
					/* 'Accept' the incoming client connection to our established socket */
					client_data_fd = accept(data_fd, (struct sockaddr *)&temp, &len);
					if (client_data_fd < 0)
						error("Error on accepting client channel for data transfer");

					/* Using append mode, we again call the STOR command, and if we encounter an error during data transfer, we 
						inform the client */
					err = STOR(file_fd, client_data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in file processing\r\n", sizeof("451 Local error in file processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));

					/* Close the file descriptor to the file, and the socket descriptors that connect to the client.
						Note that this ends the current passive mode connection; a new connection will have to be established
						before performing further transfers */
					close(file_fd);
					close(client_data_fd);
					close(data_fd);
				}

				/* Handle the case where the client has currently chosen active mode to be their
					desired form of data transfer */
				else
				{
					socklen_t len = sizeof(struct sockaddr_storage);

					/* Keep track of the IP address and the port of the client: we will be transferring the bytes
						of the file to this port */
					char hoststr[NI_MAXHOST];
					char portstr[NI_MAXSERV];

					/* Obtain the IP Address and port number of the current client connection */
					err = getnameinfo((struct sockaddr *)&(client.client_addr), len, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);

					/* Handle error situations in processing the client connection */
					if (err != 0) 
					{
						nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
						if (nwrite < 0)
							error("Error on communicating data connection error to client\n");
						continue;	
					}

					/* Successfully obtained information regarding the client's connection */			
					else
					{
						/* Use a helper function to connect to the client's IP Address, but in a *different* port than the
							regular command transfers. This different port, used for data transfers, was provided earlier via
							the PORT command */
						data_fd = get_active_client_connection(hoststr, PORT);

						/* If we for some reason fail when trying to connect to the client port for active FTP, we inform the client */
						if (data_fd < 0)
						{
							nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
							continue;
						}
					}

					/* After obtaining the active client connection, we call the STOR function with our
						file descriptor in 'append' mode to write the bytes of the client connection into the file */
					err = STOR(file_fd, data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));

					/* Close the associated file descriptors */
					close(file_fd);
					close(data_fd);
				}

			}
			
			/* Client has issued the command to remove a specified directory */
			else if (strcmp(command,"RMD") == 0)
			{
				#ifdef DEBUG
				printf("Client has issued command RMD!\n");
				fflush(stdout);
				#endif

				/* Obtain the directory name for removal */
				const char * dirname = strtok(NULL, " ");

				/* Then simply remove the specified directory */
				err = rmdir(dirname);
				if (err < 0)
					nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
				else
					nwrite = write(client.fd, "226 Removal complete\r\n", sizeof("226 Removal complete\r\n"));
			}

			/* Client has issued the command to make a new directory */
			else if (strcmp(command, "MKD") == 0)
			{
				#ifdef DEBUG
				printf("Client has issued command MKD!\n");
				fflush(stdout);
				#endif

				/* Obtain the directory name for removal */
				const char * dirname = strtok(NULL, " ");

				/* TODO: How to get the mode of the new directory? */

				/* Then simply make the specified new directory */
				err = mkdir(dirname, 0644);
				if (err < 0)
					nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
				else
					nwrite = write(client.fd, "226 Directory creation complete\r\n", sizeof("226 Directory creation complete\r\n"));
			}

			/* The command issued by the client could not be matched with any of the 
				commands supported by the server */
			else
			{
				#ifdef DEBUG
				printf("Unsupported command issued by client\n");
				fflush(stdout);
				#endif

				/* Inform the client that the command he/she sent is not suppported by the server */
				nwrite = write(client.fd, "500 Command not supported\r\n", strlen("500 Command not supported\r\n"));
				if (nwrite < 0)
					error("Error in informing client of unsupported command");
			}

			/* Reset the buffer to receive the next command, removing any residual characters */
         	memset(buf_ptr,0,sizeof(buf));
		}

		/* Client has either closed his/her side of the connection or we encountered a 
			connection failure, therefore we close the connection */
		close(client.fd);

		#ifdef DEBUG
		printf("Client connection stopped or failed!\n");
		fflush(stdout);
		#endif

		/* Deallocate certain buffers */
		free(PORT);
		PORT = NULL;
		free(local_ip_address);
		local_ip_address = NULL;

		/* Thread will continue to wait for further client connections to process */
		continue;
	}
}

/* Returns a list of the contents of the inputted working directory;
	used in conjunction with the LIST command
	*/
char * 
LIST(char * dir_name)
{
	/* Open the inputted working directory */
	DIR * d;
	d = opendir(dir_name);

	/* Initialize the variable holding the list of directory contents;
		max_size is an upper limit: 4096 * 3 = 12288.
		We will later 'realloc' it if the need arises */
	long max_size = 12288;
	char * full_list = calloc(max_size, 1);
	long cur_size = 0;

	if (d == 0)
	{
		#ifdef DEBUG
		printf("Error opening directory: %s\n", dir_name);
		fflush(stdout);
		#endif

		/* TODO: RETURN ERROR MESSAGE TO CLIENT INSTEAD OF EXITING */
		error("Error opening directory for client LIST command");
	}

	struct dirent * cur_dir_entry;
	cur_dir_entry = readdir(d);

	while (cur_dir_entry != NULL)
	{

		// Ignore hidden directories or control directories
		if (cur_dir_entry->d_name[0] == '.')
		{
			cur_dir_entry = readdir(d);
			continue;
		}

		/* If the directory contents length goes above the allotted size, 
			we reallocate the buffer */
		if (cur_size + strlen(cur_dir_entry->d_name) > max_size-2)
		{
			max_size+=4096;
			full_list = realloc(full_list, max_size);
		}

		strcat(full_list, " ");
		strcat(full_list, cur_dir_entry->d_name);
		cur_dir_entry = readdir(d);
	}
	closedir(d);

	// At the end, append a new line
	strcat(full_list, "\n");

	/* Return the directory contents */
	return full_list;
}


/* Initialize a 'listen'ing server for a specific port number on the local machine;
	used in conjunction with the Passive FTP module
	*/
int
initiate_server(long port)
{
	int err, fd =0;
	struct addrinfo hints;struct addrinfo * res;struct addrinfo * res_original;
	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	/* Create a string version of the port number */
	char port_buffer[strlen("65535")];
	char * port_pointer = port_buffer;
	sprintf(port_pointer, "%ld", port);

	#ifdef DEBUG
	printf("Initiating server on port: %s\n",port_pointer);
	fflush(stdout);
	#endif


	/* Use getaddrinfo to fill out the IP Address and port number information */
	err = getaddrinfo(NULL, port_pointer, &hints, &res_original);
	if (err)
	{
		error(gai_strerror(err));
	}

	for (res = res_original; res!=NULL; (res=res->ai_next))
	{
		/* Double checking so we don't get invalid values */
		if ( (res->ai_family != AF_INET) && (res->ai_family != AF_INET6) )
			continue;

		/* IPV4 address generated */
		if (res->ai_family == AF_INET)
		{
			/* Generate a socket for the listening server */ 
			fd = socket(AF_INET, SOCK_STREAM, 0);
			if (fd == -1)
				error("Error on socket");

			/* Bind the port to the resulting socket */
			err = bind(fd, res->ai_addr, res->ai_addrlen);
			if (err == -1)
				error("Error on binding");

			/* Start 'listen'ing so that clients can start connecting to the endpoint
				'accept'ing will be done in the next phase */
			err = listen(fd, MAX_NUM_CONNECTED_CLIENTS);
			if (err == -1)
				error("Error on listen");
			break;
		}

		/* IPV6 address generated, same process as with IPV4, but customized for IPV6 */
		if (res->ai_family == AF_INET6)
		{
			fd = socket(AF_INET6, SOCK_STREAM, 0);
			if (fd == -1)
				error("Error on socket");

			err = bind(fd, res->ai_addr, res->ai_addrlen);
			if (err == -1)
				error("Error on binding");

			err = listen(fd, MAX_NUM_CONNECTED_CLIENTS);
			if (err == -1)
				error("Error on listen");
			break;
		}
	}

	/* Free allocated resources, and return the socket file descriptor */
	freeaddrinfo(res_original);
	res_original = NULL;
	return fd;
}

/* Helper function to store a file; it receives bytes from the data file descriptor argument,
	and writes the bytes into the file descriptor */
int
STOR(int file_fd, int data_fd)
{
	/* Instantiate buffer for holding partial contents of read input */
	ssize_t nread,nwrite;
	char buf[4096];
	char * buf_ptr = buf;
	memset(buf_ptr, 0, sizeof buf);

	/* Read the inputted bytes and write them to the file, while
		some input is still given */
	while ( (nread = read(data_fd, buf_ptr, sizeof buf)) > 0)
	{
		nwrite = write(file_fd, buf_ptr, nread);
		if (nwrite < 0)
			return -1;
		memset(buf_ptr, 0, sizeof buf);
	}

	if (nread < 0)
	{
		return -1;
	}
	else
		return 0;
}

/* Command to obtain bytes from the file descriptor, and write it into the data descriptor. 
	Used in conjunction with a client request to get a file */
int 
RETR(int file_fd, int data_fd)
{
	/* Instantiate buffer for holding partial contents of file */
	ssize_t nread,nwrite;
	char buf[4096];
	char * buf_ptr = buf;
	memset(buf_ptr, 0, sizeof buf);

	/* Read contents of the file and write them to the data file descriptor, while
		some content still exists */
	while ( (nread = read(file_fd, buf_ptr, sizeof buf)) > 0)
	{
		nwrite = write(data_fd, buf_ptr, nread);
		if (nwrite < 0)
			return -1;
		memset(buf_ptr, 0, sizeof buf);
	}

	if (nread < 0)
		return -1;
	else
		return 0;	
}

/* Generates a random port number that is usable by user applications.
	Namely, will generate a random number between 1000 - 65535
	*/
long
get_random_port()
{
	int a = 0;
    while (a < 1000)
    {
        srand( time(NULL) );
        a = rand();
        a = a % 65535;
    }
    return a;
}

/* Return an IP Address + Port endpoint combination in a format that is 
	normative for FTP servers; an example would be (187,165,1,12,210,19)
	*/
char *
get_formatted_local_ip_address(unsigned int port, int IPV4ONLY)
{
	/* Use getifaddrs to get network interfaces of the local machine and combine it
		with the inputted port */
	struct ifaddrs * ifAddrStruct=NULL;
    struct ifaddrs * ifa=NULL;
    void * tmpAddrPtr=NULL;
    char * complete_address_buffer;

    getifaddrs(&ifAddrStruct);

    /* Be ready to accept both IPV4 and IPV6 interfaces; IPV4 is default */
    int ipv4 = 1;
    char address_buffer_ipv4[INET_ADDRSTRLEN];
    char address_buffer_ipv6[INET6_ADDRSTRLEN];

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) 
    {
        if (!ifa->ifa_addr)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) 
        {
        	// We don't want local interfaces, but rather the interface visible to the internet
            if (strcmp(ifa->ifa_name,"lo0") ==0)
                continue;

            // We obtain a valid IPV4 address
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, tmpAddrPtr, address_buffer_ipv4, INET_ADDRSTRLEN);
        } 

        /* Analogously, we perform the same procedure with IPV6 */
        else if (ifa->ifa_addr->sa_family == AF_INET6) 
        { 
        	/* If the client specified non-extended passive mode, we must return only
        		IPv4 Addresses */
        	if (IPV4ONLY)
        		continue;

            if (strcmp(ifa->ifa_name,"lo0") ==0)
                continue;

            tmpAddrPtr=&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            inet_ntop(AF_INET6, tmpAddrPtr, address_buffer_ipv6, INET6_ADDRSTRLEN);

            // Disable IPV4
            ipv4 = 0;
        } 
    }

    /* Some error occured, and the search for local interfaces failed */
    if (ifAddrStruct == NULL)
    	return NULL;

    /* Now we do some string manipulation so that we get the IP Address and the port
    	into the format we need for FTP. This procedure looks ostensibly complex, but is 
    	actually just simple moving around of strings and characters */
    char * buf_ptr = NULL;
    if (ipv4)
    	buf_ptr = address_buffer_ipv4;
    else
    	buf_ptr = address_buffer_ipv6;

    for (int i=0; i < strlen(buf_ptr); i++)
    {
    	if (buf_ptr[i] == '.')
    		buf_ptr[i] = ',';
    }

    if (ipv4)
    	complete_address_buffer = calloc(INET_ADDRSTRLEN+15,1);
   	else
   		complete_address_buffer = calloc(INET6_ADDRSTRLEN+15,1);

    strcat(complete_address_buffer, "(");
    strcat(complete_address_buffer,buf_ptr);
    strcat(complete_address_buffer,",");

    /* Manipulate the port number to split it into upper bits and lower bits */
    unsigned int lower_bits = port & 0xFF;
    char * lower_bits_string = calloc(4,1);
    sprintf(lower_bits_string,"%d",lower_bits);

    unsigned int higher_bits = port & 0xFF00;
    higher_bits = higher_bits >> 8;
    char * higher_bits_string = calloc(4,1);
    sprintf(higher_bits_string,"%d",higher_bits);

  	strcat(complete_address_buffer,higher_bits_string);
    strcat(complete_address_buffer,",");
    strcat(complete_address_buffer,lower_bits_string);
    strcat(complete_address_buffer,")");
    strcat(complete_address_buffer,"\0");

    /* Deallocate resources, and return the formatted IP Address and Port */
    freeifaddrs(ifAddrStruct);
    return complete_address_buffer;
}

/* Returns a file descriptor that holds a socket connection to the specified
	IP Address and port. Used in conjunction with active mode transfers where
	we would like to connect to the same client IP Address, but in a different port. */
int
get_active_client_connection(const char * ip_address, const char * port)
{

	#ifdef DEBUG
	printf("Establishing Active client connection, IP Address: %s\n", ip_address);
	printf("Establishing Active client connection, Port: %s\n", port);
	fflush(stdout);
	#endif

	/* Initialize various structures and parameters used for the getaddrinfo/4 function */
	int err, fd=-1;
	struct addrinfo hints;struct addrinfo * res;struct addrinfo * res_original;
	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	/* Get the socket address structure corresponding to the given IP Address and port */
	err = getaddrinfo(ip_address, port, &hints, &res_original);
	if (err)
	{
		return -1;
	}

	/* Parse the returned structures until we reach one that is either IPV4 or IPV6 format */
	for (res = res_original; res!=NULL; (res=res->ai_next))
	{
		if ( (res->ai_family != AF_INET) && (res->ai_family != AF_INET6) )
			continue;
		else
			break;
	}

	/* Establish an IPV4 connection to the client socket */
	if (res->ai_family == AF_INET)
	{
		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd == -1)
			error("Error in IPV4 socket establishment in active client connection");
		err = connect(fd, res->ai_addr, res->ai_addrlen);
		if (err < 0)
			error("Error in 'connect'ing to the active client port with IPV4");
		
	}

	/* Establish an IPV6 connection to the client socket */
	if (res->ai_family == AF_INET6)
	{
		fd = socket(AF_INET6, SOCK_STREAM, 0);
		if (fd == -1)
			error("Error in IPV6 socket establishment in active client connection");
		err = connect(fd, res->ai_addr, res->ai_addrlen);
		if (err < 0)
			error("Error in 'connect'ing to the active client port with IPV6");
	}

	/* Return the obtained socket file descriptor corresponding to our active client connection */
	return fd;
}
