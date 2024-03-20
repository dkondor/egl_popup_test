#define _GNU_SOURCE
#include <linux/input-event-codes.h>
#include <assert.h>
#include <GLES2/gl2.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#include "egl_common.h"
#include "xdg-shell-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#define UNUSED __attribute__((unused))

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_seat *seat;
static struct wl_shm *shm;
static struct wl_pointer *pointer;
static struct xdg_wm_base *xdg_wm_base;
static struct zwlr_foreign_toplevel_manager_v1* toplevel_manager = NULL;

static struct xdg_surface* xdg_surface = NULL;
static struct xdg_toplevel* toplevel = NULL;
static struct wl_output *wl_output;

struct wl_surface *wl_surface;
struct wl_egl_window *egl_window;
struct wlr_egl_surface *egl_surface;
struct wl_callback *frame_callback;

static uint32_t output = UINT32_MAX;
struct xdg_popup *popup;
struct xdg_surface *popup_surface;
struct wl_surface *popup_wl_surface;
struct wl_egl_window *popup_egl_window;
static uint32_t popup_width = 256, popup_height = 256;
struct wlr_egl_surface *popup_egl_surface;
struct wl_callback *popup_frame_callback;
float popup_alpha = 1.0, popup_red = 0.5f;

static uint32_t width = 256, height = 256;
static bool run_display = true;

static int is_child = 0;
static useconds_t sleep_interval = 5000;
static bool do_roundtrip = false;
static bool set_swapinterval = false;
static bool set_swapinterval_main = false;
static bool set_swapinterval_popup = false;
static bool use_frame_callback = true;
static bool draw_popup_on_click = true;

// whether the pointer is in the popup
static int pointer_popup = 0;

static float color_int = 0.0f;
static float color_inc = 1.0f;
static struct timespec last_frame;

static const char parent_app_id[] = "egl-test-parent";
static const char child_app_id[] = "egl-test-child";

static void draw(void);
static void draw_popup(void);

typedef struct zwlr_foreign_toplevel_handle_v1 wfthandle;
static wfthandle* child_handle = NULL;


static void toplevel_title_cb(UNUSED void *data, UNUSED wfthandle *handle, UNUSED const char *title) {
	/* we don't care */
}

static void toplevel_appid_cb(UNUSED void *data, wfthandle *handle, const char *app_id) {
	/* set our handle to the child */
	if(app_id && !child_handle && !strcmp(app_id, child_app_id)) {
		child_handle = handle;
		fprintf(stderr, "Child found\n");
	}
	/* otherwise we don't care */
	else zwlr_foreign_toplevel_handle_v1_destroy(handle);
}

static void toplevel_output_enter_cb(UNUSED void *data, UNUSED wfthandle *handle, UNUSED struct wl_output *output) {
	/* we don't care */
}
static void toplevel_output_leave_cb(UNUSED void *data, UNUSED wfthandle *handle, UNUSED struct wl_output *output) {
	/* we don't care */
}

static void toplevel_state_cb(UNUSED void *data, UNUSED wfthandle *handle, UNUSED struct wl_array *state) {
	/* we don't care */
}

static void toplevel_done_cb(UNUSED void *data, UNUSED wfthandle *handle) {
	/* we don't care */
}

static void toplevel_parent_cb(UNUSED void *data, UNUSED wfthandle *handle, wfthandle*) {
	/* we don't care */
}

static void toplevel_closed_cb (UNUSED void *data, UNUSED wfthandle *handle) {
	if(handle == child_handle) {
		child_handle = NULL;
		fprintf(stderr, "Child lost\n");
	}
	zwlr_foreign_toplevel_handle_v1_destroy(handle);
}

struct zwlr_foreign_toplevel_handle_v1_listener toplevel_handle_listener = {
    .title        = toplevel_title_cb,
    .app_id       = toplevel_appid_cb,
    .output_enter = toplevel_output_enter_cb,
    .output_leave = toplevel_output_leave_cb,
    .state        = toplevel_state_cb,
    .done         = toplevel_done_cb,
    .closed       = toplevel_closed_cb,
    .parent       = toplevel_parent_cb
};

static void new_toplevel (UNUSED void *data,
		UNUSED struct zwlr_foreign_toplevel_manager_v1 *manager,
		wfthandle *handle) {
	/* note: we cannot do anything as long as we get app_id */
	zwlr_foreign_toplevel_handle_v1_add_listener (handle, &toplevel_handle_listener, NULL);
}

static void toplevel_manager_finished(UNUSED void *data, UNUSED struct zwlr_foreign_toplevel_manager_v1 *manager) {
	/* don't care */
}

static struct zwlr_foreign_toplevel_manager_v1_listener toplevel_manager_listener = {
    .toplevel = new_toplevel,
    .finished = toplevel_manager_finished,
};


static void surface_frame_callback(
		void *data, struct wl_callback *cb, uint32_t time) {
	wl_callback_destroy(cb);
	frame_callback = NULL;
	draw();
}

static struct wl_callback_listener frame_listener = {
	.done = surface_frame_callback
};

static void popup_surface_frame_callback(
		void *data, struct wl_callback *cb, uint32_t time) {
	wl_callback_destroy(cb);
	popup_frame_callback = NULL;
	if (popup) {
		draw_popup();
	}
}

static struct wl_callback_listener popup_frame_listener = {
	.done = popup_surface_frame_callback
};


static void draw(void) {
	eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
	
	if(set_swapinterval_main) {
		if(!eglSwapInterval(egl_display, 0))
			fprintf(stderr, "Cannot set eglSwapInterval() on main surface!\n");
		set_swapinterval_main = false;
	}
	
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	long ms = (ts.tv_sec - last_frame.tv_sec) * 1000 +
		(ts.tv_nsec - last_frame.tv_nsec) / 1000000;
	color_int += color_inc * ms / 2000.0f;
	if(color_int > 1.0f) {
		color_int = 1.0f;
		color_inc = -1.0f;
	}
	else if(color_int < 0.0f) {
		color_int = 0.0f;
		color_inc = 1.0f;
	}

	glViewport(0, 0, width, height);
	glClearColor(is_child ? 0.0f : color_int, 0.0f, is_child ? color_int : 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	if(!frame_callback && use_frame_callback) {
		frame_callback = wl_surface_frame(wl_surface);
		wl_callback_add_listener(frame_callback, &frame_listener, NULL);
	}

	eglSwapBuffers(egl_display, egl_surface);

	last_frame = ts;
}

static void draw_popup(void) {
	if(!popup) return;
	static float alpha_mod = -0.01;

	eglMakeCurrent(egl_display, popup_egl_surface, popup_egl_surface, egl_context);
		
	if(set_swapinterval_popup) {
		if(!eglSwapInterval(egl_display, 0))
			 fprintf(stderr, "Cannot set eglSwapInterval() on main popup!\n");
		set_swapinterval_popup = false;
	}
	
	glViewport(0, 0, popup_width, popup_height);
	glClearColor(popup_red * popup_alpha, 0.5f * popup_alpha,
		0.5f * popup_alpha, 1.0f);
	popup_alpha += alpha_mod;
	if (popup_alpha < 0.2f || popup_alpha >= 1.0f) {
		alpha_mod *= -1.0;
	}
	glClear(GL_COLOR_BUFFER_BIT);

	if(!popup_frame_callback && use_frame_callback) {
		popup_frame_callback = wl_surface_frame(popup_wl_surface);
		assert(popup_frame_callback);
		wl_callback_add_listener(popup_frame_callback, &popup_frame_listener, NULL);
	}
	eglSwapBuffers(egl_display, popup_egl_surface);
	wl_surface_commit(popup_wl_surface);
}

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_popup_configure(void *data, struct xdg_popup *xdg_popup,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	fprintf(stderr, "Popup configured %dx%d@%d,%d\n", width, height, x, y);
	popup_width = width;
	popup_height = height;
	if (popup_egl_window) {
		wl_egl_window_resize(popup_egl_window, width, height, 0, 0);
	}
}

static void popup_destroy(void) {
	eglDestroySurface(egl_display, popup_egl_surface);
	wl_egl_window_destroy(popup_egl_window);
	xdg_popup_destroy(popup);
	xdg_surface_destroy(popup_surface);
	wl_surface_destroy(popup_wl_surface);
	popup_wl_surface = NULL;
	popup = NULL;
	popup_surface = NULL;
	popup_egl_window = NULL;
	set_swapinterval_popup = set_swapinterval;
}

static void xdg_popup_done(void *data, struct xdg_popup *xdg_popup) {
	fprintf(stderr, "Popup done\n");
	popup_destroy();
}

static const struct xdg_popup_listener xdg_popup_listener = {
	.configure = xdg_popup_configure,
	.popup_done = xdg_popup_done,
};

static void create_popup(void) {
	if (popup) {
		return;
	}
	struct wl_surface *surface = wl_compositor_create_surface(compositor);
	assert(xdg_wm_base && surface);
	popup_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
	struct xdg_positioner *xdg_positioner =
		xdg_wm_base_create_positioner(xdg_wm_base);
	assert(popup_surface && xdg_positioner);

	xdg_positioner_set_size(xdg_positioner, popup_width, popup_height);
	xdg_positioner_set_offset(xdg_positioner, 0, 0);
	xdg_positioner_set_anchor_rect(xdg_positioner, 100, 200, 1, 1);
	xdg_positioner_set_anchor(xdg_positioner, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);

	popup = xdg_surface_get_popup(popup_surface, xdg_surface, xdg_positioner);
	// xdg_popup_grab(popup, seat, serial);

	assert(popup);

	xdg_surface_add_listener(popup_surface, &xdg_surface_listener, NULL);
	xdg_popup_add_listener(popup, &xdg_popup_listener, NULL);

	wl_surface_commit(surface);
	wl_display_roundtrip(display);

	xdg_positioner_destroy(xdg_positioner);

	popup_wl_surface = surface;
	popup_egl_window = wl_egl_window_create(surface, popup_width, popup_height);
	assert(popup_egl_window);
	popup_egl_surface = eglCreatePlatformWindowSurfaceEXT(
			egl_display, egl_config, popup_egl_window, NULL);
	assert(popup_egl_surface != EGL_NO_SURFACE);
	draw_popup();
}

static void xdg_toplevel_handle_configure(UNUSED void *data,
		UNUSED struct xdg_toplevel *xdg_toplevel, int32_t w, int32_t h,
		UNUSED struct wl_array *states) {
	if(w) width = w;
	if(h) height = h;
	if (egl_window) {
		wl_egl_window_resize(egl_window, width, height, 0, 0);
	}
}

static void xdg_toplevel_handle_close(UNUSED void *data,
		UNUSED struct xdg_toplevel *xdg_toplevel) {
	run_display = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};


static void wl_pointer_enter(void*, struct wl_pointer*, uint32_t,
		struct wl_surface *surface, wl_fixed_t, wl_fixed_t) {
	if (surface == popup_wl_surface) pointer_popup = 1;
	else pointer_popup = 0;
}

static void wl_pointer_leave(void*, struct wl_pointer*, uint32_t, struct wl_surface*) {
	pointer_popup = 0;
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	// no-op
}

static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	if(!state) return;
	if(button == BTN_RIGHT)
	{
		run_display = false;
		return;
	}
	if(pointer_popup) {
		if(child_handle) {
			fprintf(stderr, "\n\n\nSending activate, this should close the popup\n");
			zwlr_foreign_toplevel_handle_v1_activate(child_handle, seat);
			if(do_roundtrip) wl_display_roundtrip(display);
			else wl_display_flush(display);
			if(sleep_interval) usleep(sleep_interval);
			fprintf(stderr, "\n\n\nCommiting the popup\n\n\n");
			if(draw_popup_on_click) draw_popup();
		}
	}
	else if (!popup_wl_surface) create_popup( /* serial */ );
}

static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	// Who cares
}

static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
	// Who cares
}

static void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis_source) {
	// Who cares
}

static void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis) {
	// Who cares
}

static void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, int32_t discrete) {
	// Who cares
}

struct wl_pointer_listener pointer_listener = {
	.enter = wl_pointer_enter,
	.leave = wl_pointer_leave,
	.motion = wl_pointer_motion,
	.button = wl_pointer_button,
	.axis = wl_pointer_axis,
	.frame = wl_pointer_frame,
	.axis_source = wl_pointer_axis_source,
	.axis_stop = wl_pointer_axis_stop,
	.axis_discrete = wl_pointer_axis_discrete,
};

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
		pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat,
		const char *name) {
	// Who cares
}

const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, 1);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(registry, name,
				&wl_shm_interface, 1);
	} else if (strcmp(interface, "wl_output") == 0) {
		if (output != UINT32_MAX) {
			if (!wl_output) {
				wl_output = wl_registry_bind(registry, name,
						&wl_output_interface, 1);
			} else {
				output--;
			}
		}
	} else if(!is_child && strcmp(interface,
			zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
		toplevel_manager = wl_registry_bind(registry, name,
			&zwlr_foreign_toplevel_manager_v1_interface, version);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(registry, name,
				&wl_seat_interface, 1);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		xdg_wm_base = wl_registry_bind(
				registry, name, &xdg_wm_base_interface, 1);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// who cares
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

int main(int argc, char **argv) {
	int i;
	int create_child = 1;
	for(i = 1; i < argc; i++) {
		if(argv[i][0] == '-') switch(argv[i][1]) {
			case 's':
				sleep_interval = atoi(argv[i+1]);
				i++;
				break;
			case 'C':
				create_child = 0;
				break;
			case 'r':
				do_roundtrip = true;
				break;
			case 'f':
				use_frame_callback = false;
				break;
			case 'S':
				set_swapinterval = true;
				set_swapinterval_main = true;
				set_swapinterval_popup = true;
				break;
			case 'd':
				draw_popup_on_click = false;
				break;
			default:
				fprintf(stderr, "Unknown parameter: %s!\n", argv[i]);
				break;
		}
		else fprintf(stderr, "Unknown parameter: %s!\n", argv[i]);		
	}
	
	if(create_child) {
		signal(SIGCHLD, SIG_IGN);
		int pid = fork();
		if(pid < 0) {
			fprintf(stderr, "Error creating child process!\n");
			return 1;
		}
		is_child = pid ? 0 : 1;
		if(is_child) {
			/* we don't want the child's debug messages */
			int fd = open("/dev/null", O_CLOEXEC | O_WRONLY);
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			setsid();
		}
	}
	
	if(!is_child) usleep(200000);
	
	display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "Failed to create display\n");
		return 1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	assert(compositor && seat && xdg_wm_base);
	
	if(create_child && !is_child) {
		assert(toplevel_manager);
		zwlr_foreign_toplevel_manager_v1_add_listener(toplevel_manager, &toplevel_manager_listener, NULL);
	}

	egl_init(display);
	wl_surface = wl_compositor_create_surface(compositor);
	assert(wl_surface);

	xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, wl_surface);
	assert(xdg_surface);
	toplevel = xdg_surface_get_toplevel(xdg_surface);
	assert(toplevel);
	
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel_add_listener(toplevel, &xdg_toplevel_listener, NULL);
	xdg_toplevel_set_min_size(toplevel, width, height);
	xdg_toplevel_set_title(toplevel, is_child ? "egl-popup-test-child" : "egl-popup-test-parent");
	xdg_toplevel_set_app_id(toplevel, is_child ? child_app_id : parent_app_id);
	
	wl_surface_commit(wl_surface);
	wl_display_roundtrip(display);
	
	egl_window = wl_egl_window_create(wl_surface, width, height);
	assert(egl_window);
	egl_surface = eglCreatePlatformWindowSurfaceEXT(
		egl_display, egl_config, egl_window, NULL);
	assert(egl_surface != EGL_NO_SURFACE);

	wl_display_roundtrip(display);
	draw();
	
	if(!is_child) create_popup();

	while (wl_display_dispatch(display) != -1 && run_display) {
		if(!use_frame_callback) {
			draw();
			if(popup) draw_popup();
		}
	}
	
	printf("Exiting...\n");
	
	eglDestroySurface(egl_display, egl_surface);
	wl_egl_window_destroy(egl_window);
	xdg_toplevel_destroy(toplevel);
	xdg_surface_destroy(xdg_surface);
	
	printf("Succesfully closed our EGL window\n");

	return 0;
}
