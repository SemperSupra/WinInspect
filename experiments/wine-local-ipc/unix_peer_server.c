#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s SOCKET_PATH ALLOWED_UID\n", argv[0]);
        return 2;
    }
    const char *path = argv[1];
    char *end = NULL;
    unsigned long allowed = strtoul(argv[2], &end, 10);
    if (!end || *end != '\0' || allowed > 0xffffffffUL) return 3;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 4; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "socket path too long\n");
        close(fd); return 5;
    }
    strcpy(addr.sun_path, path);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return 6;
    }
    if (chown(path, (uid_t)allowed, (gid_t)-1) < 0) {
        perror("chown"); unlink(path); close(fd); return 7;
    }
    if (chmod(path, 0600) < 0) {
        perror("chmod"); unlink(path); close(fd); return 8;
    }
    if (listen(fd, 1) < 0) {
        perror("listen"); unlink(path); close(fd); return 9;
    }

    printf("READY path=%s allowed_uid=%lu\n", path, allowed);
    fflush(stdout);

    int client = accept(fd, NULL, NULL);
    if (client < 0) { perror("accept"); unlink(path); close(fd); return 10; }

    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
        perror("SO_PEERCRED"); close(client); unlink(path); close(fd); return 11;
    }
    printf("PEER pid=%ld uid=%ld gid=%ld\n", (long)cred.pid, (long)cred.uid, (long)cred.gid);
    fflush(stdout);

    if ((unsigned long)cred.uid != allowed) {
        (void)write(client, "ERR peer\n", 9);
        close(client); unlink(path); close(fd); return 12;
    }

    char buf[16] = {0};
    ssize_t n = read(client, buf, sizeof(buf)-1);
    if (n <= 0) { perror("read"); close(client); unlink(path); close(fd); return 13; }
    if (strcmp(buf, "PING\n") != 0) {
        (void)write(client, "ERR command\n", 12);
        close(client); unlink(path); close(fd); return 14;
    }
    if (write(client, "OK\n", 3) != 3) {
        perror("write"); close(client); unlink(path); close(fd); return 15;
    }

    close(client);
    unlink(path);
    close(fd);
    return 0;
}
