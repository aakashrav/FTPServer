#include "ftp_functions.h"

void
error(const char * message)
{
	perror(message);
	printf("errno: %d\n", errno);
	exit(1);
}

// Initlialize mutexes, conditional variables, and job queue
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

// Destroy mutexes, conditional variables, and job queue
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

// Enqueue job into job queue
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

// Dequeue job from job queue
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

// Free all the remaining jobs in the queue, cleanup function
void 
free_jobs(job_t * head)
{
	while (head!= NULL)
	{
		job_t * next = head->next;
		free(head);
		head = next;
	}

	// Extra safety, don't want double free problems later
	head = NULL;
}

// Thread function that processes jobs
void *
ftp_thread(void * args)
{
	int err;
	while (1)
	{
		// Lock the job queue
		err = pthread_mutex_lock(job_queue_lock);

		if (err != 0)
		{
			destroy();
			error("Error on locking job queue thread");
		}

		while (available_jobs == 0)
		{
			pthread_cond_wait(job_available, job_queue_lock);
		}

		// Dequeue the latest job and process
		job_t client = dequeue(head);

		// Decrement the number of jobs available
		available_jobs--;

		printf("Thread going!\n");
		fflush(stdout);

		// Unlock the job queue and continue processing
		err = pthread_mutex_unlock(job_queue_lock);
		if (err != 0)
		{
			destroy();
			error("Error unlocking job queue");
		}

		/*
		Do FTP Client processing here.
		*/
		ssize_t nwrite, nread;
		nwrite = write(client.fd, "220 CoolFTPServer\r\n",strlen("200 CoolFTPServer\r\n"));
		if (nwrite < 0)
			error("Error on writing initial connection success status to client");

		printf("Wrote to client!");
		fflush(stdout);

		char buf[4096];
		char * buf_ptr = buf;
		memset(buf_ptr, 0, sizeof(buf));
		char * PORT = NULL;
		// Socket file descriptor for data transfers
		int data_fd = -1;
		int client_data_fd = 0;
		long data_port = -1;
		char * local_ip_address = NULL;
		// Default active
		int active = 1;

		while ( (nread = read(client.fd, buf_ptr, sizeof(buf))) > 0)
		{
			printf("READ FROM CLIENT:%s\n",buf_ptr);
			fflush(stdout);
			buf[strcspn(buf_ptr, "\r\n")] = 0; 
			char * str = strtok(buf_ptr, " ");

			if (strcmp(str, "USER") == 0)
			{
				printf("In command USER!\n");
				fflush(stdout);
				nwrite = write(client.fd, "331 Password required for USER\r\n", strlen("331 Password required for USER\r\n"));
				if (nwrite < 0)
					error("Error on accepting username");
			}

			else if(strcmp(str,"PASS") == 0)
			{
				printf("In command PASS!\n");
				fflush(stdout);
				nwrite = write(client.fd, "230 You are now logged in.\r\n", strlen("230- You are now logged in.\r\n"));
				if (nwrite < 0)
					error("Error on accepting user password");
			}

			else if(strcmp(str,"SYST") == 0)
			{
				printf("In command SYST!\n");
				fflush(stdout);
				nwrite = write(client.fd,"215 UNIX\r\n",strlen("215 UNIX\r\n"));
				if (nwrite < 0)
					error("Error on sending operating system type");
			}
  			
  			else if (strcmp(str,"FEAT") == 0)
			{
				printf("In command FEAT\n");
				fflush(stdout);
				/* TODO: what are extensions? */
				nwrite = write(client.fd,"211 Extensions supported\r\n",strlen("211-Extensions supported\r\n"));
				if (nwrite < 0)
					error("Error on sending extensions");
			}

			else if(strcmp(str,"PWD")==0)
			{
				printf("In command PWD\n");
				fflush(stdout);
				char * working_directory = getenv("PWD");
				char * full_message = calloc(strlen("257\r\n") + strlen(working_directory),1);
				strcat(full_message, "257 ");
				strcat(full_message, working_directory);
				strcat(full_message, "\r\n");
				nwrite = write(client.fd,full_message,strlen(full_message));
				if (nwrite < 0)
					error("Error on sending extensions");

				printf("Wrote %s",full_message);
				fflush(stdout);

				free(full_message);
			}
			else if(strcmp(str, "PASV") == 0)
			{
				printf("In command PASV\n");
				fflush(stdout);

				// Choose random port to listen for data connections
				if (data_port < 0)
				{
					printf("In here!\n");
					fflush(stdout);
					data_port = get_random_port();
					// Close any existing connections
					// close(data_fd);
					// data_fd = 0;
					// close(client_data_fd);
					// client_data_fd = 0;

					data_fd = initiate_server(data_port);
					if (data_fd < 0)
						error("Error initiating passive connection!");
			 		local_ip_address = get_formatted_local_ip_address(data_port);
			 		if (local_ip_address == NULL)
			 			error("Error on getting formatted local IP Address");
			 	}

			 	char * full_client_message = (char *) calloc(strlen("227 Entering passive mode.\r\n") + strlen(local_ip_address),1);
				strcat(full_client_message, "227 Entering passive mode. ");
				strcat(full_client_message, local_ip_address);
				strcat(full_client_message, "\r\n");
				nwrite = write(client.fd,full_client_message,strlen(full_client_message));
				if (nwrite < 0)
					error("Error on sending passive server mode");

				active = 0;
				free(full_client_message);
				full_client_message = NULL;

				// struct sockaddr_storage temp;
				// socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
				// client_data_fd = accept(data_fd, (struct sockaddr *)&temp, 
				// 		&len);
				// if (client_data_fd < 0)
				// 	error("Error on accepting client channel for data transfer");	

				// free(local_ip_address);
				// local_ip_address = NULL;
				// free(full_client_message);
				// full_client_message = NULL;
			}

			else if (strcmp(str, "CWD") == 0)
			{
				printf("In command CWD!\n");
				fflush(stdout);
				str = strtok(NULL, " ");
				err = chdir(str);
				if (err < 0)
					error("Error on CWD command");

				nwrite = write(client.fd, "250\r\n", strlen("250\r\n"));
			    if (nwrite < 0)
				    error("Error on writing CWD success status to client");
			}

			else if (strcmp(str, "PORT")==0)
			{
				printf("In command PORT!\n");
				fflush(stdout);

				// Get IP Address + port name
				str = strtok(NULL, " ");
				// Separate IP Address + port name based on commas
				char * comma_separated_string = strtok(str, ",");
				// Read until we get the two numbers corresponding to the ports
				for (int i=0; i < 4; i++)
					comma_separated_string = strtok(NULL, ",");

				int upper_bits = atoi(strtok(NULL, ","));
				int lower_bits = atoi(strtok(NULL, ","));
				printf("%d,%d",upper_bits,lower_bits);
				fflush(stdout);

				int calculated_port = (upper_bits * (2^8)) + lower_bits;

				// Free the old port
				free(PORT);
				PORT = NULL;
				// Allocate a new buffer for the port
				PORT = calloc(6, 1);
				sprintf(PORT,"%d",calculated_port);
				
				nwrite = write(client.fd, "200 Entering active mode\r\n", strlen("200 Activated Active transfer\r\n"));
			    if (nwrite < 0)
				    error("Error on writing PORT success status to client");
				active = 1;
			}

			else if (strcmp(str, "LIST") == 0)
			{
				printf("In command LIST!\n");
				fflush(stdout);
				char * directory_list = LIST(getenv("PWD"));

				char * full_message = calloc(strlen(directory_list)+strlen("226 \r\n"),1);
				strcat(full_message,"226 ");
				strcat(full_message, directory_list);
				strcat(full_message,"\r\n");

				nwrite = write(client.fd, full_message, strlen(full_message));
				if (nwrite < 0)
					error("Erorr on communicating successful LIST to client");

				free(full_message);
				full_message = NULL;
				free(directory_list);
				directory_list = NULL;
			}

			else if (strcmp(str, "RETR") == 0)
			{
				printf("In command RETR!\n");
				fflush(stdout);
				// Get filename for retrieving
				char * filename = strtok(NULL, " ");

				int file_fd = open(filename, O_RDONLY);
				if (file_fd < 0)
				{
					nwrite = write(client.fd, "550 File does not exist \r\n", sizeof("550 File does not exist \r\n"));
					if (nwrite < 0)
						error("Error on communicating file does not exist error code to client\n");
				}

				else
				{
					nwrite = write(client.fd, "150 Opening ASCII mode data connection\r\n", sizeof("150 Opening ASCII mode data connection\r\n"));
					if (nwrite < 0)
						error("Error on communicating ASCII file transfer to client\n");
				}

				// Passive mode
				if (!active)
				{
					struct sockaddr_storage temp;
					socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
					client_data_fd = accept(data_fd, (struct sockaddr *)&temp, &len);
					if (client_data_fd < 0)
						error("Error on accepting client channel for data transfer");	

					err = RETR(file_fd, client_data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));
					close(file_fd);
					close(client_data_fd);
					close(data_fd);
				}

				// Active mode
				else
				{
					// If not already connected to the data port
					if (data_fd < 0)
					{
						socklen_t len = sizeof(struct sockaddr_storage);

						char hoststr[NI_MAXHOST];
						char portstr[NI_MAXSERV];

						err = getnameinfo((struct sockaddr *)&(client.client_addr), len, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
						if (err != 0) 
						{
							nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
							if (nwrite < 0)
								error("Error on communicating data connection error to client\n");
							continue;	
						}
						else
						{
							// Establish active data connection
							data_fd = get_active_client_connection(hoststr, portstr);
							if (data_fd < 0)
							{
								nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
								continue;
							}
						}
					}

					// Store file
					err = RETR(file_fd, data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));
					close(file_fd);
					close(data_fd);
				}
			}

			else if (strcmp(str,"STOR") == 0)
			{
				printf("In command STOR!\n");
				fflush(stdout);
				// Get filename for creating
				char * filename = strtok(NULL, " ");

				int file_exists = 0;
				int file_fd;

				// TODO, HOW TO GET MODE
				err = access(filename, F_OK);
				if (err < 0)
					file_exists = 0;

				if (!file_exists)
				{
					// O_EXCL since we don't want concurrent access to the file
					file_fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0644);
					if (file_fd < 0)
					{
						if (errno == EEXIST)
						{
							printf("In situation 1!");
							fflush(stdout);

							nwrite = write(client.fd, "452 file unavailable\r\n", sizeof("452 file unavailable\r\n"));
							if (nwrite < 0)
								error("Error on communicating file unavailable to client\n");
							continue;
						}
					}

				}
				else
				{
					// File exists, so we erase old contents of file so we can append
					err = truncate(filename, 0);
					//O_EXCL since we don't want concurrent access to the file
					file_fd = open(filename, O_EXCL | O_WRONLY, 0644);
					if (file_fd < 0)
					{
						printf("In situation 2!");
						fflush(stdout);

						nwrite = write(client.fd, "452 file unavailable\r\n", sizeof("452 file unavailable\r\n"));
						if (nwrite < 0)
							error("Error on communicating file unavailable to client\n");
						continue;
					}
				}

				nwrite = write(client.fd, "150 Opening ASCII mode data connection\r\n", sizeof("150 Opening ASCII mode data connection\r\n"));
				if (nwrite < 0)
					error("Error on communicating ASCII file transfer to client\n");

				// Passive mode
				if (!active)
				{
					struct sockaddr_storage temp;
					socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
					client_data_fd = accept(data_fd, (struct sockaddr *)&temp, &len);
					if (client_data_fd < 0)
						error("Error on accepting client channel for data transfer");

					err = STOR(file_fd, client_data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));
					close(file_fd);
					close(client_data_fd);
					close(data_fd);
				}

				// Active mode
				else
				{
					// If not already connected to the data port
					if (data_fd < 0)
					{
						socklen_t len = sizeof(struct sockaddr_storage);

						char hoststr[NI_MAXHOST];
						char portstr[NI_MAXSERV];

						err = getnameinfo((struct sockaddr *)&(client.client_addr), len, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
						if (err != 0) 
						{
							nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
							if (nwrite < 0)
								error("Error on communicating data connection error to client\n");
							continue;	
						}
						else
						{
							// Establish active data connection
							data_fd = get_active_client_connection(hoststr, portstr);
							if (data_fd < 0)
							{
								nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
								continue;
							}
						}
					}

					// Store file
					err = STOR(file_fd, data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));
					close(file_fd);
					close(data_fd);
				}

			}

			else if (strcmp(str,"APPE") == 0)
			{
				printf("In command APPE!\n");
				fflush(stdout);
				// Get filename for creating
				char * filename = strtok(NULL, " ");

				int file_fd;

				// TODO, HOW TO GET MODE

				file_fd = open(filename, O_CREAT | O_APPEND, 0644);
				if (file_fd < 0)
				{
					nwrite = write(client.fd, "452 file unavailable\r\n", sizeof("452 file unavailable\r\n"));
					if (nwrite < 0)
						error("Error on communicating file unavailable to client\n");
				}
				else
				{
					nwrite = write(client.fd, "150 Opening ASCII mode data connection\r\n", sizeof("150 Opening ASCII mode data connection\r\n"));
					if (nwrite < 0)
						error("Error on communicating ASCII file transfer to client\n");
				}
                
                // Passive mode data transfer
				if (!active)
				{
					struct sockaddr_storage temp;
					socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
					client_data_fd = accept(data_fd, (struct sockaddr *)&temp, &len);
					if (client_data_fd < 0)
						error("Error on accepting client channel for data transfer");

					// Use same function as STOR command, only here we append
					err = STOR(file_fd, client_data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));
					close(file_fd);
					close(client_data_fd);
					close(data_fd);
				}

				// Active mode
				else
				{
					// If not already connected to the data port
					if (data_fd < 0)
					{
						socklen_t len = sizeof(struct sockaddr_storage);

						char hoststr[NI_MAXHOST];
						char portstr[NI_MAXSERV];

						err = getnameinfo((struct sockaddr *)&(client.client_addr), len, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
						if (err != 0) 
						{
							nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
							if (nwrite < 0)
								error("Error on communicating data connection error to client\n");
							continue;	
						}
						else
						{
							// Establish active data connection
							data_fd = get_active_client_connection(hoststr, portstr);
							if (data_fd < 0)
							{
								nwrite = write(client.fd, "425 can't open data connection\r\n", sizeof("425 can't open data connection\r\n"));
								continue;
							}
						}
					}

					// Store file
					err = STOR(file_fd, data_fd);
					if (err < 0)
						nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
					else
						nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));
					close(file_fd);
					close(data_fd);
				}

			}
			else if (strcmp(str,"RMD") == 0)
			{
				printf("In command RMD!\n");
				fflush(stdout);
				// Get directory name for removing
				const char * dirname = strtok(NULL, " ");

				err = rmdir(dirname);
				if (err < 0)
					nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
				else
					nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));
			}

			else if (strcmp(str, "MKD") == 0)
			{
				printf("In command MKD : %s!\n",str);
				fflush(stdout);
				// Get name of directory to be created
				const char * dirname = strtok(NULL, " ");

				// TODO: How to get mode of new directory

				err = mkdir(dirname, 0644);
				if (err < 0)
					nwrite = write(client.fd, "451 Local error in processing\r\n", sizeof("451 Local error in processing\r\n"));
				else
					nwrite = write(client.fd, "226 Transfer complete\r\n", sizeof("226 Transfer complete\r\n"));
			}

			else
			{
				printf("Unsupported command issued by client\n");
				fflush(stdout);
				nwrite = write(client.fd, "500 Command not supported\r\n", strlen("500 Command not supported\r\n"));
				if (nwrite < 0)
					error("Error on accepting username");
			}

         	memset(buf_ptr,0,sizeof(buf));
		}

		free(PORT);
		PORT = NULL;
		free(local_ip_address);
		local_ip_address = NULL;
		close(client.fd);
		printf("Client connection stopped or failed!");
		fflush(stdout);
		continue;
	}
}

char * LIST(char * dir_name)
{
	DIR * d;

	d = opendir(dir_name);

	long max_size = 4096;
	char * full_list = calloc(max_size, 1);
	long cur_size = 0;

	if (d == 0)
	{
		printf("%s: \n", dir_name);
		error("Error opening directory");
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
	// At the end, append a new line so the client knows that the listing is over
	strcat(full_list, "\n");
	return full_list;
}

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

	char port_buffer[strlen("65535")];
	char * port_pointer = port_buffer;
	sprintf(port_pointer, "%ld", port);


	err = getaddrinfo(NULL, port_pointer, &hints, &res_original);
	if (err)
	{
		error(gai_strerror(err));
	}

	for (res = res_original; res!=NULL; (res=res->ai_next))
	{
		if ( (res->ai_family != AF_INET) && (res->ai_family != AF_INET6) )
			continue;

		if (res->ai_family == AF_INET)
		{
			fd = socket(AF_INET, SOCK_STREAM, 0);
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

	freeaddrinfo(res_original);
	res_original = NULL;
	return fd;
}

int
STOR(int file_fd, int data_fd)
{
	ssize_t nread,nwrite;
	char buf[4096];
	char * buf_ptr = buf;
	memset(buf_ptr, 0, sizeof buf);
	while ( (nread = read(data_fd, buf_ptr, sizeof buf)) > 0)
	{
		nwrite = write(file_fd, buf_ptr, nread);
		if (nwrite < 0)
			return -1;
		memset(buf_ptr, 0, sizeof buf);
	}

	if (nread < 0)
	{
		error("Error in store!");
		return -1;
	}
	else
		return 0;
}

int 
RETR(int file_fd, int data_fd)
{
	ssize_t nread,nwrite;
	char buf[4096];
	char * buf_ptr = buf;
	memset(buf_ptr, 0, sizeof buf);
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

long
get_random_port()
{
	// Generate a random number between 1000 - 65535
	int a = 0;
    while (a < 1000)
    {
        srand( time(NULL) );
        a = rand();
        a = a % 65535;
    }
    return a;
}

char *
get_formatted_local_ip_address(unsigned int port)
{
	struct ifaddrs * ifAddrStruct=NULL;
    struct ifaddrs * ifa=NULL;
    void * tmpAddrPtr=NULL;
    char * complete_address_buffer;

    getifaddrs(&ifAddrStruct);
    char address_buffer[INET_ADDRSTRLEN];

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) 
    {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) 
        {
        	// We don't want local interfaces, but rather the interface visible to the internet
            if (strcmp(ifa->ifa_name,"lo0") ==0)
                continue;
            // is a valid IP4 Address
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, tmpAddrPtr, address_buffer, INET_ADDRSTRLEN);
        } 

        else if (ifa->ifa_addr->sa_family == AF_INET6) 
        { // check it is IP6
        	// // We don't want local interfaces, but rather the interface visible to the internet
         //    if (strcmp(ifa->ifa_name,"lo0") ==0)
         //        continue;
         //    // is a valid IP6 Address
         //    tmpAddrPtr=&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
         //    address_buffer = calloc(INET6_ADDRSTRLEN,1);
         //    inet_ntop(AF_INET6, tmpAddrPtr, address_buffer, INET6_ADDRSTRLEN);
        	continue;
        } 
    }

    char * buf_ptr = address_buffer;
    for (int i=0; i < strlen(buf_ptr); i++)
    {
    	if (buf_ptr[i] == '.')
    		buf_ptr[i] = ',';
    }
    complete_address_buffer = calloc(INET_ADDRSTRLEN+15,1);
    strcat(complete_address_buffer, "(");
    strcat(complete_address_buffer,buf_ptr);
    strcat(complete_address_buffer,",");

    // Each digit in hexadec is 4 bits.
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

    if (ifAddrStruct == NULL)
    	return NULL;
    else
    	freeifaddrs(ifAddrStruct);
    
    return complete_address_buffer;
}

int
get_active_client_connection(const char * ip_address, const char * port)
{
	int err, fd=-1;
	struct addrinfo hints;struct addrinfo * res;struct addrinfo * res_original;
	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(ip_address, port, &hints, &res_original);
	if (err)
	{
		return -1;
	}

	for (res = res_original; res!=NULL; (res=res->ai_next))
	{
		if ( (res->ai_family != AF_INET) && (res->ai_family != AF_INET6) )
			continue;
		else
			break;
	}

	if (res->ai_family == AF_INET)
	{
		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd == -1)
			error("Error on socket");
		err = connect(fd, res->ai_addr, res->ai_addrlen);
		if (err < 0)
			error("Error on connecting to client port on active FTP");
		
	}

	if (res->ai_family == AF_INET6)
	{
		fd = socket(AF_INET6, SOCK_STREAM, 0);
		if (fd == -1)
			error("Error on socket");
		err = connect(fd, res->ai_addr, res->ai_addrlen);
		if (err < 0)
			error("Error on connecting to client port on active FTP");
	}

	return fd;
}

// TODO: FURTHER SANITY CHECK AND CLEANUP, FINISH DEBUGGING, then make multithreaded
			// EXACT COMMAND SEQUENCE: https://www.webdigi.co.uk/blog/2009/ftp-using-raw-commands-and-telnet/
				/*

				   For example, a user connects to the directory /usr/dm, and creates
      				a subdirectory, named pathname:

         			CWD /usr/dm
         			200 directory changed to /usr/dm
    	     		MKD pathname
        		    257 "/usr/dm/pathname" directory created
      				An example with an embedded double quote:

         			MKD foo"bar
         			257 "/usr/dm/foo""bar" directory created
        			CWD /usr/dm/foo"bar
         			200 directory changed to /usr/dm/foo"bar
         		*/
