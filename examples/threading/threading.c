#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define MALLOC(p,s) \
    if(!(p = malloc(s))) {\
        fprintf(stderr, "Insufficeint memory.");\
        exit(EXIT_FAILURE);\
    }

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    // wait
    usleep(thread_func_args->wait_to_obtain_ms*1000);

    // obtain mutex
    if(pthread_mutex_lock(thread_func_args->mutex) != 0) {
        thread_func_args->thread_complete_success = false;
        ERROR_LOG("Failed to lock mutex");
        return thread_func_args;
    }

    // wait
    usleep(thread_func_args->wait_to_release_ms*1000);

    // release mutex
    if(pthread_mutex_unlock(thread_func_args->mutex) != 0) {
        thread_func_args->thread_complete_success = false;
        ERROR_LOG("Failed to unlock mutex");
        return thread_func_args;
    }

    thread_func_args->thread_complete_success = true;

    return thread_func_args;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO:  to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    // allocate memory for thread_data
    struct thread_data* data;
    MALLOC(data, sizeof(*data));

    // setup mutex and wait arguments, pass thread_data
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;

    // create thread
    int ret = pthread_create(thread, NULL, threadfunc, data);
    if(ret != 0) {
        free(data);
        return false;
    }

    return true;
}

