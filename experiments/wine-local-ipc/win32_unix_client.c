#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#define WININSPECT_UNIX_PATH_MAX 108

typedef struct _WININSPECT_SOCKADDR_UN {
    ADDRESS_FAMILY sun_family;
    char sun_path[WININSPECT_UNIX_PATH_MAX];
} WININSPECT_SOCKADDR_UN;

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s WINDOWS_SOCKET_PATH\n", argv[0]);
        return 2;
    }
    if (strlen(argv[1]) >= WININSPECT_UNIX_PATH_MAX) return 3;

    WSADATA data;
    if (WSAStartup(MAKEWORD(2,2), &data) != 0) return 4;

    SOCKET s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
        WSACleanup(); return 5;
    }

    WININSPECT_SOCKADDR_UN addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, argv[1]);

    if (connect(s, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "connect failed: %d\n", WSAGetLastError());
        closesocket(s); WSACleanup(); return 6;
    }

    if (send(s, "PING\n", 5, 0) != 5) {
        fprintf(stderr, "send failed: %d\n", WSAGetLastError());
        closesocket(s); WSACleanup(); return 7;
    }

    char reply[16] = {0};
    int n = recv(s, reply, sizeof(reply)-1, 0);
    if (n <= 0) {
        fprintf(stderr, "recv failed: %d\n", WSAGetLastError());
        closesocket(s); WSACleanup(); return 8;
    }
    printf("reply=%s", reply);
    int ok = strcmp(reply, "OK\n") == 0;
    closesocket(s);
    WSACleanup();
    return ok ? 0 : 9;
}
