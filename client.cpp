#include<stdio.h>
#include<stdint.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<netinet/ip.h>

static void die(char *msg){
int err=errno;
fprintf(stderr,"[%d] %s\n",err,msg);
abort();
}

int main(){

int fd=socket(AF_INET,SOCK_STREAM,0);

if(fd<0) die("socket()");

struct sockaddr_in addr={};
addr.sin_family=AF_INET;
addr.sin_port=ntohs(1234);
addr.sin_addr.s_addr=ntohl(INADDR_LOOPBACK);

int rv=connect(fd,(const struct sockaddr*) &addr,sizeof(addr));
if(rv) die("connect");

char msg[]="Hello";
write(fd,msg,strlen(msg));

char rbuff[64];
ssize_t n=read(fd,rbuff,sizeof(rbuff)-1);

if(n<0) die("read");

rbuff[n]='\0';

printf("server says:%s\n",rbuff);
close(fd);

return 0;
}