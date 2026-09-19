#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int send_all(SOCKET s,const char *p,size_t n) {
    while(n) { int rc=send(s,p,(int)n,0); if(rc<=0)return -1; p+=rc; n-=(size_t)rc; }
    return 0;
}
static int read_line(SOCKET s,char *buf,size_t cap) {
    size_t n=0;
    while(n+1<cap) { char ch; int rc=recv(s,&ch,1,0); if(rc<=0)return -1;
        if(ch=='\n'){buf[n]='\0';return 0;} if(ch!='\r')buf[n++]=ch; }
    return -1;
}
static int command(SOCKET s,const char *cmd,const char *expected) {
    char line[128];
    if(send_all(s,cmd,strlen(cmd))<0 || send_all(s,"\n",1)<0) return -1;
    if(read_line(s,line,sizeof(line))<0) return -1;
    if(strcmp(line,expected)!=0) { fprintf(stderr,"command '%s': expected '%s', got '%s'\n",cmd,expected,line); return -1; }
    return 0;
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    int port=atoi(argv[1]); const char *token=argv[2];
    WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)!=0)return 3;
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); if(s==INVALID_SOCKET)return 4;
    struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons((u_short)port);
    if(InetPtonA(AF_INET,"127.0.0.1",&a.sin_addr)!=1 || connect(s,(struct sockaddr*)&a,sizeof(a))!=0)return 5;

    char auth[192]; snprintf(auth,sizeof(auth),"AUTH %s",token);
    if(command(s,auth,"OK")<0) return 6;

    const struct { const char *cmd; const char *expect; } cases[] = {
        {"K 0 4 0 0 0 0 0","OK"},
        {"K 0 0 0 0 0 0 0","OK"},
        {"K 2 4 0 0 0 0 0","OK"},
        {"K 0 0 0 0 0 0 0","OK"},
        {"K 0 4 5 6 7 8 9","OK"},
        {"K 0 0 0 0 0 0 0","OK"},
        {"M 0 127 -127 0","OK"},
        {"M 7 0 0 0","OK"},
        {"M 0 0 0 0","OK"},
        {"M 0 0 0 127","OK"},
        {"M 0 0 0 -127","OK"},
        {"M 3 17 -11 1","OK"},
        {"M 0 0 0 0","OK"},
        {"K 0 50 0 0 0 0 0","ERR keyboard-usage"},
        {"M 8 0 0 0","ERR mouse-format"},
        {"R","OK"},
        {"K 2 4 0 0 0 0 0","OK"}
    };
    size_t count=sizeof(cases)/sizeof(cases[0]);
    for(size_t i=0;i<count;++i) if(command(s,cases[i].cmd,cases[i].expect)<0) return 10+(int)i;

    printf("PASS subset_cases=%u final_state=held-for-disconnect\n",(unsigned)count);
    closesocket(s);
    WSACleanup();
    return 0;
}
