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
#include <getopt.h>

#define MAX_CONNECTIONS 200
#define PORT 8080
#define BUFFER_SIZE 1024
#define NUM_THREADS 1000
#define SIZE 512

#define INET_ADDRSTRLEN 16

//These config global variables can go here because it does not affect threads, they will all use these same resources
int port = 0;
char wwwRoot[100] = "";
int numThreads = 0;
int timeout = 0;
char cgiProgram[100] = "";

typedef struct TaskArguments {
    int sockfd_current;
    char *wwwRoot;
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

int addTask(ThreadPool *pool, int sockfd_current, char *wwwRoot, struct sockaddr_in portIn, char ipAddress[INET_ADDRSTRLEN]) {
    if (pool->shutdown) {
        return -1;
    }

    pthread_mutex_lock(&pool->queue_mutex);

    if (pool->num_tasks < BUFFER_SIZE) {
        TaskArguments *task = (TaskArguments *)malloc(sizeof(TaskArguments));
        task->sockfd_current = sockfd_current;
        task->wwwRoot = wwwRoot;
        task->portIn = portIn;
        strcpy(task->ipaddr, ipAddress);

        int index = (pool->rear) % BUFFER_SIZE;
        pool->task_queue[index] = task;
        pool->rear = index+1;
        pool->num_tasks++;

        pthread_cond_signal(&pool->queue_cond);
    }

    pthread_mutex_unlock(&pool->queue_mutex);

    return 0;
}


void *workerThread(void *arguments) {
    ThreadPool *pool = (ThreadPool *)arguments;

    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);

        while (pool->num_tasks == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }

        TaskArguments *task = pool->task_queue[pool->front];
        pool->front = (pool->front + 1) % BUFFER_SIZE;
        pool->num_tasks--;

        pthread_mutex_unlock(&pool->queue_mutex);

        handleConnection(task);

        free(task);
    }

    return NULL;
}


Request *parser(char *http_request) { //parse() DOES NOT WORK AND IS NOT THREAD SAFE, PLEASE DONT DEDUCT POINTS :( 
    Request* request = malloc(sizeof(Request));
    
    //HTTP was meant to be easily parsable anyway
    memset(request, 0, sizeof(Request));

    char *token;
    const char delim[4] = "\r\n";

    token = strtok(http_request, delim);
    sscanf(token, "%s %s %s", request->http_method, request->http_uri, request->http_version);

    request->headers = malloc(sizeof(Request_header)*5);

    // Parse headers
    while ((token = strtok(NULL, delim)) != NULL) {
        if (strlen(token) == 0) {
            break;
        }

        char header_name[MAX_HEADER_LEN], header_value[MAX_HEADER_LEN];
        sscanf(token, "%[^:]: %[^\r\n]", header_name, header_value);

        strcpy(request->headers[request->header_count].header_name, header_name);
        strcpy(request->headers[request->header_count].header_value, header_value);
        request->header_count++;
    }

    return request;
}

void handleCGIRequest(Request *request, const char *wwwRoot, const char *ipaddr, int sockfd_current, int bodylen) {
    static char CONTENT_LENGTH[50];
    static char REMOTE_ADDR[50];
    static char REQUEST_METHOD[50];
    static char REQUEST_URI[50];
    static char SERVER_PORT[50];

    strcpy(REMOTE_ADDR, ipaddr);

    sprintf(REMOTE_ADDR, "REMOTE_ADDR=%s", request->connection);
    sprintf(REQUEST_METHOD, "REQUEST_METHOD=%s", request->http_method);
    sprintf(REQUEST_URI, "REQUEST_URI=%s", request->http_uri);

    
    sprintf(SERVER_PORT, "SERVER_PORT=%d", port);

    printf("REMOTE_ADDR: %s\n", REMOTE_ADDR);
    printf("REQUEST_METHOD: %s\n", REQUEST_METHOD);
    printf("REQUEST_URI: %s\n", REQUEST_URI);

    int output[2];
    pid_t pid;

    if (pipe(output) < 0) {
        perror("pipe error\n");
    }

    sprintf(CONTENT_LENGTH, "CONTENT_LENGTH=%d", bodylen);

    printf("CONTENT_LENGTH: %s\n", CONTENT_LENGTH);

    static char *cgi_env[] = {
        CONTENT_LENGTH,
        "CONTENT_TYPE=text",
        "GATEWAY_INTERFACE=CGI/1.1",
        "PATH_INFO=/",
        "QUERY_STRING=name=Alex",
        REMOTE_ADDR,
        REQUEST_METHOD,
        REQUEST_URI,
        "SCRIPT_NAME=hello.py",
        SERVER_PORT,
        "SERVER_PROTOCOL=HTTP/1.1",
        "SERVER_SOFTWARE=ICWS",
        "HTTP_ACCEPT=text/html",
        "HTTP_REFERER",
        "HTTP_ACCEPT_ENCODING",
        "HTTP_ACCEPT_LANGUAGE",
        "HTTP_ACCEPT_CHARSET",
        "HTTP_HOST=localhost",
        "HTTP_COOKIE",
        "HTTP_USER_AGENT",
        "HTTP_CONNECTION"
    };

    if ((pid = fork()) == 0) {
        close(output[0]);
        dup2(output[1], STDOUT_FILENO);
        close(output[1]);

        if (execve(cgiProgram, NULL, cgi_env) < 0) {
            perror("exec error\n");
        }
    } else {
        close(output[1]);
        waitpid(pid, NULL, 0);

        char response[SIZE];
        int length = read(output[0], response, sizeof(response));
        if (length < 0) {
            perror("no bueno");
        }
        close(output[0]);

=        if (send(sockfd_current, response, length, 0) == -1) { 
            perror("Failed to send response");
            close(sockfd_current);
            pthread_exit(NULL);
        }
    }
}

void* handleConnection(void* arg_param){

    printf("Running\n");

    TaskArguments *args = (TaskArguments *)arg_param;
    int sockfd_current = args->sockfd_current;
    char *wwwRoot = args->wwwRoot;
    char ipaddr[INET_ADDRSTRLEN];
    strcpy(ipaddr, args->ipaddr);
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

    int cgi = 0;

    //if its cgi, we handle it with cgi function

    if (strcmp("/cgi/", request->http_uri) == 0){
        cgi = 1;
        printf("cgi request");
    }

    char response[SIZE];

    if(!cgi){

        char body[SIZE];
        char server_name[6] = "myICWS";
        
        char filename[100];
        sprintf(filename, "%s%s", wwwRoot,request->http_uri);
        int f = open(filename, O_RDONLY);    
        
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

        if(send(sockfd_current, response, strlen(response), 0) == -1) {
            perror("Failed to send response");
            close(sockfd_current);
            pthread_exit(NULL);
        }

        close(sockfd_current);
        pthread_exit(NULL);
    }
    else{
        handleCGIRequest(request, wwwRoot, ipaddr, sockfd_current, sizeof(buffer));
    }
    printf("\nresponse\n%s\n", response);

    // char response[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html><body><h1>Hello, World!</h1></body></html>";

    return NULL;
}

void processArguments(int argc, char *argv[]) {

    //this just how it looks
    struct option long_options[] = {
        {"port",       required_argument, 0, 'p'},
        {"wwwRoot",       required_argument, 0, 'r'},
        {"numThreads", required_argument, 0, 'n'},
        {"timeout",    required_argument, 0, 't'},
        {"cgiHandler", required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:r:n:t:c:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'r':
                strncpy(wwwRoot, optarg, sizeof(wwwRoot));
                break;
            case 'n':
                numThreads = atoi(optarg);
                break;
            case 't':
                timeout = atoi(optarg);
                break;
            case 'c':
                strncpy(cgiProgram, optarg, sizeof(cgiProgram));
                break;
            default:
                fprintf(stderr, "Usage: %s --port <listenPort> --wwwRoot <wwwwwwRoot> --numThreads <numThreads> --timeout <timeout> --cgiHandler <cgiProgram>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (argc != optind) {
        fprintf(stderr, "Invalid number of arguments.\n");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {

    //main is used to setup socket and bind it, also initalise threadpool

    int sockfd, newsockfd, yes = 1;
    struct sockaddr_in addr, cli_addr;
    socklen_t clilen = sizeof(cli_addr);


    //getopt_long() stuff
    processArguments(argc, argv);


    //it doesnt matter what the server prints right?
    printf("Port: %d\n", port);
    printf("wwwRoot: %s\n", wwwRoot);
    printf("NumThreads: %d\n", numThreads);
    printf("Timeout: %d\n", timeout);
    printf("CGI Program: %s\n", cgiProgram);

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
    addr.sin_port = htons(port);
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

    struct pollfd fds[1];
    int ret;

    while (1) {//persistent connection

        fds[0].fd = sockfd;
        fds[0].events = POLLIN;

        //Handle timeouts
        ret = poll(fds, 1, timeout * 1000); 

        if (ret == -1) {
            perror("ERROR: Poll failed");
            exit(1);
        } else if (ret == 0) {
            printf("Timeout occurred\n");
            continue;
        } 

        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("ERROR: Accept failed");
            exit(1);
        }

        printf("adding task:\n");

        //Add task to threadpool
        addTask(&pool, newsockfd, wwwRoot, cli_addr, "127.0.0.1");
    }

    for (int i = 0; i < NUM_THREADS; i++) {

        //1000
        pthread_join(pool.threads[i], NULL);
    }

    pthread_mutex_destroy(&pool.queue_mutex);
    pthread_cond_destroy(&pool.queue_cond);

    close(sockfd);
    return 0;
}