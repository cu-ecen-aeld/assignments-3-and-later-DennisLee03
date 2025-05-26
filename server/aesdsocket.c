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

bool is_daemon = false;
bool terminate = false;



// This function prints out the msg to stderr (only if not
// running in daemon mode) and to syslog. If errnum is not
// 0 (errnum is expected to be equal to errno), prints out
// the system error together with the msg
void handle_error(char* msg, int errnum) {
  if (errnum != 0) {
    char* syserr = strerror(errnum);
    if (is_daemon == false) perror(msg);
    syslog(LOG_ERR, "%s: %s\n", msg, syserr);
  } else {
    if (is_daemon == false) fprintf(stderr, "%s\n", msg);
    syslog(LOG_ERR, "%s\n", msg);
  }
}


// Closes a socket, given the fd. Prints error
// to stderr and logs to syslog, if error occurs
void sock_close(int sfd) {
  if (shutdown(sfd, SHUT_RDWR) !=0) {
    handle_error("Socket shutdown failed", errno);
  }
  if (close(sfd) != 0) {
    handle_error("Socket close failed", errno);
  }
}


// Closes a file, given the fd. Prints error
// to stderr and logs to syslog, if error occurs
void file_close(int ffd) {
  if (close(ffd) != 0) {
    handle_error("File close failed", errno);
  }
}


// Terminates the program with EXIT_FAILURE status.
// Handles the error message and errnum error code,
// which is expected to be errno, simply. Finally
// closes the socket represented by sfd, if sfd is
// not -1, before terminating the program
void exit_on_failure(char* msg, int errnum, int sfd) {
  handle_error(msg, errnum);
  if (sfd != -1) sock_close(sfd);
  exit(EXIT_FAILURE);
}


// Signal handler for that terminates the server
void shutdown_server(int signum) {
  terminate = true;
}


// Sets the peer ip address into the char array passed in.
// Assumes that the array is of the appropriate size len.
// Returns true on success, false on failure
bool get_client_ip(int sockfd, char* client_ip, size_t len) {
  struct sockaddr_in sa;
  socklen_t sz = sizeof(sa);
  if (getpeername(sockfd, (struct sockaddr*)&sa, &sz) != 0) {
    handle_error("Failed to get peer info", errno);
    return false;
  }
  inet_ntop(AF_INET, &(sa.sin_addr), client_ip, len);
  return true;
}


// Reads data from sockfd until a newline is seen, appends the
// data to a file MESSAGES_FILE and writes the entire
// content of the file MESSAGES_FILE to the sockfd and closes
// it. The client is expected to send a message terminated by
// '\n'. Returns true on success, false on failure
bool receive(int sockfd) {
  char* ptr_eom = NULL;
  int outfd = open(MESSAGES_FILE, O_CREAT|O_WRONLY|O_APPEND,
		   S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (outfd == -1) {
    handle_error("Could not open out file", errno);
    return false;
  }
  while (ptr_eom == NULL) { // Loop till '\n' indicates end of data
    // The read buffer; ensure null-terminated to make string
    // manipulation easier, so BUFF_SZ+1 and init to '\0'
    char rbuffer[BUFF_SZ+1] = {'\0'};
    ssize_t count = recv(sockfd, rbuffer, BUFF_SZ, 0); 
    if (count == -1) {
      handle_error("Failed to read data from client", errno);
      file_close(outfd);
      return false;
    } else if (count == 0) {
      handle_error("No data received from client", 0);
      file_close(outfd);
      return false;
    }
    // Check for \n; If count != position('\n') + 1,
    // extra data sent. Error out
    ptr_eom = strchr(rbuffer, '\n');
    if (ptr_eom != NULL && ptr_eom != rbuffer+count-1) {
      handle_error("Unexpected data after new-line", 0);
      file_close(outfd);
      return false;
    }
    // Write only count bytes, discarding the trailing '\0'
    if (write(outfd, rbuffer, count) != count) {
      handle_error("Error writing to out file", errno);
      file_close(outfd);
      return false;
    }
  }
  file_close(outfd);
  return true;
}


void dispatch(int sockfd) {
  int infd = open(MESSAGES_FILE, O_RDONLY, 0);
  if (infd == -1) {
    handle_error("Could not open in file", errno);
    return;
  }
  char buffer[BUFF_SZ];
  for (ssize_t rc = read(infd, buffer, BUFF_SZ); rc != 0;
               rc = read(infd, buffer, BUFF_SZ)) {
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



// Handles communication with a client - reading the data
// sent from the client and sending the response. The
// response is sent only if the data from the client is
// read successfully
void converse(int sockfd) {
  char client_ip[INET_ADDRSTRLEN+1];
  client_ip[INET_ADDRSTRLEN] = '\0'; // Ensure NULL-terminated
  if (get_client_ip(sockfd, client_ip, INET_ADDRSTRLEN)) {
    // Log connection accept
    if (is_daemon == false) {
      fprintf(stdout, "Accepted connection from %s\n", client_ip);
    }
    syslog(LOG_INFO, "Accepted connection from %s\n", client_ip);
  }

  if (receive(sockfd) == true) dispatch(sockfd);
  
  sock_close(sockfd);
  if (is_daemon == false) {
    fprintf(stdout, "Closed connection from %s\n", client_ip);
  }
  syslog(LOG_INFO, "Closed connection from %s\n", client_ip);
}



void init_server(char* appname) {
  openlog(appname, LOG_CONS, LOG_USER);

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    exit_on_failure("Server socket create failed", errno, -1);
  }

  // Set socket options (at socket level, hence SOL_SOCKET, not AF_INET)
  int enable=1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
		 &enable, sizeof(enable)) != 0) {
    exit_on_failure("Socket option setting failed", errno, sockfd);
  }

  struct sockaddr_in sa = {
      .sin_family=AF_INET, .sin_port=htons(PORT), .sin_addr=INADDR_ANY 
  };
  if (bind(sockfd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
    exit_on_failure("Server socket bind failed", errno, sockfd);
  }

  // Listen
  if (is_daemon == false) {
    fprintf(stdout, "Listening at localhost:%d\n", PORT);
  }
  syslog(LOG_INFO, "Listening at localhost:%d\n", PORT);
  if (listen(sockfd, ACCEPT_BACK_LOG) != 0) {
    exit_on_failure("Server socket listen failed", errno, sockfd);
  }

  // Set up signal handler
  struct sigaction siga = { .sa_handler=shutdown_server };
  if (sigaction(SIGINT, &siga, NULL) != 0 ||
      sigaction(SIGTERM, &siga, NULL) != 0) {
    exit_on_failure("Failed to set up signal handler", errno, sockfd);
  }

  // Wait for connection
  while (terminate == false) {
    struct sockaddr ca;
    socklen_t sz = sizeof(ca);
    int psockfd = accept(sockfd, &ca, &sz);
    if (psockfd == -1) {
      if (terminate == false) {
	handle_error("Failed to accept client connection", errno);
      }
    } else converse(psockfd);
  }
  if (is_daemon == false) fprintf(stdout, "Caught signal, exiting\n");
  syslog(LOG_INFO, "Caught signal, exiting\n"); 
  sock_close(sockfd);
  // Delete file
  if (remove(MESSAGES_FILE) != 0) {
    handle_error("Could not delete out file", errno);
  }
  closelog();
}


int main(int argc, char** argv) {
  if (argc == 2 && strcmp(argv[1], "-d") == 0) is_daemon = true;      

  if (is_daemon == true) {
    pid_t cpid = fork();
    if (cpid == -1) {
      handle_error("Failed to create child process", errno);
      return EXIT_FAILURE;
    }
    if (cpid == 0) {
      init_server(argv[0]);
    } else {
      printf("Created server process with pid %d\n", cpid);
    }
  } else {
    init_server(argv[0]);
  }
  return EXIT_SUCCESS;
}