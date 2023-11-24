#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include "parse.h"

#define MAX_CONNECTIONS 10
#define PORT 8080
#define BUFFER_SIZE 1024
#define NUM_THREADS 10
#define SERVER_PORT 8080
#define SIZE 512

#define INET_ADDRSTRLEN 16


typedef struct TaskArguments {
    int sockfd_current;
    char *root;
    struct sockaddr_in portIn;
    char ipaddr[INET_ADDRSTRLEN];
} TaskArguments;

typedef struct ThreadPool {
    pthread_t *threads;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    TaskArguments *task_queue[BUFFER_SIZE];
    int front;
    int rear;
    int num_tasks;
    int shutdown;
} ThreadPool;

void *handleConnection(void *arguments);

// AddTask function
int addTask(ThreadPool *pool, int sockfd_current, char *root, struct sockaddr_in portIn, char ipAddress[INET_ADDRSTRLEN]) {
    if (pool->shutdown) {
        return -1;
    }

    pthread_mutex_lock(&pool->queue_mutex);

    // Check if the task queue is full before adding a new task
    if (pool->num_tasks < BUFFER_SIZE) {
        TaskArguments *task = (TaskArguments *)malloc(sizeof(TaskArguments));
        task->sockfd_current = sockfd_current;
        task->root = root;
        task->portIn = portIn;
        strcpy(task->ipaddr, ipAddress);

        // Ensure index remains within bounds
        int index = (pool->rear) % BUFFER_SIZE;
        pool->task_queue[index] = task;
        pool->rear = index+1;
        pool->num_tasks++;

        pthread_cond_signal(&pool->queue_cond);
    }

    pthread_mutex_unlock(&pool->queue_mutex);

    return 0;
}


// WorkerThread function
void *workerThread(void *arguments) {
    ThreadPool *pool = (ThreadPool *)arguments;

    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);

        // Check for tasks in the queue
        while (pool->num_tasks == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }

        // Process the task
        TaskArguments *task = pool->task_queue[pool->front];
        pool->front = (pool->front + 1) % BUFFER_SIZE;
        pool->num_tasks--;

        pthread_mutex_unlock(&pool->queue_mutex);

        handleConnection(task);

        free(task); // Free memory allocated for the task
    }

    return NULL;
}


Request *parser(char *http_request) {
    Request* request = malloc(sizeof(Request));
    
    memset(request, 0, sizeof(Request)); // Initialize the struct to 0

    char *token;
    const char delim[4] = "\r\n";
    const char space[2] = " ";

    // Parse the first line for method, URI, and HTTP version
    token = strtok(http_request, delim);
    sscanf(token, "%s %s %s", request->http_method, request->http_uri, request->http_version);

    request->headers = malloc(sizeof(Request_header)*5);

    // Parse headers
    while ((token = strtok(NULL, delim)) != NULL) {
        if (strlen(token) == 0) {
            break; // Empty line, end of headers
        }

        char header_name[MAX_HEADER_LEN], header_value[MAX_HEADER_LEN];
        sscanf(token, "%[^:]: %[^\r\n]", header_name, header_value);

        // Store the header in the request struct


        strcpy(request->headers[request->header_count].header_name, header_name);
        strcpy(request->headers[request->header_count].header_value, header_value);
        request->header_count++;
    }

    return request;
}

void* handleConnection(void* arg_param){

    printf("Running\n");

    TaskArguments *args = (TaskArguments *)arg_param;
    int sockfd_current = args->sockfd_current;
    char *root = args->root;
    char ipaddr[INET_ADDRSTRLEN]; // Assuming ipaddr is an array of characters
    strcpy(ipaddr, args->ipaddr); // Copy the IP address
    struct sockaddr_in portIn = args->portIn;

    printf("Accepting connection...\n\n");

    char buffer[BUFFER_SIZE];
    int bytes_received = recv(sockfd_current, buffer, sizeof(buffer), 0);
    if (bytes_received <= 0) {
        perror("Failed to receive request from client");
        close(sockfd_current);
        pthread_exit(NULL);
    }

    buffer[bytes_received] = '\0';

    printf("point1");

    if (inet_ntop(AF_INET, &portIn.sin_addr, ipaddr, INET_ADDRSTRLEN) == NULL) {
        perror("inet_ntop");
        close(sockfd_current);
        pthread_exit(NULL);
    }

    printf("Request from %s:%i\n", ipaddr, ntohs(portIn.sin_port));
    printf("Message: %s\n", buffer);
    

    // Request *request = parse(buffer, bytes_received, sockfd_current);
    Request *request = parser(buffer);


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

    // char response[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html><body><h1>Hello, World!</h1></body></html>";
    if(send(sockfd_current, response, strlen(response), 0) == -1) {
        perror("Failed to send response");
        close(sockfd_current);
        pthread_exit(NULL);
    }

    close(sockfd_current);
    pthread_exit(NULL);

    return NULL;
}

int main() {
    int sockfd, newsockfd, yes = 1;
    struct sockaddr_in addr, cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR: Socket creation failed");
        exit(1);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("ERROR: setsockopt failed");
        exit(1);
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("ERROR: Bind failed");
        exit(1);
    }

    if (listen(sockfd, MAX_CONNECTIONS) < 0) {
        perror("ERROR: Listen failed");
        exit(1);
    }

    ThreadPool pool;
    pool.front = pool.rear = pool.num_tasks = 0;
    pool.shutdown = 0;
    pool.threads = (pthread_t *)malloc(NUM_THREADS * sizeof(pthread_t));

    pthread_mutex_init(&pool.queue_mutex, NULL);
    pthread_cond_init(&pool.queue_cond, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&pool.threads[i], NULL, workerThread, &pool);
    }

    while (1) {

        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("ERROR: Accept failed");
            exit(1);
        }

        printf("adding task:\n");
        addTask(&pool, newsockfd, "./resources", cli_addr, "127.0.0.1");
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(pool.threads[i], NULL);
    }

    pthread_mutex_destroy(&pool.queue_mutex);
    pthread_cond_destroy(&pool.queue_cond);

    close(sockfd);
    return 0;
}