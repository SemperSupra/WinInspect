#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int emit_event(int fd, unsigned short type, unsigned short code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type; ev.code = code; ev.value = value;
    return write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev) ? 0 : -1;
}
static int sync_device(int fd) { return emit_event(fd, EV_SYN, SYN_REPORT, 0); }

static int create_keyboard(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, KEY_A) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) goto fail;
    struct uinput_setup s;
    memset(&s, 0, sizeof(s));
    s.id.bustype = BUS_VIRTUAL; s.id.vendor = 0x1209; s.id.product = 0x0266;
    snprintf(s.name, UINPUT_MAX_NAME_SIZE, "%s", "WinInspect-L1-Keyboard");
    if (ioctl(fd, UI_DEV_SETUP, &s) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) goto fail;
    return fd;
fail:
    close(fd); return -1;
}

static int create_mouse(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, BTN_LEFT) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_REL) < 0 ||
        ioctl(fd, UI_SET_RELBIT, REL_X) < 0 ||
        ioctl(fd, UI_SET_RELBIT, REL_WHEEL) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) goto fail;
    struct uinput_setup s;
    memset(&s, 0, sizeof(s));
    s.id.bustype = BUS_VIRTUAL; s.id.vendor = 0x1209; s.id.product = 0x0267;
    snprintf(s.name, UINPUT_MAX_NAME_SIZE, "%s", "WinInspect-L1-Mouse");
    if (ioctl(fd, UI_DEV_SETUP, &s) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) goto fail;
    return fd;
fail:
    close(fd); return -1;
}

static int read_line(int fd, char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char ch;
        ssize_t rc = recv(fd, &ch, 1, 0);
        if (rc == 0) return 0;
        if (rc < 0) { if (errno == EINTR) continue; return -1; }
        if (ch == '\n') { buf[n] = '\0'; return 1; }
        buf[n++] = ch;
    }
    return -2;
}
static int reply(int fd, const char *text) {
    size_t n = strlen(text);
    return send(fd, text, n, MSG_NOSIGNAL) == (ssize_t)n ? 0 : -1;
}

static int selftest_input(int keyboard_fd, int mouse_fd) {
    if (emit_event(keyboard_fd, EV_KEY, KEY_A, 1) < 0 || sync_device(keyboard_fd) < 0) return -1;
    if (emit_event(keyboard_fd, EV_KEY, KEY_A, 0) < 0 || sync_device(keyboard_fd) < 0) return -1;
    if (emit_event(mouse_fd, EV_REL, REL_X, 1) < 0 ||
        emit_event(mouse_fd, EV_KEY, BTN_LEFT, 1) < 0 ||
        emit_event(mouse_fd, EV_KEY, BTN_LEFT, 0) < 0 ||
        emit_event(mouse_fd, EV_REL, REL_WHEEL, 1) < 0 ||
        sync_device(mouse_fd) < 0) return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s UID GID TOKEN\n", argv[0]);
        return 2;
    }
    char *end = NULL;
    unsigned long uid_ul = strtoul(argv[1], &end, 10);
    if (!end || *end) return 3;
    end = NULL;
    unsigned long gid_ul = strtoul(argv[2], &end, 10);
    if (!end || *end) return 4;
    const char *token = argv[3];
    if (!*token || strlen(token) > 128 || strchr(token, '\n') || strchr(token, '\r')) return 5;

    if (geteuid() != 0) {
        fprintf(stderr, "server must start as root for uinput open/create\n");
        return 6;
    }
    int keyboard_fd = create_keyboard();
    int mouse_fd = create_mouse();
    if (keyboard_fd < 0 || mouse_fd < 0) {
        fprintf(stderr, "uinput create failed errno=%d (%s)\n", errno, strerror(errno));
        return 7;
    }

    if (setgroups(0, NULL) < 0 || setgid((gid_t)gid_ul) < 0 || setuid((uid_t)uid_ul) < 0) {
        fprintf(stderr, "privilege drop failed errno=%d (%s)\n", errno, strerror(errno));
        return 8;
    }
    if (geteuid() != (uid_t)uid_ul || getegid() != (gid_t)gid_ul) return 9;

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return 10;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(listener, 1) < 0) return 11;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(listener, (struct sockaddr *)&addr, &addrlen) < 0) return 12;

    printf("READY port=%u euid=%lu egid=%lu pid=%ld\n",
           (unsigned)ntohs(addr.sin_port), (unsigned long)geteuid(),
           (unsigned long)getegid(), (long)getpid());
    fflush(stdout);

    int client = accept(listener, NULL, NULL);
    if (client < 0) return 13;
    char line[256];
    if (read_line(client, line, sizeof(line)) != 1) return 14;
    char expected[160];
    snprintf(expected, sizeof(expected), "AUTH %s", token);
    if (strcmp(line, expected) != 0) {
        (void)reply(client, "ERR auth\n");
        return 15;
    }
    if (reply(client, "OK\n") < 0) return 16;

    if (read_line(client, line, sizeof(line)) != 1 || strcmp(line, "SELFTEST") != 0) return 17;
    if (selftest_input(keyboard_fd, mouse_fd) < 0) {
        (void)reply(client, "ERR input\n");
        return 18;
    }
    if (reply(client, "OK\n") < 0) return 19;

    close(client);
    close(listener);

    /* Release before destroy. */
    (void)emit_event(keyboard_fd, EV_KEY, KEY_A, 0); (void)sync_device(keyboard_fd);
    (void)emit_event(mouse_fd, EV_KEY, BTN_LEFT, 0); (void)sync_device(mouse_fd);
    int mouse_destroy = ioctl(mouse_fd, UI_DEV_DESTROY);
    int keyboard_destroy = ioctl(keyboard_fd, UI_DEV_DESTROY);
    close(mouse_fd); close(keyboard_fd);

    printf("RESULT post_drop_input=1 keyboard_destroy=%d mouse_destroy=%d euid=%lu egid=%lu\n",
           keyboard_destroy, mouse_destroy, (unsigned long)geteuid(), (unsigned long)getegid());
    fflush(stdout);
    return (keyboard_destroy == 0 && mouse_destroy == 0) ? 0 : 20;
}
