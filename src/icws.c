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

#define root '.'

int main(int argc, char *argv[])
{   
    printf("Running");
    int portnumber;
    int sockfd; //for new client connections
    int sockfd_current; //for accepted clients
    struct sockaddr_in sockIn;
    struct sockaddr_in portIn;
    char buffert[SIZE];
    int addrlen;
    char ipAddress[INET_ADDRSTRLEN]; //incomming IP-address to server with length


    portnumber= 8080;  //second parameter passed from main into portnumber


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
    if((sockfd_current = accept(sockfd, (struct sockaddr*) &portIn, (socklen_t*) &addrlen)) == -1)  //trying to Create a new socket for the accepted client
    {
        perror("accept");
        exit(1);
    }

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

    Request *request = parse(buffert, strlen(buffert), sockfd_current);

    printf("Http Method %s\n",request->http_method);
    printf("Http Version %s\n",request->http_version);
    printf("Http Uri %s\n",request->http_uri);

//Responds to clients request
    printf("Send Response:\n\n");

// HTTP/1.1 200 OK
// Content-Type: text/html
// Content-Length: 123
// Server: MyWebServer/1.0

    fgets(buffert, SIZE - 1, stdin);

    if(send(sockfd_current, buffert, strlen(buffert) + 1, 0) == -1) 
    {
        perror("send");
        exit(1);
    }


    close(sockfd_current);
    close(sockfd);
    return 0;
}