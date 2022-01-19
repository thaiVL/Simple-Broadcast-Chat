#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

#define RECPORT "35123"
#define SENPORT "36000"
#define MSGLEN 2048

#define BACKLOG 20

char* buff;

int rc;             // receiver clients count
int rcServed;       // number of receiver clients who had their messages received already
pthread_mutex_t rcLock;      // lock for rc
int turn;

// pthread_mutex_t senLock;
pthread_mutex_t writeLock;

pthread_cond_t hasMsg;      // wake up receiver client threads

typedef struct info{
    int socket;
    struct sockaddr_storage peeraddr;
} peer;

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void* sendClient(void* info) {
    int sendSock = ((peer*) info)->socket;
    struct sockaddr_storage clientaddr = ((peer*) info)->peeraddr;
    free(info);

    char senderIP[INET6_ADDRSTRLEN];
    char senderPort[6];

    inet_ntop(clientaddr.ss_family,
        get_in_addr((struct sockaddr*)&clientaddr),
        senderIP, sizeof senderIP);
    
    sprintf(senderPort, "%d", ntohs(((struct sockaddr_in*)&clientaddr)->sin_port));

    char temp[MSGLEN];
    while(1) {
        int bytes = recv(sendSock, temp, MSGLEN-1, 0);
        if(bytes == 0){
            close(sendSock);
            fprintf(stderr, "Sender client %s %s disconnected %s\n", senderIP, senderPort, strerror(errno));
            return NULL;
        }
        
        pthread_mutex_lock(&writeLock);
        strcpy(buff, senderIP);
        strcat(buff, ", ");
        strcat(buff, senderPort);
        strcat(buff, ": ");
        strcat(buff, temp);
        // strcpy(buff, temp);
        
        turn += 1;
        // printf("sendC turn %d\n", turn);
        // if everyone is not served yet, keep sending messages to everyone
        while(rcServed != rc){
            // spin so no one can access this lock and overwrite buffer
            pthread_cond_signal(&hasMsg);
        }
        rcServed = 0;   // done serving all receiver clients, reset rcServed back to 0
        memset(buff, 0, strlen(buff));
        memset(temp, 0, strlen(temp));
        pthread_mutex_unlock(&writeLock);

        
    }
    close(sendSock);
    return NULL;
}

void* senderCon(){
    printf("In senderCon.\n");
    int serverSock, clientSock;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    char host[INET6_ADDRSTRLEN];
    int sn;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((sn = getaddrinfo(NULL, SENPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "send getaddrinfo: %s\n", gai_strerror(sn));
        // return 1;
        pthread_exit(NULL);
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((serverSock = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("sender server: socket");
            continue;
        }

        if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("sender setsockopt");
            pthread_exit(NULL);
        }

        if (bind(serverSock, p->ai_addr, p->ai_addrlen) == -1) {
            close(serverSock);
            perror("sender server: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        pthread_exit(NULL);
    }

    if(listen(serverSock, BACKLOG) == -1){
        perror("listen");
        pthread_exit(NULL);
    }

    gethostname(host, INET6_ADDRSTRLEN);
    printf("sender server host: %s waiting for sender connections...\n", host);


    while(1){
        sin_size = sizeof their_addr;
        clientSock = accept(serverSock, (struct sockaddr*)&their_addr, &sin_size);
        if(clientSock == -1){
            perror("sender accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got sender connection from %s\n", s);

        // handles connection 
        peer* peerInfo = (peer*) malloc(sizeof(peer));
        peerInfo->socket = clientSock;
        peerInfo->peeraddr = their_addr;
        // int* clientSockT = (int*) malloc(sizeof(int));
        // *clientSockT = clientSock;

        pthread_t t;
        pthread_create(&t, NULL, sendClient, peerInfo);
        pthread_detach(t);

    }
}



void* recvClient(void* clientSock){
    // recvClient asleep until buffer is filled, then wake up and send
    int recvSock = *((int*)clientSock);
    free(clientSock);
    int myturn = turn;
    while(1){
        // int sent = 0; // 0 if not msg sent, 1 if msg sent 
        pthread_mutex_lock(&rcLock);

        // if everyone is fully served or I have already sent my msg, then wait
        while(rcServed == rc || myturn == turn){
            pthread_cond_wait(&hasMsg, &rcLock);
        }
        int valid = send(recvSock, buff, strlen(buff), MSG_NOSIGNAL);
        if( valid == -1 && errno == EPIPE){
            // perror("receiver send");
            printf("A receiver client has disconnected\n");
            rc -= 1;
            pthread_mutex_unlock(&rcLock);
            close(recvSock);
            return NULL;
        }
        if( valid == -1){
            perror("receiver send");
            rc -= 1;
            pthread_mutex_unlock(&rcLock);
            close(recvSock);
            return NULL;
        }
        rcServed += 1;
        myturn += 1;
        // printf("rcServed %d rc %d myturn %d\n", rcServed, rc, myturn);
        pthread_mutex_unlock(&rcLock);
    }
    pthread_mutex_lock(&rcLock);
    rc -= 1;
    pthread_mutex_unlock(&rcLock);

    close(recvSock);
    return NULL;

}

void* receiverCon(){
    int serverSock, clientSock;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    char host[INET6_ADDRSTRLEN];
    int rv;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, RECPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "recv getaddrinfo: %s\n", gai_strerror(rv));
        pthread_exit(NULL);
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((serverSock = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("receiver server: socket");
            continue;
        }

        if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("receiver setsockopt");
            pthread_exit(NULL);
        }

        if (bind(serverSock, p->ai_addr, p->ai_addrlen) == -1) {
            close(serverSock);
            perror("receiver server: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        pthread_exit(NULL);
    }

    if(listen(serverSock, BACKLOG) == -1){
        perror("listen");
        pthread_exit(NULL);
    }

    gethostname(host, INET6_ADDRSTRLEN);
    printf("receiver server host: %s waiting for receiver connections...\n", host);

    while(1){
        sin_size = sizeof their_addr;
        clientSock = accept(serverSock, (struct sockaddr*)&their_addr, &sin_size);
        if(clientSock == -1){
            perror("receiver accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got receiver connection from %s\n", s);

        // handles connection 
        int* clientSockT = (int*) malloc(sizeof(int));
        *clientSockT = clientSock;

        pthread_t t;
        if(pthread_create(&t, NULL, recvClient, clientSockT) != 0){
            perror("recv connection handler thread creation err:");
            continue;
        }
        rc+=1;
        // printf("%d\n", rc);
        
        pthread_detach(t);

    }
    
    close(serverSock);


}


int main(void){
    rc = 0;
    turn = 0;
    buff = (char*) malloc(sizeof(char)*MSGLEN);

    pthread_mutex_init(&writeLock, NULL);
    pthread_mutex_init(&rcLock, NULL);
    pthread_cond_init(&hasMsg, NULL);

    pthread_t recCon, senCon;

    pthread_create(&recCon, NULL, receiverCon, NULL);
    pthread_create(&senCon, NULL, senderCon, NULL);

    pthread_join(recCon, NULL);
    pthread_join(senCon, NULL);

    pthread_mutex_destroy(&writeLock);
    pthread_mutex_destroy(&rcLock);
    pthread_cond_destroy(&hasMsg);
}