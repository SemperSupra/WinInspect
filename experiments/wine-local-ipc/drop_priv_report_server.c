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

static uint8_t g_modifiers;
static uint8_t g_keys[6];
static uint8_t g_buttons;

static int emit_event(int fd, unsigned short type, unsigned short code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type; ev.code = code; ev.value = value;
    return write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev) ? 0 : -1;
}
static int sync_device(int fd) { return emit_event(fd, EV_SYN, SYN_REPORT, 0); }

static int usage_to_code(unsigned int usage) {
    switch (usage) {
        case 0x04: return KEY_A; case 0x05: return KEY_B; case 0x06: return KEY_C;
        case 0x07: return KEY_D; case 0x08: return KEY_E; case 0x09: return KEY_F;
        default: return -1;
    }
}

static const int modifier_codes[8] = {
    KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_LEFTALT, KEY_LEFTMETA,
    KEY_RIGHTCTRL, KEY_RIGHTSHIFT, KEY_RIGHTALT, KEY_RIGHTMETA
};

static bool contains_usage(const uint8_t keys[6], uint8_t usage) {
    for (int i = 0; i < 6; ++i) if (keys[i] == usage) return true;
    return false;
}

static int apply_keyboard(int fd, uint8_t modifiers, const uint8_t keys[6]) {
    for (int bit = 0; bit < 8; ++bit) {
        bool old_down = (g_modifiers & (1u << bit)) != 0;
        bool new_down = (modifiers & (1u << bit)) != 0;
        if (old_down && !new_down && emit_event(fd, EV_KEY, (unsigned short)modifier_codes[bit], 0) < 0) return -1;
    }
    for (int i = 0; i < 6; ++i) {
        uint8_t usage = g_keys[i];
        if (usage && !contains_usage(keys, usage)) {
            int code = usage_to_code(usage);
            if (code < 0 || emit_event(fd, EV_KEY, (unsigned short)code, 0) < 0) return -1;
        }
    }
    for (int bit = 0; bit < 8; ++bit) {
        bool old_down = (g_modifiers & (1u << bit)) != 0;
        bool new_down = (modifiers & (1u << bit)) != 0;
        if (!old_down && new_down && emit_event(fd, EV_KEY, (unsigned short)modifier_codes[bit], 1) < 0) return -1;
    }
    for (int i = 0; i < 6; ++i) {
        uint8_t usage = keys[i];
        if (usage && !contains_usage(g_keys, usage)) {
            int code = usage_to_code(usage);
            if (code < 0 || emit_event(fd, EV_KEY, (unsigned short)code, 1) < 0) return -1;
        }
    }
    if (sync_device(fd) < 0) return -1;
    g_modifiers = modifiers;
    memcpy(g_keys, keys, sizeof(g_keys));
    return 0;
}

static int apply_mouse(int fd, uint8_t buttons, int dx, int dy, int wheel) {
    const unsigned short button_codes[3] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE};
    for (int bit = 0; bit < 3; ++bit) {
        bool old_down = (g_buttons & (1u << bit)) != 0;
        bool new_down = (buttons & (1u << bit)) != 0;
        if (old_down != new_down && emit_event(fd, EV_KEY, button_codes[bit], new_down ? 1 : 0) < 0) return -1;
    }
    if (dx && emit_event(fd, EV_REL, REL_X, dx) < 0) return -1;
    if (dy && emit_event(fd, EV_REL, REL_Y, dy) < 0) return -1;
    if (wheel && emit_event(fd, EV_REL, REL_WHEEL, wheel) < 0) return -1;
    if (sync_device(fd) < 0) return -1;
    g_buttons = buttons;
    return 0;
}

static void release_all(int keyboard_fd, int mouse_fd) {
    const uint8_t empty[6] = {0};
    (void)apply_keyboard(keyboard_fd, 0, empty);
    (void)apply_mouse(mouse_fd, 0, 0, 0, 0);
}

static int create_keyboard(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) goto fail;
    for (int bit = 0; bit < 8; ++bit) if (ioctl(fd, UI_SET_KEYBIT, modifier_codes[bit]) < 0) goto fail;
    for (unsigned int usage = 0x04; usage <= 0x09; ++usage) {
        int code = usage_to_code(usage);
        if (code < 0 || ioctl(fd, UI_SET_KEYBIT, code) < 0) goto fail;
    }
    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL; setup.id.vendor = 0x1209; setup.id.product = 0x0266;
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", "WinInspect-L2-Keyboard");
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) goto fail;
    return fd;
fail:
    close(fd); return -1;
}

static int create_mouse(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, BTN_LEFT) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_REL) < 0 ||
        ioctl(fd, UI_SET_RELBIT, REL_X) < 0 ||
        ioctl(fd, UI_SET_RELBIT, REL_Y) < 0 ||
        ioctl(fd, UI_SET_RELBIT, REL_WHEEL) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) goto fail;
    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL; setup.id.vendor = 0x1209; setup.id.product = 0x0267;
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", "WinInspect-L2-Mouse");
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) goto fail;
    return fd;
fail:
    close(fd); return -1;
}

static int reply(int fd, const char *text) {
    size_t n = strlen(text);
    return send(fd, text, n, MSG_NOSIGNAL) == (ssize_t)n ? 0 : -1;
}

static int read_line(int fd, char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char ch;
        ssize_t rc = recv(fd, &ch, 1, 0);
        if (rc == 0) return 0;
        if (rc < 0) { if (errno == EINTR) continue; return -1; }
        if (ch == '\n') { buf[n] = '\0'; return 1; }
        if (ch != '\r') buf[n++] = ch;
    }
    return -2;
}

static int handle_client(int client, int keyboard_fd, int mouse_fd, const char *token,
                         unsigned int *accepted, unsigned int *rejected) {
    char line[256];
    if (read_line(client, line, sizeof(line)) != 1) return -1;
    char expected[160];
    snprintf(expected, sizeof(expected), "AUTH %s", token);
    if (strcmp(line, expected) != 0) { (void)reply(client, "ERR auth\n"); return -1; }
    if (reply(client, "OK\n") < 0) return -1;

    for (;;) {
        int rc = read_line(client, line, sizeof(line));
        if (rc == 0) return 0;
        if (rc < 0) return -1;

        if (line[0] == 'K' && line[1] == ' ') {
            unsigned int m,k0,k1,k2,k3,k4,k5; char extra;
            int fields = sscanf(line,"K %u %u %u %u %u %u %u %c",&m,&k0,&k1,&k2,&k3,&k4,&k5,&extra);
            if (fields != 7 || m > 255 || k0 > 255 || k1 > 255 || k2 > 255 || k3 > 255 || k4 > 255 || k5 > 255) {
                ++*rejected; if (reply(client,"ERR keyboard-format\n") < 0) return -1; continue;
            }
            uint8_t keys[6]={(uint8_t)k0,(uint8_t)k1,(uint8_t)k2,(uint8_t)k3,(uint8_t)k4,(uint8_t)k5};
            bool supported = true;
            for (int i=0;i<6;++i) if (keys[i] && usage_to_code(keys[i]) < 0) supported=false;
            if (!supported) { ++*rejected; if (reply(client,"ERR keyboard-usage\n") < 0) return -1; continue; }
            if (apply_keyboard(keyboard_fd,(uint8_t)m,keys) < 0) return -1;
            ++*accepted; if (reply(client,"OK\n") < 0) return -1; continue;
        }

        if (line[0] == 'M' && line[1] == ' ') {
            int buttons,dx,dy,wheel; char extra;
            int fields = sscanf(line,"M %d %d %d %d %c",&buttons,&dx,&dy,&wheel,&extra);
            if (fields != 4 || buttons < 0 || buttons > 7 || dx < -127 || dx > 127 ||
                dy < -127 || dy > 127 || wheel < -127 || wheel > 127) {
                ++*rejected; if (reply(client,"ERR mouse-format\n") < 0) return -1; continue;
            }
            if (apply_mouse(mouse_fd,(uint8_t)buttons,dx,dy,wheel) < 0) return -1;
            ++*accepted; if (reply(client,"OK\n") < 0) return -1; continue;
        }

        if (strcmp(line,"R") == 0) {
            release_all(keyboard_fd,mouse_fd);
            ++*accepted; if (reply(client,"OK\n") < 0) return -1; continue;
        }

        ++*rejected; if (reply(client,"ERR command\n") < 0) return -1;
    }
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr,"usage: %s UID GID TOKEN\n",argv[0]);
        return 2;
    }
    char *end=NULL;
    unsigned long uid_ul=strtoul(argv[1],&end,10); if (!end || *end) return 3;
    end=NULL; unsigned long gid_ul=strtoul(argv[2],&end,10); if (!end || *end) return 4;
    const char *token=argv[3]; if (!*token || strlen(token)>128 || strchr(token,'\n') || strchr(token,'\r')) return 5;
    if (geteuid()!=0) return 6;

    int keyboard_fd=create_keyboard();
    int mouse_fd=create_mouse();
    if (keyboard_fd<0 || mouse_fd<0) return 7;

    if (setgroups(0,NULL)<0 || setgid((gid_t)gid_ul)<0 || setuid((uid_t)uid_ul)<0) return 8;
    if (geteuid()!=(uid_t)uid_ul || getegid()!=(gid_t)gid_ul) return 9;

    int listener=socket(AF_INET,SOCK_STREAM,0); if (listener<0) return 10;
    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK); addr.sin_port=0;
    if (bind(listener,(struct sockaddr*)&addr,sizeof(addr))<0 || listen(listener,1)<0) return 11;
    socklen_t len=sizeof(addr); if (getsockname(listener,(struct sockaddr*)&addr,&len)<0) return 12;

    printf("READY port=%u euid=%lu egid=%lu\n",(unsigned)ntohs(addr.sin_port),
           (unsigned long)geteuid(),(unsigned long)getegid());
    fflush(stdout);

    int client=accept(listener,NULL,NULL); if (client<0) return 13;
    unsigned int accepted=0,rejected=0;
    int client_result=handle_client(client,keyboard_fd,mouse_fd,token,&accepted,&rejected);
    close(client);
    release_all(keyboard_fd,mouse_fd);
    close(listener);

    int mouse_destroy=ioctl(mouse_fd,UI_DEV_DESTROY);
    int keyboard_destroy=ioctl(keyboard_fd,UI_DEV_DESTROY);
    close(mouse_fd); close(keyboard_fd);

    printf("RESULT accepted=%u rejected=%u disconnect_release=1 client_result=%d keyboard_destroy=%d mouse_destroy=%d euid=%lu egid=%lu\n",
           accepted,rejected,client_result,keyboard_destroy,mouse_destroy,
           (unsigned long)geteuid(),(unsigned long)getegid());
    fflush(stdout);
    return (client_result==0 && rejected==2 && keyboard_destroy==0 && mouse_destroy==0) ? 0 : 14;
}
