/* idle-watch [seconds]: show when the compositor considers the session idle,
 * and count down to the idle lock. Run it in a terminal and walk away; the
 * log it leaves tells why a lock did or did not come.
 *
 * It asks labwc for two idle timers of the same length (default 5 s):
 *   normal      what swayidle sees; an app's idle inhibitor holds it off
 *   input-only  counts keyboard and mouse only, ignores inhibitors
 * Neither firing means input keeps arriving; only input-only firing means an
 * app is inhibiting idle. The lock time is read from ~/.config/swayidle/config.
 * Everything goes to the terminal; it shows no desktop notifications. */
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include "ext-idle-notify-v1-client-protocol.h"

static struct ext_idle_notifier_v1 *notifier;
static struct wl_seat *seat;
static int threshold = 5, lock_at = 300;
static int normal_idle, input_idle;
static long idle_since;   /* when the normal timer fired, minus the threshold */
static long input_since;  /* when the input-only timer fired */
static int warned_inhibit;

static long now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec;
}

static void say(const char *what) {
    time_t t = time(NULL);
    char b[16];
    strftime(b, sizeof b, "%T", localtime(&t));
    printf("\r\033[K%s  %s\n", b, what);
    fflush(stdout);
}

static int locked(void) {
    return system("pgrep -x swaylock-plugin >/dev/null") == 0;
}

static void idled(void *data, struct ext_idle_notification_v1 *n) {
    char msg[128];
    if (data) {
        input_idle = 1;
        input_since = now_s();
        say("idle    (input-only: no key or mouse for the threshold)");
        return;
    }
    normal_idle = 1;
    idle_since = now_s() - threshold;
    snprintf(msg, sizeof msg, "idle    (normal: swayidle's clock runs; lock in %d s)", lock_at - threshold);
    say(msg);
}

static void resumed(void *data, struct ext_idle_notification_v1 *n) {
    char msg[128];
    if (data) {
        input_idle = 0;
        warned_inhibit = 0;
        return;  /* the normal timer reports the resume the user cares about */
    }
    normal_idle = 0;
    if (input_idle) return;  /* no input: an inhibitor stopped the clock, and the INHIBITED line says so */
    snprintf(msg, sizeof msg, "active  (input after %ld s idle)", now_s() - idle_since);
    say(msg);
}

static const struct ext_idle_notification_v1_listener listener = { idled, resumed };

static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t v) {
    if (!strcmp(iface, ext_idle_notifier_v1_interface.name) && v >= 2)
        notifier = wl_registry_bind(r, name, &ext_idle_notifier_v1_interface, 2);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
}
static void global_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { global, global_remove };

/* The first "timeout N ... matrix-lock" line of the swayidle config. */
static void read_lock_time(void) {
    char path[512], line[512];
    snprintf(path, sizeof path, "%s/.config/swayidle/config", getenv("HOME"));
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int n;
        if (strstr(line, "matrix-lock") && sscanf(line, " timeout %d", &n) == 1) { lock_at = n; break; }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc > 1) threshold = atoi(argv[1]);
    if (threshold < 1) threshold = 5;
    read_lock_time();

    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { fprintf(stderr, "idle-watch: no Wayland display\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!notifier || !seat) { fprintf(stderr, "idle-watch: compositor lacks ext-idle-notify v2\n"); return 1; }
    ext_idle_notification_v1_add_listener(
        ext_idle_notifier_v1_get_idle_notification(notifier, threshold * 1000, seat), &listener, NULL);
    ext_idle_notification_v1_add_listener(
        ext_idle_notifier_v1_get_input_idle_notification(notifier, threshold * 1000, seat), &listener, (void *)1);

    char msg[160];
    snprintf(msg, sizeof msg, "watching: idle after %d s without input, lock at %d s (Ctrl-C to stop)", threshold, lock_at);
    say(msg);

    struct pollfd pfd = { wl_display_get_fd(dpy), POLLIN, 0 };
    int was_locked = 0;
    for (;;) {
        while (wl_display_prepare_read(dpy) != 0) wl_display_dispatch_pending(dpy);
        wl_display_flush(dpy);
        if (poll(&pfd, 1, 1000) > 0) {
            if (wl_display_read_events(dpy) < 0) return 1;
        } else {
            wl_display_cancel_read(dpy);
        }
        if (wl_display_dispatch_pending(dpy) < 0) return 1;

        /* Input stopped but the normal clock does not run: something inhibits idle. */
        if (input_idle && !normal_idle && !warned_inhibit && now_s() - input_since >= 2) {
            say("INHIBITED  no input, but an app holds an idle inhibitor (video, keep-awake); swayidle's clock is stopped");
            warned_inhibit = 1;
        }

        int l = normal_idle || was_locked ? locked() : 0;
        if (l != was_locked) {
            say(l ? "LOCKED  swaylock-plugin is running" : "unlocked");
            was_locked = l;
        }
        if (normal_idle && !l) {
            long left = lock_at - (now_s() - idle_since);
            if (left >= 0) printf("\r\033[K  lock in %ld:%02ld", left / 60, left % 60);
            else printf("\r\033[K  lock overdue by %ld s (is swayidle running?)", -left);
            fflush(stdout);
        }
    }
}
