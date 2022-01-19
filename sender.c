#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#define PORT "36000"
#define MAXDATASIZE 2048

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void sendMsg(int sockfd) {
    char* msg = NULL;
    size_t len = 0;
    for(;;) {
        printf("Send message: ");
        if(getline(&msg, &len, stdin) == -1){
            perror("getline");
            close(sockfd);
            free(msg);
            return;
        }
        if(strlen(msg) > MAXDATASIZE){
            printf("Your message is too long! Only 2048 characters allowed!\n");
            memset(msg, 0, MAXDATASIZE);
            continue;
        }
        int valid = send(sockfd, msg, strlen(msg), MSG_NOSIGNAL);
        if( valid == -1 && errno == EPIPE){
            printf("Connection to server lost\n");
            free(msg);
            close(sockfd);
            return;
        }
        if( valid == -1){
            perror("sendMsg");
            free(msg);
            close(sockfd);
            return;
        }
        // printf("msg sent: %s\n", msg);
        // bzero(&msg, sizeof(msg));
        memset(msg, 0, MAXDATASIZE);
    }
    free(msg);
    close(sockfd);
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    if (argc != 2) {
        fprintf(stderr,"usage: client hostname\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("sender client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("sender client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "sender client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
    printf("sender client: connecting to %s\n", s);

    //Sending message - loop to ask client for a message
    sendMsg(sockfd);

    freeaddrinfo(servinfo); // all done with this structure

}
