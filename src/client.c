#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>

#define SIZE 512
#define NUM_REQUESTS 20

void *sendRequests(void *arg) {
    char *server_ip = (char *)arg;
    int port = 80; // Replace with your server's port number

    for (int i = 0; i < NUM_REQUESTS; ++i) {
        int sockfd;
        struct sockaddr_in server_addr;
        struct hostent *server;

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("ERROR opening socket");
            exit(1);
        }

        server = gethostbyname(server_ip);
        if (server == NULL) {
            fprintf(stderr, "ERROR, no such host\n");
            exit(1);
        }

        memset((char *)&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        memcpy((char *)&server_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
        server_addr.sin_port = htons(port);

        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("ERROR connecting");
            exit(1);
        }

        char request[SIZE];
        snprintf(request, SIZE, "GET / HTTP/1.1\r\nHost: %s\r\n\r\n", server_ip);

        if (send(sockfd, request, strlen(request), 0) < 0) {
            perror("ERROR sending request");
            exit(1);
        }

        char response[SIZE];
        int bytes_received = recv(sockfd, response, SIZE - 1, 0);
        if (bytes_received < 0) {
            perror("ERROR receiving response");
            exit(1);
        }

        response[bytes_received] = '\0';
        printf("Response %d received:\n%s\n\n", i + 1, response);

        close(sockfd);
    }

    return NULL;
}

int main() {
    char *server_ip = "127.0.0.1"; // Set the server IP to localhost

    pthread_t thread_id;

    if (pthread_create(&thread_id, NULL, sendRequests, (void *)server_ip) != 0) {
        perror("pthread_create");
        exit(1);
    }

    pthread_join(thread_id, NULL);

    return 0;
}

// #include <stdio.h>
// #include <stdlib.h>
// #include <unistd.h>


// int main() {
//     int i;
//     for (i = 0; i < 20; i++) {
//         sleep(1);
//         system("curl localhost");
//     }
//     return 0;
// }

