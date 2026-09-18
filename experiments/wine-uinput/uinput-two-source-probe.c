#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static const char *k_name_a = "WinInspect-Actions-Probe-Keyboard-A";
static const char *k_name_b = "WinInspect-Actions-Probe-Keyboard-B";

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
    for (int i = 0; i < 100; ++i) {
        if (file_contains(path, needle)) {
            printf("xorg_attached=%s\n", name);
            fflush(stdout);
            return 0;
        }
        sleep_ms(100);
    }
    fprintf(stderr, "Xorg attachment not observed for %s\n", name);
    return -1;
}

static int emit_event(int fd, unsigned short type, unsigned short code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type; ev.code = code; ev.value = value;
    return write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev) ? 0 : -1;
}

static int emit_key(int fd, unsigned short code) {
    if (emit_event(fd, EV_KEY, code, 1) < 0 || emit_event(fd, EV_SYN, SYN_REPORT, 0) < 0) return -1;
    sleep_ms(75);
    if (emit_event(fd, EV_KEY, code, 0) < 0 || emit_event(fd, EV_SYN, SYN_REPORT, 0) < 0) return -1;
    return 0;
}

static int create_keyboard(const char *name, unsigned short product, unsigned short key) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, key) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) {
        close(fd); return -1;
    }
    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1D6B;
    setup.id.product = product;
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", name);
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        close(fd); return -1;
    }
    printf("created=%s product=0x%04x\n", name, product);
    fflush(stdout);
    return fd;
}

static void destroy_keyboard(int fd) {
    if (fd >= 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
    }
}

int main(void) {
    int a = create_keyboard(k_name_a, 0x0104, KEY_A);
    int b = create_keyboard(k_name_b, 0x0105, KEY_B);
    if (a < 0 || b < 0) {
        fprintf(stderr, "two-source uinput create failed: errno=%d (%s)\n", errno, strerror(errno));
        destroy_keyboard(a); destroy_keyboard(b); return 2;
    }
    if (wait_for_xorg_attachment(k_name_a) < 0 || wait_for_xorg_attachment(k_name_b) < 0) {
        destroy_keyboard(a); destroy_keyboard(b); return 3;
    }
    sleep_ms(500);
    if (emit_key(a, KEY_A) < 0) {
        fprintf(stderr, "A emission failed\n"); destroy_keyboard(a); destroy_keyboard(b); return 4;
    }
    printf("emitted=%s KEY_A_make_break\n", k_name_a); fflush(stdout);
    sleep_ms(350);
    if (emit_key(b, KEY_B) < 0) {
        fprintf(stderr, "B emission failed\n"); destroy_keyboard(a); destroy_keyboard(b); return 5;
    }
    printf("emitted=%s KEY_B_make_break\n", k_name_b); fflush(stdout);
    /* Keep both devices alive long enough for XInput identity sampling. */
    sleep_ms(3000);
    destroy_keyboard(b); destroy_keyboard(a);
    return 0;
}
