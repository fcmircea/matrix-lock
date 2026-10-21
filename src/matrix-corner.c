/* matrix-corner: arm the bottom-right screen corner for a few seconds; the
 * pointer touching it runs matrix-lock with the same 60 s grace as the idle
 * lock. The flick itself does not dismiss it: swaylock-plugin measures motion
 * from where the pointer enters, and the corner cannot be pushed past. labwc starts it from a
 * Right Ctrl keybind. It cannot see the key being released without taking
 * the keyboard from the focused window, so it simply expires.
 *
 * If $XDG_RUNTIME_DIR/matrix-corner.dry-run exists, a hit only shows a
 * notification instead of locking (for testing the gesture). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define ARMED_MS 2000   /* how long the corner stays live after the key press */
#define SETTLE_MS 200   /* an enter this soon after mapping means the pointer was parked there */
#define SIZE 4          /* corner zone, logical pixels */

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_surface *surface;
static struct wl_buffer *buffer;
static long mapped_at = -1;
static int hit;

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static struct wl_buffer *transparent_buffer(void) {
    int stride = SIZE * 4, len = stride * SIZE;
    int fd = memfd_create("matrix-corner", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, len) < 0) return NULL;  /* zero-filled = fully transparent */
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, len);
    struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, SIZE, SIZE, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return b;
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
                          struct wl_surface *s, wl_fixed_t x, wl_fixed_t y) {
    if (s == surface && mapped_at >= 0 && now_ms() - mapped_at >= SETTLE_MS) hit = 1;
}
static void pointer_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) {}
static void pointer_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {}
static void pointer_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t b, uint32_t st) {}
static void pointer_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {}
static const struct wl_pointer_listener pointer_listener = {  /* seat v1: no frame/axis_* events */
    .enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion,
    .button = pointer_button, .axis = pointer_axis,
};

static void layer_configure(void *d, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t w, uint32_t h) {
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
    if (mapped_at < 0) mapped_at = now_ms();
}
static void layer_closed(void *d, struct zwlr_layer_surface_v1 *ls) { exit(0); }
static const struct zwlr_layer_surface_v1_listener layer_listener = { layer_configure, layer_closed };

static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t v) {
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name))
        shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
        layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, 1);
}
static void global_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { global, global_remove };

int main(void) {
    const char *run = getenv("XDG_RUNTIME_DIR"), *home = getenv("HOME");
    if (!run || !home) return 1;

    /* One armed corner at a time; the lock fd is not passed on to matrix-lock. */
    char path[512];
    snprintf(path, sizeof path, "%s/matrix-corner.lock", run);
    int lockfd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lockfd < 0 || flock(lockfd, LOCK_EX | LOCK_NB) < 0) return 0;

    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) return 1;
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!compositor || !shm || !seat || !layer_shell) {
        fprintf(stderr, "matrix-corner: compositor lacks wl_shm, wl_seat or layer-shell\n");
        return 1;
    }
    if (!(buffer = transparent_buffer())) return 1;
    wl_pointer_add_listener(wl_seat_get_pointer(seat), &pointer_listener, NULL);

    surface = wl_compositor_create_surface(compositor);
    struct zwlr_layer_surface_v1 *ls = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "matrix-corner");
    zwlr_layer_surface_v1_add_listener(ls, &layer_listener, NULL);
    zwlr_layer_surface_v1_set_size(ls, SIZE, SIZE);
    zwlr_layer_surface_v1_set_anchor(ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(ls, -1);  /* the real screen corner, not above the panel */
    wl_surface_commit(surface);

    long deadline = now_ms() + ARMED_MS;
    struct pollfd pfd = { wl_display_get_fd(dpy), POLLIN, 0 };
    while (!hit) {
        while (wl_display_prepare_read(dpy) != 0) wl_display_dispatch_pending(dpy);
        wl_display_flush(dpy);
        long left = deadline - now_ms();
        if (left <= 0) { wl_display_cancel_read(dpy); return 0; }
        if (poll(&pfd, 1, left) > 0) {
            if (wl_display_read_events(dpy) < 0) return 1;
        } else {
            wl_display_cancel_read(dpy);
        }
        if (wl_display_dispatch_pending(dpy) < 0) return 1;
    }
    wl_display_disconnect(dpy);  /* drop the zone before the lock appears */

    snprintf(path, sizeof path, "%s/matrix-corner.dry-run", run);
    if (access(path, F_OK) == 0)
        execlp("notify-send", "notify-send", "-t", "3000", "matrix-corner", "Corner hit (dry run, no lock)", (char *)NULL);
    snprintf(path, sizeof path, "%s/.local/bin/matrix-lock", home);
    execl(path, path, "--grace", "60", (char *)NULL);
    perror("matrix-corner: exec");
    return 1;
}
