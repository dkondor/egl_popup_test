Simple test case to demonstrate a race condition with xdg_popups and EGL, based on the [wlroots example clients](https://gitlab.freedesktop.org/wlroots/wlr-clients).

The race condition occurs if a client is using EGL to render into an XDG popup that is being closed by the compositor. Specifically, this likely happens if the client calls `eglSwapBuffers()` after the compositor has sent an `xdg_popup.popup_done()` event which has not yet been processed by the client.

This program simulates this condition by using the `wlr-foreign-toplevel-management` protocol to activate another toplevel just before trying to render to the popup. It shows two views: the red / black one is the parent process, the blue / black one is a child process. The grey / black view is an `xdg_popup` created by the parent process. Clicking on the popup will call `zwlr_foreign_toplevel_handle_v1.activate()` on the view belonging to the child process. This should also trigger the compositor closing the popup. The result is that the parent process locks up when trying to render the next frame for the popup. This happens before the `xdg_popup.popup_done()` event is received.

Compiling (requires the Wayland client libraries, protocols and EGL interface to be installed):
```
meson build
ninja -C build
```

Running:
```
build_rel/egl-popup-test
```

Command line options:
```
  -r       do a roundtrip after activating the child view (avoids the problem)
  -S       set eglSwapInterval() to zero in all surfaces used (avoids the problem)
  -f       do not use frame callbacks, but render from the main loop
  -d       do not try to draw the popup immediately after activating the child view
              (avoids the problem except if -f is also set)
  -s <us>  sleep for the given amount (in microseconds) after activating the child
  -C       do not create a child process (cannot demonstrate the problem)
```
