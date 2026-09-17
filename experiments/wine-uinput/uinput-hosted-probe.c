#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

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
    usleep(50000);
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
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "WinInspect-Actions-Probe-Keyboard");

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

    printf("created=WinInspect-Actions-Probe-Keyboard\n");
    fflush(stdout);
    sleep(3);

    if (emit_key_a(fd) < 0) {
        fprintf(stderr, "event emission failed: errno=%d (%s)\n", errno, strerror(errno));
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
        return 6;
    }
    printf("emitted=KEY_A_make_break\n");
    fflush(stdout);
    sleep(2);

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
