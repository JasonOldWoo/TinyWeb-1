/* A simple HTTP Web server */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "wrapper.h"
#include <sys/sendfile.h>
#include <sys/wait.h>

#define MAXLINE  8192  /* max text line length */
#define MAXBUF   8192  /* max I/O buffer size */
extern char **environ;

#define ROOT "/home/shanks/project/WebServer/www"
#define PORT 8888


void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmes);


int main(int argc, char const *argv[])
{
    int listenfd, connfd, port, clientlen;
    struct sockaddr_in clientaddr;

    // check CLI args
    if (argc != 2) {
        port = PORT;
        printf("Not defined port, use default port %d...\n", port);
    }
    else {
        port = atoi(argv[1]);
        printf("Now listening to %d\n", port);
    }

    if((listenfd = open_listenfd(port)) < 0)
        perror("open_listenfd");

    while(1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if(connfd < 0){
            perror("accept");
            break;
        }
        doit(connfd);
        if(close(connfd) < 0)
            perror("close");
    }
    return 0;
}

void doit(int fd)
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    rio_readinitb(&rio, fd);
    rio_readlineb(&rio, buf, MAXLINE);
    // sscanf() reads its input from the character string pointed to by buf.
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        client_error(fd, method, "501", "Not Implemented",
                "Micro does not implement this method");
        return;
    }
    printf("request line: %s\n", buf);
    read_requesthdrs(&rio);

    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0) {
        client_error(fd, filename, "404", "Not found",
                "Micro couldn't find this file");
        return;
    }

    if (is_static) { /* Serve static content */
        // S_ISREG: macro -> is regular file?
        // S_IRUSR: user read bit(aka. -r--------)
        // see also: APUE Chapter 4.3 4.4
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            client_error(fd, filename, "403", "Forbidden",
                    "Micro couldn't read the file");
            return;
        }
        serve_static(fd, filename, sbuf.st_size);
    }
    else { /* Serve dynamic content */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            client_error(fd, filename, "403", "Forbidden",
                    "Micro couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs);
    }
}

/* read request headers and print*/
void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {
        rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

/* parse_uri: dynamic or static judgement according to the uri string
 * @uri: arg passed in
 * @filename: arg passed out, baseurl is current dir(".")
 * @cigargs: arg passed out
 *
 * return val:
 * static = 1
 * dynamic = 0
 */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;

    if (!strstr(uri, "/cgi-bin")) {  /* Static content */
    strcpy(cgiargs, "");
    strcpy(filename, ROOT);
    strcat(filename, uri);  // This is not safe enough, may have stackoverflow attack
    if (uri[strlen(uri)-1] == '/')
        strcat(filename, "index.html");  // add default index page
    return 1;
    }
    else {  /* Dynamic content */
        // char *index(const char *s, int c);
        ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgiargs, ptr+1);
            *ptr = '\0';
        }
        else
            strcpy(cgiargs, "");
        strcpy(filename, ROOT);
        strcat(filename, uri);
        return 0;
    }
}


void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    /* Send response headers to client */
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.1 200 OK\r\n");
    sprintf(buf, "%sServer: Micro Web Server\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    // use another '\r\n'(aka. space line) to indicate the ending of response header,
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    rio_writen(fd, buf, strlen(buf));

    /* Send response body to client */
    srcfd = open(filename, O_RDONLY, 0);
    // Memory map IO: read only, copy on write, return the map area address
    // srcp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    // close(srcfd);
    // rio_writen(fd, srcp, filesize);
    // munmap(srcp, filesize);
    sendfile(fd, srcfd, 0, filesize);
    close(srcfd);
}



void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else
        strcpy(filetype, "text/plain");
}


void serve_dynamic(int fd, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist[] = { NULL };
    int fp[2];
    pid_t pid;
    int ret = 0;
    // FILE *fpout;

    // create two file descriptor(a pipe), fp[1] points to fp[0]
    if (pipe(fp) < 0) {
        printf("pipe error\n");
        return;
    }
    if (fork() == 0) { /* child */
        close(fp[0]);   // close child's read pipe
        /* Real server would set all CGI vars here */
        setenv("QUERY_STRING", cgiargs, 1);
        // int dup2(int oldfd, int newfd); dup2() makes newfd be the copy of oldfd, closing newfd first if necessary.
        dup2(fp[1], STDOUT_FILENO);         /* Redirect stdout to client */
        ret = execve(filename, emptylist, environ); /* Run CGI program */
        if (ret == 0) {
            printf("HTTP/1.1 200 OK\r\n");
            printf("Server: Micro Web Server\r\n\r\n");
        }
    }
    waitpid(-1, NULL, 0); /* Parent waits for and reaps child */

    // if (fpout = popen(filename, "r")){
    //     sprintf(buf, "HTTP/1.1 200 OK\r\n");
    //     strcat(buf, "Server: Micro Web Server\r\n\r\n");
    //     rio_writen(fd, buf, strlen(buf));
    //     memset(buf, 0, sizeof(buf));
    //     while (fread(buf, 1, MAXLINE, fpout) != 0) {
    //         rio_writen(fd, buf, strlen(buf));
    //     }
    //     pclose(fpout);
    // }
    // else {
    //     client_error(fd, filename, "500", "Internal Error", "Micro couldn't read the file");
    // }
}


void client_error(int fd, char *cause, char *errnum,
         char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Micro Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Micro Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}
