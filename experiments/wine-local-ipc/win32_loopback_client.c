#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int send_all(SOCKET s, const char *p, size_t n) {
    while (n) {
        int rc = send(s, p, (int)n, 0);
        if (rc <= 0) return -1;
        p += rc; n -= (size_t)rc;
    }
    return 0;
}
static int read_line(SOCKET s, char *buf, size_t cap) {
    size_t n=0;
    while (n+1<cap) {
        char ch; int rc=recv(s,&ch,1,0);
        if (rc<=0) return -1;
        if (ch=='\n') { buf[n]='\0'; return 0; }
        if (ch!='\r') buf[n++]=ch;
    }
    return -1;
}
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr,"usage: %s PORT TOKEN\n",argv[0]); return 2; }
    int port=atoi(argv[1]); const char *token=argv[2];
    if (port < 1 || port > 65535 || !*token) return 3;
    WSADATA data; if (WSAStartup(MAKEWORD(2,2),&data)!=0) return 4;
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (s==INVALID_SOCKET) return 5;
    struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons((u_short)port);
    if (InetPtonA(AF_INET,"127.0.0.1",&a.sin_addr)!=1 || connect(s,(struct sockaddr*)&a,sizeof(a))!=0) {
        fprintf(stderr,"connect failed: %d\n",WSAGetLastError()); return 6;
    }
    char auth[192]; int n=snprintf(auth,sizeof(auth),"AUTH %s\n",token);
    if (n<=0 || (size_t)n>=sizeof(auth) || send_all(s,auth,(size_t)n)<0) return 7;
    char line[64]; if (read_line(s,line,sizeof(line))<0 || strcmp(line,"OK")!=0) return 8;
    if (send_all(s,"SELFTEST\n",9)<0) return 9;
    if (read_line(s,line,sizeof(line))<0 || strcmp(line,"OK")!=0) return 10;
    printf("PASS authenticated loopback + post-drop uinput selftest\n");
    closesocket(s); WSACleanup(); return 0;
}
