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
	const char * port = argv[1];
	int err;
	int server_fd = initiate_server(port);

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

		ssize_t nread, nwrite;
		struct sockaddr_storage client_addr;
		int client_fd = -1;

		if (fds[0].revents & POLLIN)
		{
			socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
			client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);
			nwrite = write(client_fd, "220 CoolFTPServer\0",strlen("200 CoolFTPServer\0"));
			if (nwrite < 0)
				error("Error on writing initial connection success status to client");
		}

		char buf[4096];
		char * buf_ptr = buf;
		memset(buf_ptr, 0, sizeof(buf));
		char * PORT = NULL;

		while ( (nread = read(client_fd, buf_ptr, sizeof(buf))) > 0)
		{
			printf("READ FROM CLIENT:%s\n",buf_ptr);
			fflush(stdout);
			buf[strcspn(buf_ptr, "\r\n")] = 0; 
			char * str = strtok(buf_ptr, " ");

			if ( (strcmp(str, "SSH-2.0-OpenSSH_6.2") == 0) )
			{
				nwrite = write(client_fd, "220 CoolFTPServer\0",strlen("200 CoolFTPServer\0"));
				if (nwrite < 0)
					error("Error on sending server identification message");
				printf("Wrote initial welcome message\n");
				fflush(stdout);
			}

			else if (strcmp(str, "USER") == 0)
			{
				printf("In command USER!\n");
				fflush(stdout);
				nwrite = write(client_fd, "331\0", strlen("331\0"));
				if (nwrite < 0)
					error("Error on accepting username");
			}

			else if(strcmp(str,"PASS") == 0)
			{
				printf("In command PASS!\n");
				fflush(stdout);
				nwrite = write(client_fd, "230\0", strlen("230\0"));
				if (nwrite < 0)
					error("Error on accepting user password");
			}
  		
			else if (strcmp(str, "CWD") == 0)
			{
				printf("In command CWD!\n");
				fflush(stdout);
				str = strtok(NULL, " ");
				err = chdir(str);
				if (err < 0)
					error("Error on CWD command");

				nwrite = write(client_fd, "250\0", strlen("250\0"));
			    if (nwrite < 0)
				    error("Error on writing CWD success status to client");
			}
			else if (strcmp(str, "PORT")==0)
			{
				printf("In command PORT!\n");
				fflush(stdout);
				PORT = calloc(strlen(str)+1, 1);
				strcpy(PORT, str);
				nwrite = write(client_fd, "200\0", strlen("200\0"));
			    if (nwrite < 0)
				    error("Error on writing PORT success status to client");
			}
			else if (strcmp(str, "LIST") == 0)
			{
				printf("In command LIST!\n");
				fflush(stdout);
				char * directory_list = LIST(getenv("PWD"));
				if (PORT == NULL)
				{
					int data_fd = socket(client_addr.ss_family, SOCK_STREAM, 0);
					if (err < 0)
						error("Error on establishing socket for client data transfer");
					err = connect(data_fd, (struct sockaddr *)&client_addr, 
						(socklen_t)sizeof(struct sockaddr_storage));
					if (err < 0)
						error("Error on connecting to client for data transfer");
					nwrite = write(data_fd, directory_list, strlen(directory_list));
			    	if (nwrite < 0)
				    	error("Error on writing PORT succes status to client");
				    nwrite = write(client_fd, "226\0", strlen("226\0"));
				    if (nwrite < 0)
				    	error("Erorr on communicating successful listing to client");

				    close(data_fd);
				}
				else
				{
					/* How to change solely the port of the client? */
				}

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
					nwrite = write(client_fd, "550\0", sizeof("550\0"));
					if (nwrite < 0)
						error("Error on communicating file does not exist error code to client\n");
				}

				if (PORT == NULL)
				{
					int data_fd = socket(client_addr.ss_family, SOCK_STREAM, 0);
					if (err < 0)
						error("Error on establishing socket for client data transfer");
					err = connect(data_fd, (struct sockaddr *)&client_addr, 
						(socklen_t)sizeof(struct sockaddr_storage));
					if (err < 0)
						error("Error on connecting to client for data transfer");

					err = RETR(file_fd, data_fd);
					if (err < 0)
						nwrite = write(client_fd, "550\0", sizeof("550\0"));
					else
						nwrite = write(client_fd, "200\0", sizeof("200\0"));
					close(data_fd);
					close(file_fd);
				}
				else
				{
					/* How to change solely the port of the client? */
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
					file_exists = 1;

				if (!file_exists)
				{
					// O_EXCL since we don't want concurrent access to the file
					file_fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0644);
					if (file_fd < 0)
					{
						if (errno == EEXIST)
						{
							/* Need to send some sort of error, to notify user about concurrent access */
							continue;
						}
					}

				}
				else
				{
					//O_EXCL since we don't want concurrent access to the file
					file_fd = open(filename, O_EXCL | O_WRONLY, 0644);
					if (file_fd < 0)
					{
						/* Need some sort of error, to notify user about concurrent access */
						continue;
					}
				}

				if (PORT == NULL)
				{
					int data_fd = socket(client_addr.ss_family, SOCK_STREAM, 0);
					if (err < 0)
						error("Error on establishing socket for client data transfer");
					err = connect(data_fd, (struct sockaddr *)&client_addr, 
						(socklen_t)sizeof(struct sockaddr_storage));
					if (err < 0)
						error("Error on connecting to client for data transfer");

					err = STOR(file_fd, data_fd);
					if (err < 0)
						nwrite = write(client_fd, "550\0", sizeof("550\0"));
					else
						nwrite = write(client_fd, "200\0", sizeof("200\0"));
					close(data_fd);
					close(file_fd);
				}
				else
				{
					/* How to change solely the port of the client? */
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
					error("Error on opening file for appending");

				if (PORT == NULL)
				{
					int data_fd = socket(client_addr.ss_family, SOCK_STREAM, 0);
					if (err < 0)
						error("Error on establishing socket for client data transfer");
					err = connect(data_fd, (struct sockaddr *)&client_addr, 
						(socklen_t)sizeof(struct sockaddr_storage));
					if (err < 0)
						error("Error on connecting to client for data transfer");

					// Use same function as STOR command
					err = STOR(file_fd, data_fd);
					if (err < 0)
						nwrite = write(client_fd, "550\0", sizeof("550\0"));
					else
						nwrite = write(client_fd, "200\0", sizeof("200\0"));
					close(data_fd);
					close(file_fd);
				}
				else
				{
					/* How to change solely the port of the client? */
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
					nwrite = write(client_fd, "550\0", sizeof("550\0"));
				else
					nwrite = write(client_fd, "200\0", sizeof("200\0"));
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
					nwrite = write(client_fd, "550\0", sizeof("550\n"));
				else
					nwrite = write(client_fd, "200\0", sizeof("200\0"));
			}
			else
			{
				// nwrite = write(client_fd, "220 ProFTPD 1.3.1 Server (ProFTPD) [64.170.98.33]\0",strlen("200 CoolFTPServer\0"));
				// if (nwrite < 0)
				// 	error("Error on sending server identification message");
				// printf("Sent message and about to loop \n");
				// fflush(stdout);
				printf("Nothing got compared..\n");
				fflush(stdout);
				memset(buf_ptr, 0, sizeof(buf));
				continue;
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
         	memset(buf_ptr,0,sizeof(buf));
		}
		if (nread < 0)
		{
			printf("Nothing read, starting again!\n");
			fflush(stdout);
			free(PORT);
			PORT = NULL;
			memset(buf_ptr, 0, sizeof(buf));
			printf("Client connection stopped or failed!");
			fflush(stdout);
			continue;
		}

		memset(buf_ptr, 0, sizeof(buf));
		free(PORT);
		PORT = NULL;
		printf("Client connection stopped or failed!");
		fflush(stdout);
		continue;
	}
}