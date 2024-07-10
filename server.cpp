#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<string.h>
#include<errno.h>
#include<unistd.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/ip.h>

static void msg(const char* msg){
fprintf(stderr,"%s\n",msg);
}

static void die(const char* msg){
    int err=errno;
    fprintf(stderr,"[%d] %s",err,msg);
    abort();
}

static void do_something(int connfd){
    char rbuff[64];
    ssize_t n=read(connfd,rbuff,sizeof(rbuff)-1);
    
    if(n<0){
        msg("read() error");
        return;
    }

    printf("client says:%s\n",rbuff);

    char wbuff[]="World";
    write(connfd,wbuff,sizeof(wbuff));
}

int main(){
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0) die("socket()");

    int val=1;
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&val,sizeof(val));

    struct sockaddr_in addr={};
    addr.sin_family=AF_INET;
    addr.sin_port=ntohs(1234);
    addr.sin_addr.s_addr=ntohl(0);

    int rv=bind(fd,(const sockaddr *)&addr,sizeof(addr));
    if(rv<0) die("bind()");

    rv = listen(fd,SOMAXCONN);
    if(rv<0) die("listen()");

    while(true){
        struct sockaddr_in client={};
        socklen_t socklen=sizeof(client);

        int connfd=accept(fd,(struct sockaddr*)&client,&socklen);
        if(connfd<0)continue;
        do_something(connfd);
        close(connfd);
    }
    return 0;
}