#include "ftp_functions.h"

void
error(const char * message)
{
	perror(message);
	printf("errno: %d\n", errno);
	exit(1);
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

		// Ignore control directories
		if (!(strcmp(cur_dir_entry->d_name,".")) || !(strcmp(cur_dir_entry->d_name,"..")))
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
initiate_server(const char * port)
{
	int err, fd =0;
	struct addrinfo hints;struct addrinfo * res;struct addrinfo * res_original;
	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(NULL, port, &hints, &res_original);
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
	}

	if (nread < 0)
		return -1;
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
	}

	if (nread < 0)
		return -1;
	else
		return 0;	
}
