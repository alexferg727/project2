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

#define SIZE 512

char* copy(char *dest, char *source){

    strcpy(dest, source);
    return dest+strlen(source);
}

int main(int argc, char *argv[])
{   
    printf("Running");
    int portnumber;
    char *root;
    int sockfd; //for new client connections
    int sockfd_current; //for accepted clients
    struct sockaddr_in sockIn;
    struct sockaddr_in portIn;
    char buffert[SIZE];
    int addrlen;
    char ipAddress[INET_ADDRSTRLEN]; //incomming IP-address to server with length


    if (argc < 3) {
    fprintf(stderr, "Usage: %s <port> <root_directory>\n", argv[0]);
    exit(1);
}

    portnumber = atoi(argv[1]);  // Convert the string to an integer
    root = argv[2];


    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)  //trying to create socket address-family
    {   
        perror ("socket");
        exit(1);
    }

    memset(&sockIn, 0, sizeof(sockIn)); //assign memory and set socket address structure
    sockIn.sin_family= AF_INET;
    sockIn.sin_addr.s_addr =INADDR_ANY;
    sockIn.sin_port = htons(portnumber); //assign port to network byteorder: hostToNetwork

    if(bind(sockfd, (struct sockaddr *) &sockIn, sizeof(sockIn)) == -1)//trying to assign address to socket
    {
        perror("bind");
        exit(1);
    }

    if(listen(sockfd, 10) == -1)    //trying to Listen for clients that fulfills the requirements  
    {
        perror("listen");
        exit(1);
    }
    addrlen = sizeof(portIn);

    while(1){

        if((sockfd_current = accept(sockfd, (struct sockaddr*) &portIn, (socklen_t*) &addrlen)) == -1)  //trying to Create a new socket for the accepted client
        {
            perror("accept");
            exit(1);
        }
    

    // pthread_t thread_id;
    // pthread_create(&thread_id, NULL, handle_client, (void *)client_fd);
    // pthread_deatch(thread_id);

//Start communication aka HEAD/GET from client...
        printf("Accepting connection...\n\n");
        if(recv(sockfd_current, buffert, sizeof(buffert), 0) == -1) //trying to recive message from client
        {
            perror("Failed to recive request from client");
            exit(1);
        }

        inet_ntop(AF_INET, &portIn.sin_addr, ipAddress, sizeof(ipAddress)); //convert binary-ip from client to "networkToPresentable" string
        printf("Request from %s:%i\n", ipAddress, ntohs(portIn.sin_port));
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
        
        char filename[100];
        sprintf(filename, "%s%s", root,request->http_uri);
        int f = open(filename, O_RDONLY);    
        char response[SIZE];
        if (f < 0){
            sprintf(response, "HTTP/1.1 404 Not Found\r\n");

        }
        else{
            int bodylen = read(f, body, sizeof(body));
            sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: text/html\r\n\r\n%s", bodylen, body);
        }
        

        printf("\nresponse\n%s\n", response);

        if(send(sockfd_current, response, strlen(response) + 1, 0) == -1) 
        {
            perror("send");
            exit(1);
        }

        close(f);
        close(sockfd_current);
    }


    close(sockfd);
    return 0;
}