#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include "parse.h"
#include <pthread.h>
#include <stdbool.h>
#include <poll.h>

#define SIZE 512
#define THREAD_POOL_SIZE 100

typedef struct {
    void* (*function)(void*); 
    struct TaskArguments *argument;           
} Task;


//single producer multiple consumer queufgve

typedef struct {
    pthread_t* threads;  
    Task* tasks;         
    int pool_size;       
    int task_queue_size; 
    int task_queue_front;
    int task_queue_rear;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    bool shutdown;
} ThreadPool;


struct TaskArguments {
    int sockfd_current;
    char* root;
    struct sockaddr_in portIn;
    char ipAddress[INET_ADDRSTRLEN];
};

void *handleConnection(void *arg_param);
void initializeThreadPool(ThreadPool *threadPool, int pool_size, int task_queue_size);
void submitTask(ThreadPool *threadPool, void *(*function)(void *), struct TaskArguments *argument);
void *worker(void *arg);
void shutdownThreadPool(ThreadPool *threadPool);


//Idea:
// ThreadPool threadPool;
// initializeThreadPool(&threadPool, pool_size, task_queue_size);
// submitTask(&threadPool, handleConnection, (void*)stuff);
// shutdownThreadPool(&threadPool);

void* handleConnection(void* arg_param){
    printf("Running");

    struct TaskArguments *args = (struct TaskArguments *)arg_param;
    int sockfd_current = args->sockfd_current;
    char* root = args->root;
    struct sockaddr_in portIn = args->portIn;
    char* ipaddr = args->ipAddress;
    int timeoutInSeconds = 10; // Set your desired timeout here

    printf("Accepting connection...\n\n");
    char buffert[SIZE];

    struct pollfd fds;
    fds.fd = sockfd_current;
    fds.events = POLLIN;

    int pollResult = poll(&fds, 1, timeoutInSeconds * 1000);

    if (pollResult == -1) {
        perror("poll");
        close(sockfd_current);
        pthread_exit(NULL);
    } else if (pollResult == 0) {
        printf("Timeout occurred. No data received within the specified time.\n");
        close(sockfd_current);
        pthread_exit(NULL);
    }

    if (recv(sockfd_current, buffert, sizeof(buffert), 0) == -1) {
        perror("Failed to receive request from client");
        close(sockfd_current);
        pthread_exit(NULL);
    }

    inet_ntop(AF_INET, &portIn.sin_addr, ipaddr, sizeof(ipaddr)); 
    printf("Request from %s:%i\n", ipaddr, ntohs(portIn.sin_port));
    printf("Message: %s\n", buffert);

    Request *request = parse(buffert, sizeof(buffert), sockfd_current);


    if (request) {
        // Just printing everything
        printf("Http Method %s\n", request->http_method);
        printf("Http Version %s\n", request->http_version);
        printf("Http Uri %s\n", request->http_uri);
        for (int index = 0; index < request->header_count; index++) {
            printf("Request Header\n");
            printf("Header name %s Header Value %s\n", request->headers[index].header_name, request->headers[index].header_value);
        }

    } else {
        printf("Parsing Failed\n");
    }

// HTTP/1.1 200 OK
// Content-Type: text/html
// Content-Length: 123
// Server: MyWebServer/1.0

    char body[SIZE];
    char server_name[6] = "myICWS";
    
    char filename[100];
    sprintf(filename, "%s%s", root,request->http_uri);
    int f = open(filename, O_RDONLY);    
    char response[SIZE];
    printf("%s", filename);

    int bodylen = read(f, body, sizeof(body));

    if (f < 0){
        sprintf(response, "HTTP/1.1 404 Not Found\r\n");

    }
    else if (strcmp(request->http_method, "HEAD") == 0){
        sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: text/html\r\nServer: %s\r\n\r\n", bodylen, server_name);
    }
    else{
        sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: text/html\r\nServer: %s\r\n\r\n%s", bodylen, server_name, body);

    }
    

    printf("\nresponse\n%s\n", response);

    if(send(sockfd_current, response, strlen(response) + 1, 0) == -1) 
    {
        perror("send");
        exit(1);
    }
    
    free(args);

    close(sockfd_current);
    pthread_exit(NULL);

    return NULL;
}

void initializeThreadPool(ThreadPool *threadPool, int pool_size, int task_queue_size) {
    threadPool->pool_size = pool_size;
    threadPool->task_queue_size = task_queue_size;
    threadPool->task_queue_front = 0;
    threadPool->task_queue_rear = 0;
    threadPool->shutdown = false;

    // Allocate memory for threads and tasks
    threadPool->threads = (pthread_t *)malloc(pool_size * sizeof(pthread_t));
    threadPool->tasks = (Task *)malloc(task_queue_size * sizeof(Task));

    // Initialize mutex and condition variable
    pthread_mutex_init(&(threadPool->lock), NULL);
    pthread_cond_init(&(threadPool->not_empty), NULL);

    // Create worker threads
    for (int i = 0; i < pool_size; ++i) {
        if (pthread_create(&(threadPool->threads[i]), NULL, worker, (void *)threadPool) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }
}

void submitTask(ThreadPool *threadPool, void *(*function)(void *), struct TaskArguments *argument) {
    pthread_mutex_lock(&(threadPool->lock));

    while ((threadPool->task_queue_rear + 1) % threadPool->task_queue_size == threadPool->task_queue_front) {
        // Queue is full, wait for it to have space
        pthread_cond_wait(&(threadPool->not_empty), &(threadPool->lock));
    }

    // Add task to the queue
    threadPool->tasks[threadPool->task_queue_rear].function = function;
    threadPool->tasks[threadPool->task_queue_rear].argument = argument;
    threadPool->task_queue_rear = (threadPool->task_queue_rear + 1) % threadPool->task_queue_size;

    pthread_mutex_unlock(&(threadPool->lock));
}

void *worker(void *arg) {
    ThreadPool *threadPool = (ThreadPool *)arg;

    while (1) {
        pthread_mutex_lock(&(threadPool->lock));

        while (threadPool->task_queue_front == threadPool->task_queue_rear && !threadPool->shutdown) {
            // Queue is empty, wait for a task
            pthread_cond_wait(&(threadPool->not_empty), &(threadPool->lock));
        }

        if (threadPool->shutdown) {
            pthread_mutex_unlock(&(threadPool->lock));
            pthread_exit(NULL);
        }

        // Dequeue and execute task
        struct TaskArguments *task_argument = threadPool->tasks[threadPool->task_queue_front].argument;
        threadPool->task_queue_front = (threadPool->task_queue_front + 1) % threadPool->task_queue_size;

        pthread_mutex_unlock(&(threadPool->lock));

        threadPool->tasks[threadPool->task_queue_front].function(task_argument);
        free(task_argument); // Free allocated memory for the argument
    }

    return NULL;
}

void shutdownThreadPool(ThreadPool *threadPool) {
    pthread_mutex_lock(&(threadPool->lock));
    threadPool->shutdown = true;
    pthread_mutex_unlock(&(threadPool->lock));

    pthread_cond_broadcast(&(threadPool->not_empty));

    for (int i = 0; i < threadPool->pool_size; ++i) {
        pthread_join(threadPool->threads[i], NULL);
    }

    free(threadPool->threads);
    free(threadPool->tasks);
    pthread_mutex_destroy(&(threadPool->lock));
    pthread_cond_destroy(&(threadPool->not_empty));
}


int main(int argc, char *argv[])
{   
    printf("Running");
    int portnumber;
    char *root;
    int sockfd;
    int sockfd_current;
    struct sockaddr_in sockIn;
    struct sockaddr_in portIn;
    int addrlen;
    char ipaddr[INET_ADDRSTRLEN];

    ThreadPool threadpool;

    initializeThreadPool(&threadpool, THREAD_POOL_SIZE, SIZE);

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <root_directory>\n", argv[0]);
        exit(1);
    }

    portnumber = atoi(argv[1]);
    root = argv[2];

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)  
    {   
        perror ("socket");
        exit(1);
    }

    memset(&sockIn, 0, sizeof(sockIn)); 
    sockIn.sin_family= AF_INET;
    sockIn.sin_addr.s_addr =INADDR_ANY;
    sockIn.sin_port = htons(portnumber); 

    if(bind(sockfd, (struct sockaddr *) &sockIn, sizeof(sockIn)) == -1)
    {
        perror("bind");
        exit(1);
    }

    if(listen(sockfd, 10) == -1)  
    {
        perror("listen");
        exit(1);
    }
    addrlen = sizeof(portIn);

    for (int i = 0; i < THREAD_POOL_SIZE; ++i) {
        if (pthread_create(&threadpool.threads[i], NULL, worker, (void *)&threadpool) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }


    while(1){

        if((sockfd_current = accept(sockfd, (struct sockaddr*) &portIn, (socklen_t*) &addrlen)) == -1)  //trying to Create a new socket for the accepted client
        {
            perror("accept");
            exit(1);
        }

        struct TaskArguments *stuff = malloc(sizeof(struct TaskArguments));
        stuff->root = root;
        stuff->sockfd_current = sockfd_current;
        stuff->portIn = portIn;
        strcpy(stuff->ipAddress, ipaddr);

        submitTask(&threadpool, handleConnection, stuff);
    
    }

    shutdownThreadPool(&threadpool);
    close(sockfd);
    return 0;
}

//     pthread_create()
//         Creates a new thread.
//         Syntax: int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
//         Parameters:
//             thread: Pointer to a pthread_t variable where the thread ID will be stored.
//             attr: Thread attributes (usually NULL for default attributes).
//             start_routine: Function the thread will execute.
//             arg: Argument passed to the start_routine.
//         Returns 0 on success, otherwise an error number.

//     pthread_join()
//         Waits for a thread to terminate.
//         Syntax: int pthread_join(pthread_t thread, void **retval);
//         Parameters:
//             thread: Thread ID of the thread to wait for.
//             retval: Pointer to store the exit status of the joined thread.
//         Returns 0 on success, otherwise an error number.

//     pthread_detach()
//         Marks a thread as detached (thread resources are automatically released when the thread terminates).
//         Syntax: int pthread_detach(pthread_t thread);
//         Parameters:
//             thread: Thread ID of the thread to detach.
//         Returns 0 on success, otherwise an error number.

//     pthread_exit()
//         Terminates the calling thread.
//         Syntax: void pthread_exit(void *retval);
//         Parameter:
//             retval: Exit status of the thread.

// Synchronization and Mutual Exclusion

//     pthread_mutex_init()
//         Initializes a mutex.
//         Syntax: int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
//         Parameters:
//             mutex: Pointer to the mutex object to be initialized.
//             attr: Mutex attributes (usually NULL for default attributes).
//         Returns 0 on success, otherwise an error number.

//     pthread_mutex_lock()
//         Locks a mutex, waits if the mutex is already locked by another thread.
//         Syntax: int pthread_mutex_lock(pthread_mutex_t *mutex);
//         Parameter:
//             mutex: Mutex to be locked.
//         Returns 0 on success, otherwise an error number.

//     pthread_mutex_unlock()
//         Unlocks a mutex.
//         Syntax: int pthread_mutex_unlock(pthread_mutex_t *mutex);
//         Parameter:
//             mutex: Mutex to be unlocked.
//         Returns 0 on success, otherwise an error number.

//     pthread_mutex_destroy()
//         Destroys a mutex, freeing associated resources.
//         Syntax: int pthread_mutex_destroy(pthread_mutex_t *mutex);
//         Parameter:
//             mutex: Mutex to be destroyed.
//         Returns 0 on success, otherwise an error number.

// Condition Variables

//     pthread_cond_init()
//         Initializes a condition variable.
//         Syntax: int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
//         Parameters:
//             cond: Pointer to the condition variable object to be initialized.
//             attr: Condition variable attributes (usually NULL for default attributes).
//         Returns 0 on success, otherwise an error number.

//     pthread_cond_wait()
//         Waits on a condition variable, atomically releases the associated mutex and waits for a signal.
//         Syntax: int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
//         Parameters:
//             cond: Condition variable to wait on.
//             mutex: Associated mutex to be unlocked while waiting.
//         Returns 0 on success, otherwise an error number.

//     pthread_cond_signal()
//         Signals one thread waiting on a condition variable.
//         Syntax: int pthread_cond_signal(pthread_cond_t *cond);
//         Parameter:
//             cond: Condition variable to signal.
//         Returns 0 on success, otherwise an error number.

//     pthread_cond_broadcast()
//         Signals all threads waiting on a condition variable.
//         Syntax: int pthread_cond_broadcast(pthread_cond_t *cond);
//         Parameter:
//             cond: Condition variable to broadcast.
//         Returns 0 on success, otherwise an error number.

//     pthread_cond_destroy()
//         Destroys a condition variable, freeing associated resources.
//         Syntax: int pthread_cond_destroy(pthread_cond_t *cond);
//         Parameter:
//             cond: Condition variable to be destroyed.
//         Returns 0 on success, otherwise an error number.