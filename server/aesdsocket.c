#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>

#define PORT "9000"
#define BACKLOG 10
#define BUFFER_SIZE 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t exit_requested = 0;
int server_fd = -1;

void signal_handler(int signum) {
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_requested = 1;
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    int client_fd = -1;
    int ret;

    if (argc > 1) {
        if (strcmp(argv[1], "-d") == 0)
        {
            daemon_mode = 1;
        } else {
            fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    openlog(argv[0], LOG_PID | LOG_CONS, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigfillset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Sigaction failed");
        exit(-1);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Sigaction failed");
        exit(-1);
    }

    struct addrinfo hints;
    struct addrinfo *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    ret = getaddrinfo(NULL, PORT, &hints, &res);
    if( ret != 0 ) {
        syslog(LOG_ERR, "getaddrinfo failed: %s", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }

    server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd == -1)
    {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    ret = bind(server_fd, res->ai_addr, res->ai_addrlen);
    if (ret == -1)
    {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(res);

    if(daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "daemon_mode fork failed: %s", strerror(errno));
            close(server_fd);
            exit(EXIT_FAILURE);
        }
        if( pid > 0) {
            exit(EXIT_SUCCESS);
        }
        if( setsid() < 0) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            close(server_fd);
            exit(EXIT_FAILURE);
        }
    }

    ret = listen(server_fd, BACKLOG);
    if( ret == -1 ) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while(!exit_requested) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if(client_fd == -1){
            if (errno == EINTR) {
                break;
            }
            syslog( LOG_ERR, "accept failed: %s", strerror(errno));
            continue;
        }
        
        char client_ip[INET6_ADDRSTRLEN];
        void *addr;
        if (((struct sockaddr*)&client_addr)->sa_family == AF_INET) {
            addr = &(((struct sockaddr_in*)&client_addr)->sin_addr);
        }else {
            addr = &(((struct sockaddr_in6*)&client_addr)->sin6_addr);
        }
        inet_ntop(((struct sockaddr *)&client_addr)->sa_family, addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
        
        FILE *fp = fopen(DATA_FILE, "a+");
        if(fp == NULL) {
            syslog(LOG_ERR, "fopen() failed for %s: %s", DATA_FILE, strerror(errno));
            close(client_fd);
            continue;
        }
        
        char recv_buf[BUFFER_SIZE];
        ssize_t bytes_read;
        int newline_received = 0;
        while (( bytes_read = recv(client_fd, recv_buf, BUFFER_SIZE, 0)) > 0) {
            if( fwrite(recv_buf, 1, bytes_read, fp) != (size_t)bytes_read) {
                syslog( LOG_ERR, "fwrite() failed: %s", strerror(errno));
                break;
            }
            if ( memchr(recv_buf, '\n', bytes_read)) {
                newline_received = 1;
                break;
            }
        }

        fflush(fp);

        if (newline_received) {
            rewind(fp);
            char file_buf[BUFFER_SIZE];
            size_t bytes_file;
            while ((bytes_file = fread(file_buf, 1, BUFFER_SIZE, fp)) > 0 ) {
                size_t total_sent = 0;
                while (total_sent < bytes_file) {
                    ssize_t sent = send(client_fd, file_buf + total_sent, bytes_file - total_sent, 0);
                    if ( sent == -1) {
                        syslog(LOG_ERR, "send failed: %s", strerror(errno));
                        break;
                    }
                    total_sent += sent;
                }
            }
        }
        
        fclose(fp);
        close(client_fd);
        client_fd = -1;
        syslog(LOG_INFO, "Close connection from %s", client_ip);
    }

    if (server_fd != -1) {
        close(server_fd);
    }
    unlink(DATA_FILE);
    closelog();
    return 0;
}