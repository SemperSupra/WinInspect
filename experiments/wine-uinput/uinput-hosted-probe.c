#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static const char *k_device_name = "WinInspect-Actions-Probe-Keyboard";

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
    }
}

static int file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    char line[2048];
    int found = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strstr(line, needle) != NULL) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

static int wait_for_xorg_attachment(void) {
    const char *path = getenv("WININSPECT_XORG_LOG");
    if (!path || path[0] == '\0') {
        path = "evidence/Xorg.log";
    }

    char needle[512];
    snprintf(needle, sizeof(needle), "XINPUT: Adding extended input device \"%s\"", k_device_name);

    for (int i = 0; i < 100; ++i) {
        if (file_contains(path, needle)) {
            printf("xorg_attached=%s\n", k_device_name);
            fflush(stdout);
            sleep_ms(250);
            return 0;
        }
        sleep_ms(100);
    }

    fprintf(stderr, "Xorg attachment was not observed in %s within 10 seconds\n", path);
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

static int emit_key_a(int fd) {
    if (emit_event(fd, EV_KEY, KEY_A, 1) < 0 || emit_event(fd, EV_SYN, SYN_REPORT, 0) < 0) {
        return -1;
    }
    sleep_ms(50);
    if (emit_event(fd, EV_KEY, KEY_A, 0) < 0 || emit_event(fd, EV_SYN, SYN_REPORT, 0) < 0) {
        return -1;
    }
    return 0;
}

int main(void) {
    const char *dev = "/dev/uinput";
    int fd = open(dev, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: errno=%d (%s)\n", dev, errno, strerror(errno));
        return 2;
    }

    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, KEY_A) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) {
        fprintf(stderr, "uinput capability ioctl failed: errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return 3;
    }

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1D6B;
    setup.id.product = 0x0104;
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", k_device_name);

    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0) {
        fprintf(stderr, "UI_DEV_SETUP failed: errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return 4;
    }
    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "UI_DEV_CREATE failed: errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return 5;
    }

    printf("created=%s\n", k_device_name);
    fflush(stdout);

    if (wait_for_xorg_attachment() < 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
        return 7;
    }

    if (emit_key_a(fd) < 0) {
        fprintf(stderr, "event emission failed: errno=%d (%s)\n", errno, strerror(errno));
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
        return 6;
    }
    printf("emitted=KEY_A_make_break\n");
    fflush(stdout);
    sleep_ms(2000);

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
