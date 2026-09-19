#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static const char *KBD_A = "WinInspect-Multi-Kbd-A";
static const char *KBD_B = "WinInspect-Multi-Kbd-B";
static const char *MOUSE_A = "WinInspect-Multi-Mouse-A";
static const char *MOUSE_B = "WinInspect-Multi-Mouse-B";

static void sleep_ms(long ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}

static int file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[2048];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) { found = 1; break; }
    }
    fclose(f);
    return found;
}

static int wait_for_xorg_attachment(const char *name) {
    const char *path = getenv("WININSPECT_XORG_LOG");
    if (!path || !path[0]) path = "evidence/Xorg.log";
    char needle[512];
    snprintf(needle, sizeof(needle), "XINPUT: Adding extended input device \"%s\"", name);
    for (int i = 0; i < 120; ++i) {
        if (file_contains(path, needle)) return 0;
        sleep_ms(100);
    }
    fprintf(stderr, "Xorg attachment not observed for %s\n", name);
    return -1;
}

static int emit_event(int fd, unsigned short type, unsigned short code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    return write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev) ? 0 : -1;
}

static int sync_fd(int fd) {
    return emit_event(fd, EV_SYN, SYN_REPORT, 0);
}

static int create_keyboard(const char *name, unsigned short product) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, KEY_A) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, KEY_B) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) {
        close(fd);
        return -1;
    }
    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1209;
    setup.id.product = product;
    setup.id.version = 1;
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", name);
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        close(fd);
        return -1;
    }
    printf("created=%s product=0x%04x\n", name, product);
    fflush(stdout);
    return fd;
}

static int create_mouse(const char *name, unsigned short product) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, BTN_LEFT) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_REL) < 0 ||
        ioctl(fd, UI_SET_RELBIT, REL_X) < 0 ||
        ioctl(fd, UI_SET_RELBIT, REL_Y) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) {
        close(fd);
        return -1;
    }
    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1209;
    setup.id.product = product;
    setup.id.version = 1;
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", name);
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        close(fd);
        return -1;
    }
    printf("created=%s product=0x%04x\n", name, product);
    fflush(stdout);
    return fd;
}

static int drop_privileges(uid_t uid, gid_t gid) {
    if (setgroups(0, NULL) < 0) return -1;
    if (setgid(gid) < 0) return -1;
    if (setuid(uid) < 0) return -1;
    return (geteuid() == uid && getegid() == gid) ? 0 : -1;
}

static void destroy_device(int fd) {
    if (fd >= 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
    }
}

static int key_state(int fd, unsigned short key, int value) {
    return emit_event(fd, EV_KEY, key, value) < 0 || sync_fd(fd) < 0 ? -1 : 0;
}

static int mouse_button(int fd, unsigned short button, int value) {
    return emit_event(fd, EV_KEY, button, value) < 0 || sync_fd(fd) < 0 ? -1 : 0;
}

static int mouse_move(int fd, unsigned short axis, int value) {
    return emit_event(fd, EV_REL, axis, value) < 0 || sync_fd(fd) < 0 ? -1 : 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s TARGET_UID TARGET_GID\n", argv[0]);
        return 2;
    }
    uid_t uid = (uid_t)strtoul(argv[1], NULL, 10);
    gid_t gid = (gid_t)strtoul(argv[2], NULL, 10);

    int ka = -1, kb = -1, ma = -1, mb = -1;
    ka = create_keyboard(KBD_A, 0x0301);
    kb = create_keyboard(KBD_B, 0x0302);
    ma = create_mouse(MOUSE_A, 0x0303);
    mb = create_mouse(MOUSE_B, 0x0304);
    if (ka < 0 || kb < 0 || ma < 0 || mb < 0) {
        fprintf(stderr, "uinput create failed errno=%d (%s)\n", errno, strerror(errno));
        goto fail_create;
    }

    if (drop_privileges(uid, gid) < 0) {
        fprintf(stderr, "privilege drop failed errno=%d (%s) euid=%u egid=%u\n",
                errno, strerror(errno), (unsigned)geteuid(), (unsigned)getegid());
        goto fail_create;
    }
    printf("privilege_drop=ok euid=%u egid=%u\n", (unsigned)geteuid(), (unsigned)getegid());
    fflush(stdout);

    if (wait_for_xorg_attachment(KBD_A) < 0 ||
        wait_for_xorg_attachment(KBD_B) < 0 ||
        wait_for_xorg_attachment(MOUSE_A) < 0 ||
        wait_for_xorg_attachment(MOUSE_B) < 0) {
        goto fail_runtime;
    }

    printf("ready=1\n");
    fflush(stdout);
    sleep_ms(750);

    /* Keyboard interleave: A remains held while B makes and breaks. */
    if (key_state(ka, KEY_A, 1) < 0) goto fail_runtime;
    printf("emit=kbdA KEY_A down\n"); fflush(stdout);
    sleep_ms(120);
    if (key_state(kb, KEY_B, 1) < 0) goto fail_runtime;
    printf("emit=kbdB KEY_B down\n"); fflush(stdout);
    sleep_ms(120);
    if (key_state(kb, KEY_B, 0) < 0) goto fail_runtime;
    printf("emit=kbdB KEY_B up\n"); fflush(stdout);
    sleep_ms(120);
    if (key_state(ka, KEY_A, 0) < 0) goto fail_runtime;
    printf("emit=kbdA KEY_A up\n"); fflush(stdout);

    sleep_ms(250);

    /* Mouse interleave: A left button stays held while B moves/clicks. */
    if (mouse_button(ma, BTN_LEFT, 1) < 0) goto fail_runtime;
    printf("emit=mouseA BTN_LEFT down\n"); fflush(stdout);
    sleep_ms(120);
    if (mouse_move(mb, REL_Y, -13) < 0) goto fail_runtime;
    printf("emit=mouseB REL_Y -13\n"); fflush(stdout);
    if (mouse_button(mb, BTN_RIGHT, 1) < 0) goto fail_runtime;
    printf("emit=mouseB BTN_RIGHT down\n"); fflush(stdout);
    sleep_ms(120);
    if (mouse_button(mb, BTN_RIGHT, 0) < 0) goto fail_runtime;
    printf("emit=mouseB BTN_RIGHT up\n"); fflush(stdout);
    sleep_ms(120);
    if (mouse_move(ma, REL_X, 17) < 0) goto fail_runtime;
    printf("emit=mouseA REL_X 17\n"); fflush(stdout);
    sleep_ms(120);
    if (mouse_button(ma, BTN_LEFT, 0) < 0) goto fail_runtime;
    printf("emit=mouseA BTN_LEFT up\n"); fflush(stdout);

    printf("emission_complete=1\n");
    fflush(stdout);
    sleep_ms(1800);

    destroy_device(mb);
    destroy_device(ma);
    destroy_device(kb);
    destroy_device(ka);
    return 0;

fail_runtime:
    fprintf(stderr, "runtime emission/attachment failure errno=%d (%s)\n", errno, strerror(errno));
fail_create:
    destroy_device(mb);
    destroy_device(ma);
    destroy_device(kb);
    destroy_device(ka);
    return 3;
}
