#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define BASE_DIR "public"
#define MAXLINE 2000
#define LISTENQ 8 // Max number of client connections

typedef struct {
	char *code;
	char *content_type;
	
	char *file;
} http_response;

int open_listenfd(char *port);
void do_work(int connfd);
char *get_request(int fd);
char *parse_uri(const char *request);
http_response create_response(char* uri);
char *get_full_path(const char *path);

int main(int argc, char **argv)
{
	int listenfd, connfd;
	struct sockaddr_storage clientaddr;
	socklen_t clientlen;
	
	if(argc != 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	
	listenfd = open_listenfd(argv[1]);
	while (1) 
	{
		char hostname[MAXLINE];
		char port[MAXLINE];
		pid_t pid;
		
		clientlen = sizeof(clientaddr);
		connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
		getnameinfo((struct sockaddr *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s %s)\n", hostname, port);
		
		pid = fork();
		if(pid == -1)
		{
			printf("forking failed\n");
			close(connfd);
			return -1;
		}
		
		// Child process;
		if(pid == 0)
		{
			do_work(connfd);
			close(connfd);
			break;
		}
		else
			close(connfd);
	}
	
	close(listenfd);
	return 0;
}

int open_listenfd(char *port)
{
	int listenfd, optval = 1;
	struct addrinfo hints, *listp, *p;
	
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM; 				/* Open a connection */
	hints.ai_flags |= AI_PASSIVE | AI_ADDRCONFIG;	/* On any IP address */
	getaddrinfo(NULL, port, &hints, &listp);
	
	for (p = listp; p; p = p->ai_next)
	{
		/* Create a socket descriptor */
		if((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
			continue; /* Socket failed, try the next one */
		
		/* Eliminates "Address already in use" error from bind */
		setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));
		
		/* Bind to the server */
		if(bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
			break; /* Success */
			
		close(listenfd);
	}
	
	/* Cleanup */
	freeaddrinfo(listp);
	if(!p)	/* No address worked */
		return -1;
	
	/* Make it a listening socket ready to accept connection requests */
	if(listen(listenfd, LISTENQ) < 0)
	{
		close(listenfd);
		return -1;
	}
	
	{
		char buf[MAXLINE];
		getnameinfo(p->ai_addr, p->ai_addrlen, buf, MAXLINE, NULL, 0, 0);
		printf("Listening at %s\n", buf);
	}		
	return listenfd;
}

void do_work(int connfd)
{
	int n = 0, file_fd;
	char *request, *uri, *path;
	char buffer[MAXLINE];
	http_response response;
	
	request = get_request(connfd);
	uri = parse_uri(request);
	printf("URI %s\n", uri);
	free(request);
	
	response = create_response(uri);
	path = get_full_path(response.file);
	
	printf("File: %s\n", path);
	printf("Header: %s\n", response.code);
	
	send(connfd, response.code, strlen(response.code), 0);
	send(connfd, response.content_type, strlen(response.content_type), 0);
	send(connfd, "\r\n", strlen("\r\n"), 0);
	
	file_fd = open(path, 0, 0);
	while(1)
	{
		n = read(file_fd, buffer, MAXLINE);
		if(n == 0)
			break;
			
		send(connfd, buffer, n, 0);
	}
	
	free(uri);
	free(path);
}

char *get_request(int fd)
{
	FILE *sstream;
	size_t size = 1, line_size = 1, line_usable_size = 1;  
	char *request = malloc(sizeof(char) * size);
	char *line = malloc(sizeof(char) * size);

	if((sstream = fdopen(fd, "r")) == NULL)
	{
		fprintf(stderr, "Error allocating memory while getting request\n");
		exit(1);
	}
	
	*request = '\0';
	while((line_size = getline(&line, &line_usable_size, sstream)) > 0)
	{
		// If the line read is blank and has only carraige return
		if(strcmp(line, "\r\n") == 0)
			break;
		
		request = realloc(request, size + line_size);
		size += line_size;
		strcat(request, line);
	}
	free(line);
	return request;
}

#define VERB_GET "GET "
#define VERB_GET_LEN strlen(VERB_GET)
#define HTTP_PARAM "?"
#define HTTP_PROTOCOL " HTTP/1"

char *parse_uri(const char *request)
{
	char *request_uri;
	char *pos_get = strstr(request, VERB_GET);
	char *pos_param = strstr(pos_get, HTTP_PARAM);
	char *pos_http = strstr(pos_get, HTTP_PROTOCOL);
	
	char *pos_start, *pos_end;
	size_t len;
	
	if(!pos_get || !pos_http)
	{
		fprintf(stderr, "Type of request not supported\n");
		exit(1);
	}
	pos_start = pos_get + VERB_GET_LEN;
	pos_end = pos_param && (pos_param>pos_start && pos_param < pos_http) ? pos_param : pos_http;
	
	len = pos_end - pos_start;
	request_uri = malloc(len + 1);
	for(int i = 0; i < len; ++i)
	{
		request_uri[i] = pos_start[i];
	}
	request_uri[len] = '\0';
	
	return request_uri;
}

#define BASE_DIR_LEN strlen(BASE_DIR)

char *get_full_path(const char *path)
{
	size_t size = (BASE_DIR_LEN + strlen(path) + 1) * sizeof(char);
	char *full_path = malloc(size);
	
	strcpy(full_path, BASE_DIR);
	strcat(full_path, path);
	return full_path;
}

const char *get_extension(const char *path)
{
	const char *dot = strrchr(path, '.');
	if(!dot)
		return NULL;
	return dot + 1;
}

#define HTTP_200 "HTTP/1.1 200 OK\r\n"
#define HTTP_400 "HTTP/1.1 400 Bad Request\r\n"
#define HTTP_404 "HTTP/1.1 404 Not Found\r\n"
#define CONTENT_TYPE(type) "Content-Type:" type "charset=UTF-8\r\n"

#define BAD_STRING ".."

http_response create_response(char* uri)
{
	http_response response;
	char *path;
	
	if(strcmp(uri, "/") == 0)
		path = "/index.html";
	else
		path = uri;
	
	if(strcmp(uri, BAD_STRING) == 0)
	{
		response.code = HTTP_400;
		response.content_type = CONTENT_TYPE ("text/html;");
		response.file = "/400.html";
	}
	else
	{
		char *full_path = get_full_path(path);
		FILE *exists = fopen(full_path, "r");
		
		if(exists)
		{
			const char *ext = get_extension(path);
			response.code = HTTP_200;
			response.file = path;
			
			if(strcmp(ext, "html") == 0)
				response.content_type = CONTENT_TYPE ("text/html;");
			else if(strcmp(ext, "js") == 0)
				response.content_type = CONTENT_TYPE ("text/javascript;");
			else if(strcmp(ext, "css") == 0)
				response.content_type = CONTENT_TYPE ("text/css;");
			else if(strcmp(ext, "ico") == 0)	
				response.content_type = CONTENT_TYPE ("image/icon;");
			else if(strcmp(ext, "jpg") == 0)	
				response.content_type = CONTENT_TYPE ("image/jpg;");
			else if(strcmp(ext, "jpeg") == 0)	
				response.content_type = CONTENT_TYPE ("image/jpeg;");
			else if(strcmp(ext, "png") == 0)	
				response.content_type = CONTENT_TYPE ("image/png;");
			else if(strcmp(ext, "gif") == 0)	
				response.content_type = CONTENT_TYPE ("image/gif;");
			else if(strcmp(ext, "pdf") == 0)			
				response.content_type = CONTENT_TYPE ("application/pdf;");
			else
				response.content_type = CONTENT_TYPE ("application/octet-stream;");			
			
			fclose(exists);
		}
		else
		{
			response.code = HTTP_404;
			response.content_type = CONTENT_TYPE ("text/html;");
			response.file = "/404.html";
		}
		
		free(full_path);
	}
	return response;
}


