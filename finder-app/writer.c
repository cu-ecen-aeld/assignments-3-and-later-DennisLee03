#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>


int main(int argc, char *argv[]) {
    // open syslog, specify LOG_USER facility
    openlog("writer", LOG_PID, LOG_USER);

    // if number of args is wrong, we write an error message into syslog with LOG_ERR level
    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: expected 2, got %d", argc - 1);
        fprintf(stderr, "Usage: %s <writefile> <writestr>\n", argv[0]);
        closelog();
        exit(1);
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    // try to open and over-write
    int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    // cannot open the file
    if (fd == -1) {
        syslog(LOG_ERR, "Error opening file %s", writefile);
        perror("open");
        closelog();
        exit(1);
    }

    // try to write content into file
    ssize_t write_len = write(fd, writestr, strlen(writestr));
    
    // fail to write
    if (write_len == -1) {
        syslog(LOG_ERR, "Error writing to file %s", writefile);
        perror("write");
        close(fd);
        closelog();
        exit(1);
    }

    // success
    syslog(LOG_DEBUG, "Writing \"%s\" to %s", writestr, writefile);
    close(fd);
    closelog();

    return 0;
}
