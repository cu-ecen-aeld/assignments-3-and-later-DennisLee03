#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>

#define PORT 9000
#define ACCEPT_BACK_LOG 10
#define MESSAGES_FILE "/var/tmp/aesdsocketdata"
#define BUFF_SZ 64
#define SOCK_FAMILY AF_INET

bool is_daemon = false;
bool terminate = false;

void handle_error(char* msg, int errnum);
void run_server(char *servername);
void sock_close(int sfd);
void exit_on_failure(char* msg, int errnum, int sfd);
void file_close(int ffd);
void shutdown_server(int signum);
void converse(int sockfd);
bool receive(int sockfd);
void dispatch(int sockfd);
bool get_client_ip(int sockfd, char* client_ip, size_t len);

int main(int argc, char** argv) {

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        is_daemon = true;
    }

    if(is_daemon) {
        pid_t cpid = fork();
        if(cpid == -1) {
            // fail to create child process
            handle_error("Failed to create a child process", errno);
            return EXIT_FAILURE;
        }
        if(cpid == 0) {
            run_server(argv[0]);
        } else {
            // parent process prints
            printf("Created server process with pid %d\n", cpid);
        }
    } else {
        run_server(argv[0]);
    }

    return EXIT_SUCCESS;
}

void run_server(char *servername) {
    openlog(servername, LOG_CONS, LOG_USER);

    int socket_fd = socket(SOCK_FAMILY, SOCK_STREAM, 0);

    if (socket_fd == -1) {
        exit_on_failure("Server socket create failed", errno, -1);
    }

    // setup socket option
    int enable=1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0) {
        exit_on_failure("Socket option setting failed", errno, socket_fd);
    }

    // bind
    struct in_addr ip_addr;
    if (inet_pton(SOCK_FAMILY, SOCK_FAMILY == AF_INET ? "127.0.0.1" : "::1", &ip_addr) <= 0) {
        exit_on_failure("IP address init failed", errno, socket_fd);
    }
    struct sockaddr_in sa = {
        .sin_family=SOCK_FAMILY, 
        .sin_port=htons(PORT), 
        .sin_addr=ip_addr
    };
    if (bind(socket_fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        exit_on_failure("Server socket bind failed", errno, socket_fd);
    }

    // Listen
    if (is_daemon == false) {
        fprintf(stdout, "Listening at localhost:%d\n", PORT);
    }
    syslog(LOG_INFO, "Listening at localhost:%d\n", PORT);
    if (listen(socket_fd, ACCEPT_BACK_LOG) != 0) {
        exit_on_failure("Server socket listen failed", errno, socket_fd);
    }

    // register signals
    struct sigaction sigact = { .sa_handler=shutdown_server };
    if (sigaction(SIGINT, &sigact, NULL) != 0 || sigaction(SIGTERM, &sigact, NULL) != 0) {
        exit_on_failure("Failed to set up signal handler", errno, socket_fd);
    }

    // Wait for connection
    while (terminate == false) {
        struct sockaddr ca;
        socklen_t sz = sizeof(ca);
        int psockfd = accept(socket_fd, &ca, &sz);
        if (psockfd == -1) {
            if (terminate == false) {
                handle_error("Failed to accept client connection", errno);
            }
        } else converse(psockfd);
    }

    if (is_daemon == false) fprintf(stdout, "Caught signal, exiting\n");
    syslog(LOG_INFO, "Caught signal, exiting\n"); 
    sock_close(socket_fd);
    // Delete file
    if (remove(MESSAGES_FILE) != 0) {
        handle_error("Could not delete out file", errno);
    }
    closelog();
}

void handle_error(char* msg, int errnum) {
    if (errnum != 0) {
        char* syserr = strerror(errnum);
        if(is_daemon == false) 
            perror(msg);
        syslog(LOG_ERR, "%s: %s\n", msg, syserr);
    } else {
        if(is_daemon == false) 
            fprintf(stderr, "%s\n", msg);
        syslog(LOG_ERR, "%s\n", msg);
    }
}

void sock_close(int sfd) {
    if(shutdown(sfd, SHUT_RDWR) !=0) {
        handle_error("Socket shutdown failed", errno);
    }
    if(close(sfd) != 0) {
        handle_error("Socket close failed", errno);
    }
}

void exit_on_failure(char* msg, int errnum, int sfd) {
    handle_error(msg, errnum);
    if(sfd != -1) sock_close(sfd);
    exit(EXIT_FAILURE);
}

void file_close(int ffd) {
    if(close(ffd) != 0) {
        handle_error("File close failed", errno);
    }
}

void shutdown_server(int signum) {
    terminate = true;
}

void converse(int sockfd) {
    size_t addr_len = (SOCK_FAMILY == AF_INET ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN);
    char client_ip[addr_len+1];
    client_ip[addr_len] = '\0'; // Ensure NULL-terminated
    if (get_client_ip(sockfd, client_ip, addr_len)) {
        // Log connection accept
        if (is_daemon == false) {
            fprintf(stdout, "Accepted connection from %s\n", client_ip);
        }
        syslog(LOG_INFO, "Accepted connection from %s\n", client_ip);
    }

    receive(sockfd);

    sock_close(sockfd);
    if (is_daemon == false) {
        fprintf(stdout, "Closed connection from %s\n", client_ip);
    }
    syslog(LOG_INFO, "Closed connection from %s\n", client_ip);
}

bool receive(int sockfd) {
	char buffer[BUFF_SZ];
	char *packet = NULL;
	size_t packet_size = 0;
	ssize_t count;

	while ((count = recv(sockfd, buffer, BUFF_SZ, 0)) > 0) {
		char *new_packet = realloc(packet, packet_size + count);
		if (!new_packet) {
			handle_error("Memory allocation failed", 0);
			free(packet);
			return false;
		}
		packet = new_packet;
		memcpy(packet + packet_size, buffer, count);
		packet_size += count;

		// 如果有 \n 就結束接收
		if (memchr(buffer, '\n', count) != NULL) {
			break;
		}
	}

	if (count == -1) {
		handle_error("recv failed", errno);
		free(packet);
		return false;
	}

	// 寫入檔案
	int outfd = open(MESSAGES_FILE, O_CREAT | O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (outfd == -1) {
		handle_error("Could not open out file", errno);
		free(packet);
		return false;
	}

	if (write(outfd, packet, packet_size) != packet_size) {
        handle_error("Error writing to out file", errno);
        free(packet);
        file_close(outfd);
        return false;
    }
    file_close(outfd);

    if (send(sockfd, packet, packet_size, 0) != packet_size) {
        handle_error("Error sending data back to client", errno);
        free(packet);
        return false;
    }

    free(packet);
    return false;
}


void dispatch(int sockfd) {
    int infd = open(MESSAGES_FILE, O_RDONLY, 0);
    if (infd == -1) {
        handle_error("Could not open in file", errno);
        return;
    }
    char buffer[BUFF_SZ];
    for (ssize_t rc = read(infd, buffer, BUFF_SZ); rc != 0; rc = read(infd, buffer, BUFF_SZ)) {
        if (rc == -1) {
            handle_error("Failed to read data from client", errno);
            file_close(infd);
            return;
        }
        if (send(sockfd, buffer, rc, 0) != rc) {
            if (rc == -1) handle_error("Failed to send data", errno);
            else handle_error("Failed to send complete data", 0);
            file_close(infd);
            return;
        }
    }
    file_close(infd);
}

bool get_client_ip(int sockfd, char* client_ip, size_t len) {
  struct sockaddr_in sa;
  socklen_t sz = sizeof(sa);
  if (getpeername(sockfd, (struct sockaddr*)&sa, &sz) != 0) {
    handle_error("Failed to get peer info", errno);
    return false;
  }
  inet_ntop(SOCK_FAMILY, &(sa.sin_addr), client_ip, len);
  return true;
}