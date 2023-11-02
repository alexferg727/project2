#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "parse.h"

int main(int argc, char **argv) {
    // Read from the file the sample
    int fd_in = open(argv[1], O_RDONLY);
    int index;
    char buf[8192];
    if (fd_in < 0) {
        printf("Failed to open the file\n");
        return 0;
    }
    int readRet = read(fd_in, buf, 8192);

    // Parse the buffer to the parse function.
    Request *request = parse(buf, readRet, fd_in);

    if (request) {
        // Just printing everything
        printf("Http Method %s\n", request->http_method);
        printf("Http Version %s\n", request->http_version);
        printf("Http Uri %s\n", request->http_uri);
        for (index = 0; index < request->header_count; index++) {
            printf("Request Header\n");
            printf("Header name %s Header Value %s\n", request->headers[index].header_name, request->headers[index].header_value);
        }

    } else {
        printf("Parsing Failed\n");
    }

    return 0;
}
